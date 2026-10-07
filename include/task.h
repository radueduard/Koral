//
// Created by radue on 4/15/2026.
//

#pragma once

#include <coroutine>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <atomic>
#include <type_traits>
#include <expected>
#include <optional>
#include <utility>
#include <string>
#include <vector>

#include "token.h"

namespace kor {
    /**
     * @brief Somewhere suspended coroutines are resumed: the main thread, or a background pool.
     *
     * Koral runs two — the main-thread executor, drained once per frame by the run loop, and a
     * background one. Reach them through Context::SwitchToMainThread() and
     * Context::SwitchToBackgroundThread() rather than by name.
     */
    struct Executor {
        virtual ~Executor() = default;

        /** @brief Whether this executor resumes work on the thread the run loop drives. */
        virtual bool IsMainThread() const noexcept { return false; }

        /** @brief Schedules a suspended coroutine to be resumed on this executor. */
        virtual void Enqueue(std::coroutine_handle<>) = 0;

        /**
         * @brief Schedules arbitrary work on this executor.
         *
         * What a Token uses to resume a coroutine, so it can check, at the moment the work runs,
         * that the coroutine has not been destroyed while it sat in the queue.
         */
        virtual void Post(std::function<void()> work) = 0;
    };

    /**
     * @brief Awaitable that moves the rest of a coroutine onto another executor.
     *
     * Awaiting one suspends where it is and resumes on the executor it names, so a coroutine can
     * hand its expensive part to a background thread and come back for the part that must run on
     * the main one:
     *
     * @code
     * kor::Task<void> LoadAsync() {
     *     co_await kor::Context::SwitchToBackgroundThread();
     *     auto bytes = readFromDisk();                  // off the frame loop
     *     co_await kor::Context::SwitchToMainThread();
     *     upload(bytes);                                // back where the device is driven
     * }
     * @endcode
     */
    struct SwitchAwaiter {
        virtual ~SwitchAwaiter() = default;

        /** @param executor Where the awaiting coroutine should resume. */
        explicit SwitchAwaiter(Executor* executor) noexcept : exec(executor) {}
        Executor* exec; ///< The executor to resume on.
        virtual bool await_ready() const noexcept { return false; }
        virtual void await_suspend(const std::coroutine_handle<> h) const noexcept { exec->Enqueue(h); }
        virtual void await_resume() const noexcept {}
    };


    /**
     * @brief The return type of an asynchronous operation: a coroutine that may suspend and resume.
     * @tparam T What it produces, or void for one that only performs work.
     *
     * Writing a function that returns a Task makes it a coroutine — it may `co_await` other tasks,
     * tokens and executor switches, and it starts running immediately when called, up to its first
     * suspension.
     *
     * @code
     * kor::Task<int> CountThings() {
     *     co_await kor::Context::SwitchToBackgroundThread();
     *     co_return expensiveCount();
     * }
     *
     * kor::Task<void> Use() {
     *     const int n = co_await CountThings();   // suspends here until it finishes
     * }
     * @endcode
     *
     * Every task carries a completion Token, signalled when its coroutine finishes. Awaiting a task
     * is awaiting that token, so it follows the token's rule: the awaiting coroutine resumes where
     * it suspended — on the main thread if it was there, on the background pool otherwise — not on
     * whichever thread the task happened to finish on.
     *
     * A task owns its coroutine and destroys it when it goes out of scope, so it is move-only and
     * must outlive the work it represents. From ordinary code, Wait() for it (or poll Done()) and
     * then Take() the result.
     */
    template <typename T>
    class Task;

    /**
     * @brief What a cancelled task throws at its next suspension point: every `co_await` in it after
     *        Task::Cancel(), and the one it is waiting at when that is a token's, or another task's.
     *
     * An ordinary exception, so a task stops by unwinding — its destructors run, a `catch` may tidy up and
     * rethrow. Task::IsCancelled() tells it from a failure; Take() reports it as "cancelled".
     */
    struct Cancelled : std::exception {
        [[nodiscard]] const char* what() const noexcept override { return "cancelled"; }
    };

    /**
     * @brief A point in a long stretch of a task's own work, with nothing to wait for, where it may be stopped:
     *        `co_await kor::CancellationPoint{};` throws kor::Cancelled when the task has been cancelled, and does
     *        nothing otherwise — it never suspends.
     */
    struct CancellationPoint {
        bool await_ready() const noexcept { return true; }
        void await_suspend(std::coroutine_handle<>) const noexcept {}
        void await_resume() const noexcept {}
    };

