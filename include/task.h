//
// Created by radue on 4/15/2026.
//

#pragma once

#include <coroutine>
#include <exception>
#include <expected>
#include <optional>
#include <utility>
#include <string>

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
        virtual bool isMainThread() const noexcept { return false; }

        /** @brief Schedules a suspended coroutine to be resumed on this executor. */
        virtual void Enqueue(std::coroutine_handle<>) = 0;
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
     * Writing a function that returns a Task makes it a coroutine — it may `co_await` other tasks
     * and executor switches, and it starts running immediately when called, up to its first
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
     * A task owns its coroutine and destroys it when it goes out of scope, so it is move-only and
     * must outlive the work it represents. From non-coroutine code — the run loop, for instance —
     * poll done() and then take() the result.
     */
    template <typename T>
    class Task;

    /**
     * @brief A task that performs work but produces no value.
     *
     * @note Unlike Task<T>, this cannot be `co_await`ed. Drive it from ordinary code with done()
     *       and take(), which is how the engine runs a headless Job::Run to completion.
     */
    template <>
    class Task<void> {
    public:
        struct promise_type {
            std::exception_ptr exception;

            Task get_return_object() noexcept {
                return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
            }

            std::suspend_never initial_suspend() noexcept { return {}; }
            std::suspend_always final_suspend() noexcept { return {}; }

            void unhandled_exception() noexcept { exception = std::current_exception(); }
            void return_void() noexcept {}
        };

        Task() noexcept = default;
        explicit Task(std::coroutine_handle<promise_type> h) noexcept : _handle(h) {}

        Task(Task&& other) noexcept : _handle(std::exchange(other._handle, {})) {}
        Task& operator=(Task&& other) noexcept {
            if (this != &other) {
                if (_handle) _handle.destroy();
                _handle = std::exchange(other._handle, {});
            }
            return *this;
        }

        Task(const Task&) = delete;
        Task& operator=(const Task&) = delete;

        ~Task() {
            if (_handle) _handle.destroy();
        }

        /** @brief Whether the coroutine has run to completion (or was never started). */
        [[nodiscard]] bool done() const noexcept {
            return !_handle || _handle.done();
        }

        /**
         * @brief Collects the outcome and releases the coroutine.
         * @return An empty result on success; an error string if the task never started, has not
         *         finished yet, or threw. An exception that escaped the coroutine is caught and
         *         reported here rather than propagating.
         * @note Consumes the task: calling it twice reports that there is nothing to take.
         */
        std::expected<void, std::string> take() {
            if (!_handle) return std::unexpected("No task to take from");
            if (!_handle.done()) return std::unexpected("Task is not completed yet");

            auto& p = _handle.promise();
            if (p.exception) {
                try {
                    std::rethrow_exception(p.exception);
                } catch (const std::exception& e) {
                    return std::unexpected(e.what());
                } catch (...) {
                    return std::unexpected("Unknown exception");
                }
            }

            _handle.destroy();
            _handle = {};
            return {}; // success
        }

    private:
        std::coroutine_handle<promise_type> _handle{};
    };

    template <typename T>
    class Task {
    public:
        struct promise_type {
            std::optional<T> value;
            std::exception_ptr exception;
            std::coroutine_handle<> continuation; // who's waiting on us

            Task get_return_object() noexcept {
                return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
            }

            std::suspend_never initial_suspend() noexcept { return {}; }

            // Custom final awaiter: resume the continuation (if any) when this task finishes
            struct FinalAwaiter {
                bool await_ready() const noexcept { return false; }
                void await_suspend(std::coroutine_handle<promise_type> h) const noexcept {
                    if (auto cont = h.promise().continuation)
                        cont.resume(); // or enqueue on an executor
                }
                void await_resume() const noexcept {}
            };
            FinalAwaiter final_suspend() noexcept { return {}; }

            void unhandled_exception() noexcept { exception = std::current_exception(); }

            template <typename U>
            void return_value(U&& v) noexcept(std::is_nothrow_constructible_v<T, U&&>) {
                value.emplace(std::forward<U>(v));
            }
        };

        Task() noexcept = default;
        explicit Task(std::coroutine_handle<promise_type> h) noexcept : _handle(h) {}

        Task(Task&& other) noexcept : _handle(std::exchange(other._handle, {})) {}
        Task& operator=(Task&& other) noexcept {
            if (this != &other) {
                if (_handle) _handle.destroy();
                _handle = std::exchange(other._handle, {});
            }
            return *this;
        }

        Task(const Task&) = delete;
        Task& operator=(const Task&) = delete;

        ~Task() {
            if (_handle) _handle.destroy();
        }

        /** @brief Whether the coroutine has run to completion (or was never started). */
        [[nodiscard]] bool done() const noexcept {
            return !_handle || _handle.done();
        }

        /**
         * @brief Collects the value and releases the coroutine.
         * @return The produced value; or an error string if the task never started, has not
         *         finished yet, threw, or completed without returning anything. An exception that
         *         escaped the coroutine is caught and reported here rather than propagating.
         * @note Consumes the task: calling it twice reports that there is nothing to take.
         */
        std::expected<T, std::string> take() {
            if (!_handle) return std::unexpected("No task to take from");
            if (!_handle.done()) return std::unexpected("Task is not completed yet");

            auto& p = _handle.promise();
            if (p.exception) {
                try {
                    std::rethrow_exception(p.exception);
                } catch (const std::exception& e) {
                    return std::unexpected(e.what());
                } catch (...) {
                    return std::unexpected("Unknown exception");
                }
            }
            if (!p.value.has_value()) {
                return std::unexpected("Task completed without returning a value");
            }

            T out = std::move(*p.value);
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
            std::coroutine_handle<promise_type> handle;

            bool await_ready() const noexcept { return handle.done(); }

            void await_suspend(std::coroutine_handle<> awaiting) const noexcept {
                handle.promise().continuation = awaiting;
            }

            T await_resume() const {
                auto& p = handle.promise();
                if (p.exception) std::rethrow_exception(p.exception);
                return std::move(*p.value);
            }
        };

        /** @brief Makes the task awaitable, so `co_await task` yields its value. */
        Awaiter operator co_await() noexcept {
            return Awaiter{_handle};
        }

    private:
        std::coroutine_handle<promise_type> _handle{};
    };
}