    namespace detail {
        /**
         * Whether a task was asked to stop, and how to wake it where it waits: interrupting the token it waits
         * on, or asking the task it awaits to stop too (whose stopping then wakes this one).
         */
        struct CancelState {
            std::atomic<bool> requested{false};
            std::mutex mutex;
            std::function<void()> wake;     // set while it waits somewhere it can be woken from

            void Cancel() {
                requested.store(true, std::memory_order_release);
                std::function<void()> now;
                {
                    std::lock_guard lock(mutex);
                    now = std::move(wake);
                    wake = nullptr;
                }
                // Outside the lock: waking may resume the task inline, which then takes the lock to resume.
                if (now) now();
            }
            [[nodiscard]] bool IsRequested() const noexcept { return requested.load(std::memory_order_acquire); }
        };

        template <typename A>
        concept HasMemberCoAwait = requires(A&& a) { std::forward<A>(a).operator co_await(); };

        // What a task's `co_await x` waits through: x's awaiter, with the cancellation check around it.
        template <typename Inner>
        struct CancellableAwaiter {
            CancelState* state;
            Inner inner;

            template <typename Make>
            CancellableAwaiter(CancelState* s, Make&& make) : state(s), inner(std::forward<Make>(make)()) {}

            bool await_ready() {
                // Cancelled already: no waiting, straight to the throw.
                if (state->IsRequested()) return true;
                return inner.await_ready();
            }

            template <typename Handle>
            auto await_suspend(Handle h) {
                // Under the lock, so Cancel() cannot run between the awaiter parking and its waking being known
                // — and a resume that races it waits at await_resume for the lock, keeping this frame alive.
                std::unique_lock lock(state->mutex);
                using Result = decltype(inner.await_suspend(h));
                if constexpr (std::is_void_v<Result>) {
                    inner.await_suspend(h);
                    remember();
                } else {
                    auto parked = inner.await_suspend(h);
                    if constexpr (std::is_same_v<Result, bool>) { if (parked) remember(); }
                    return parked;
                }
            }

            decltype(auto) await_resume() {
                {
                    std::lock_guard lock(state->mutex);
                    state->wake = nullptr;
                }
                if (state->IsRequested()) throw Cancelled{};
                return inner.await_resume();
            }

        private:
            void remember() {
                using Plain = std::remove_cvref_t<Inner>;
                if constexpr (std::is_same_v<Plain, Token::Awaiter>) {
                    state->wake = inner.Interrupter();
                } else if constexpr (requires { inner.Cancellation(); }) {
                    // Another task: asked to stop too, and its stopping wakes this one.
                    state->wake = [child = inner.Cancellation()] { if (child) child->Cancel(); };
                }
            }
        };

        struct TaskPromiseBase {
            std::exception_ptr exception;
            Token completion = Token::Create();
            std::shared_ptr<CancelState> cancel = std::make_shared<CancelState>();

            /** Every `co_await` in a task goes through here: checked for cancellation, and wakeable by it. */
            template <typename A>
            auto await_transform(A&& awaitable) {
                if constexpr (HasMemberCoAwait<A>) {
                    using Inner = decltype(std::forward<A>(awaitable).operator co_await());
                    return CancellableAwaiter<Inner>(cancel.get(), [&]() -> Inner { return std::forward<A>(awaitable).operator co_await(); });
                } else {
                    // An awaiter itself: held by reference — a temporary one lives to the end of the co_await's full
                    // expression, past the suspension, and some cannot be moved.
                    using Inner = A&&;
                    return CancellableAwaiter<Inner>(cancel.get(), [&]() -> Inner { return std::forward<A>(awaitable); });
                }
            }

            std::suspend_never initial_suspend() noexcept { return {}; }

            // Suspends for good at the end, so the Task can still read the result, and signals the
            // completion token. The coroutine counts as suspended once await_suspend is entered, so
            // an awaiter resumed inline by the signal may destroy this frame at once — which is
            // why the token is copied out before it is signalled and nothing is touched after.
            struct FinalAwaiter {
                bool await_ready() const noexcept { return false; }
                template <typename Promise>
                void await_suspend(const std::coroutine_handle<Promise> h) const noexcept {
                    const Token done = h.promise().completion;
                    done.Signal();
                }
                void await_resume() const noexcept {}
            };
            FinalAwaiter final_suspend() noexcept { return {}; }

            void unhandled_exception() noexcept { exception = std::current_exception(); }
        };

        inline std::string Describe(const std::exception_ptr& exception) {
            try {
                std::rethrow_exception(exception);
            } catch (const std::exception& e) {
                return e.what();
            } catch (...) {
                return "Unknown exception";
            }
        }

        // What Task<void> and Task<T> share: ownership of the coroutine, and its completion.
        template <typename Promise>
        class TaskBase {
        public:
            TaskBase() noexcept = default;
            explicit TaskBase(std::coroutine_handle<Promise> h) noexcept : _handle(h) {}

            TaskBase(TaskBase&& other) noexcept : _handle(std::exchange(other._handle, {})) {}
            TaskBase& operator=(TaskBase&& other) noexcept {
                if (this != &other) {
                    if (_handle) _handle.destroy();
                    _handle = std::exchange(other._handle, {});
                }
                return *this;
            }
            TaskBase(const TaskBase&) = delete;
            TaskBase& operator=(const TaskBase&) = delete;

            ~TaskBase() {
                if (_handle) _handle.destroy();
            }

            /** @brief Whether the coroutine has run to completion (or was never started). */
            [[nodiscard]] bool Done() const noexcept {
                return !_handle || _handle.done();
            }

            /** @brief A token signalled once the coroutine has finished. Always ready for an empty task. */
            [[nodiscard]] Token Completion() const noexcept {
                return _handle ? _handle.promise().completion : Token{};
            }

            /**
             * @brief Blocks the calling thread until the coroutine has finished.
             * @warning Never from the main thread for a task that needs the main thread to finish:
             *          it resumes there on the next drain, which is what this is blocking.
             */
            void Wait() const { Completion().Wait(); }

            /** @brief What the coroutine threw, once it has finished; null if it has not, or threw nothing. */
            [[nodiscard]] std::exception_ptr Exception() const noexcept {
                return _handle && _handle.done() ? _handle.promise().exception : nullptr;
            }

            /**
             * @brief Asks the task to stop: it throws kor::Cancelled at its next suspension point — at once, when it
             *        is waiting on a token or on another task (which is asked to stop too). Cooperative: work between
             *        suspensions runs on to the next one, or to a `co_await kor::CancellationPoint{}`. Wait() for
             *        it to have stopped before letting the task go. A no-op once it has finished.
             */
            void Cancel() const {
                if (_handle && !_handle.done()) _handle.promise().cancel->Cancel();
            }

            /** @brief Whether it has finished by being cancelled: by kor::Cancelled escaping it. */
            [[nodiscard]] bool IsCancelled() const noexcept {
                const auto exception = Exception();
                if (!exception) return false;
                try { std::rethrow_exception(exception); }
                catch (const Cancelled&) { return true; }
                catch (...) { return false; }
            }

            /** @brief The task's cancellation, for a task awaiting it to pass its own on. */
            [[nodiscard]] std::shared_ptr<CancelState> CancelStateOf() const noexcept {
                return _handle ? _handle.promise().cancel : nullptr;
            }

        protected:
            std::coroutine_handle<Promise> _handle{};
        };
    }

    namespace detail {
        struct VoidPromise : TaskPromiseBase {
            Task<void> get_return_object() noexcept;
            void return_void() noexcept {}
        };

        template <typename T>
        struct ValuePromise : TaskPromiseBase {
            std::optional<T> value;

            Task<T> get_return_object() noexcept;

            template <typename U>
            void return_value(U&& v) noexcept(std::is_nothrow_constructible_v<T, U&&>) {
                value.emplace(std::forward<U>(v));
            }
        };
    }

    /** @brief A task that performs work but produces no value. */
    template <>
    class Task<void> : public detail::TaskBase<detail::VoidPromise> {
    public:
        using promise_type = detail::VoidPromise;

        Task() noexcept = default;
        explicit Task(std::coroutine_handle<promise_type> h) noexcept : TaskBase(h) {}

        /**
         * @brief Collects the outcome and releases the coroutine.
         * @return An empty result on success; an error string if the task never started, has not
         *         finished yet, or threw. An exception that escaped the coroutine is caught and
         *         reported here rather than propagating.
         * @note Consumes the task: calling it twice reports that there is nothing to take.
         */
        std::expected<void, std::string> Take() {
            if (!_handle) return std::unexpected("No task to take from");
            if (!_handle.done()) return std::unexpected("Task is not completed yet");
            const auto exception = _handle.promise().exception;
            _handle.destroy();
            _handle = {};
            if (exception) return std::unexpected(detail::Describe(exception));
            return {};
        }

        /**
         * @brief Suspends the awaiting coroutine until this task finishes.
         *
         * An exception that escaped the awaited task is rethrown here, in the awaiting coroutine.
         */
        struct Awaiter {
            std::coroutine_handle<promise_type> handle;
            Token::Awaiter finished;

            bool await_ready() const noexcept { return finished.await_ready(); }
            bool await_suspend(const std::coroutine_handle<> awaiting) { return finished.await_suspend(awaiting); }
            void await_resume() const {
                if (handle && handle.promise().exception) std::rethrow_exception(handle.promise().exception);
            }
            /** For a cancelled task awaiting this one: this one is asked to stop too. */
            [[nodiscard]] std::shared_ptr<detail::CancelState> Cancellation() const { return handle ? handle.promise().cancel : nullptr; }
        };

        Awaiter operator co_await() noexcept { return Awaiter{_handle, Token::Awaiter(Completion())}; }
    };

    template <typename T>
    class Task : public detail::TaskBase<detail::ValuePromise<T>> {
        using Base = detail::TaskBase<detail::ValuePromise<T>>;
    public:
        using promise_type = detail::ValuePromise<T>;

        Task() noexcept = default;
        explicit Task(std::coroutine_handle<promise_type> h) noexcept : Base(h) {}

        /**
         * @brief Collects the value and releases the coroutine.
         * @return The produced value; or an error string if the task never started, has not
         *         finished yet, threw, or completed without returning anything. An exception that
         *         escaped the coroutine is caught and reported here rather than propagating.
         * @note Consumes the task: calling it twice reports that there is nothing to take.
         */
        std::expected<T, std::string> Take() {
            auto& _handle = this->_handle;
            if (!_handle) return std::unexpected("No task to take from");
            if (!_handle.done()) return std::unexpected("Task is not completed yet");
            auto& p = _handle.promise();
            std::expected<T, std::string> out = p.exception
                ? std::unexpected(detail::Describe(p.exception))
                : p.value.has_value() ? std::expected<T, std::string>(std::move(*p.value))
                                      : std::unexpected(std::string("Task completed without returning a value"));
            _handle.destroy();
            _handle = {};
            return out;
        }

        /**
         * @brief Suspends the awaiting coroutine until this task finishes, then yields its value.
         *
         * An exception that escaped the awaited task is rethrown here, in the awaiting coroutine.
         */
        struct Awaiter {
            promise_type* promise;
            Token::Awaiter finished;

            bool await_ready() const noexcept { return finished.await_ready(); }
            bool await_suspend(const std::coroutine_handle<> awaiting) { return finished.await_suspend(awaiting); }
            T await_resume() const {
                if (promise->exception) std::rethrow_exception(promise->exception);
                return std::move(*promise->value);
            }
            /** For a cancelled task awaiting this one: this one is asked to stop too. */
            [[nodiscard]] std::shared_ptr<detail::CancelState> Cancellation() const { return promise ? promise->cancel : nullptr; }
        };

        /** @brief Makes the task awaitable, so `co_await task` yields its value. */
        Awaiter operator co_await() noexcept {
            return Awaiter{&this->_handle.promise(), Token::Awaiter(this->Completion())};
        }
    };

    inline Task<void> detail::VoidPromise::get_return_object() noexcept {
        return Task<void>{std::coroutine_handle<VoidPromise>::from_promise(*this)};
    }

    template <typename T>
    Task<T> detail::ValuePromise<T>::get_return_object() noexcept {
        return Task<T>{std::coroutine_handle<ValuePromise>::from_promise(*this)};
    }

    /**
     * @brief Suspends until every one of @p tasks has finished.
     *
     * The tasks are already running — a task starts when it is called — so this only waits; they
     * make progress side by side, wherever each has taken itself. If any threw, the first
     * exception is rethrown once all have finished, so none is left running unobserved.
     *
     * Safe to Wait() on from the main thread even when the tasks run on the background pool: its
     * own bookkeeping never goes back through the main thread's queue.
     *
     * @code
     * std::vector<kor::Task<void>> passes;
     * for (auto& pass : graph) passes.push_back(RecordOnBackground(pass));
     * co_await kor::WhenAll(std::move(passes));   // or .Wait() from ordinary code
     * @endcode
     */
    [[nodiscard]] KORAL_API Task<void> WhenAll(std::vector<Task<void>> tasks);

    /** @brief Suspends until every one of @p tokens has happened. As WhenAll over tasks. */
    [[nodiscard]] KORAL_API Task<void> WhenAll(std::vector<Token> tokens);
}