//
// Created by radue on 4/15/2026.
//

#pragma once

#include <coroutine>
#include <exception>
#include <expected>
#include <optional>
#include <utility>

namespace gfx {
    struct Executor {
        virtual ~Executor() = default;
        virtual bool IsMainThread() const noexcept { return false; }
        virtual void Enqueue(std::coroutine_handle<>) = 0;
    };

    struct SwitchAwaiter {
        virtual ~SwitchAwaiter() = default;

        explicit SwitchAwaiter(Executor* executor) noexcept : exec(executor) {}
        Executor* exec;
        virtual bool await_ready() const noexcept { return false; }
        virtual void await_suspend(const std::coroutine_handle<> h) const noexcept { exec->Enqueue(h); }
        virtual void await_resume() const noexcept {}
    };


    template <typename T>
    class Task;

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
        explicit Task(std::coroutine_handle<promise_type> h) noexcept : handle_(h) {}

        Task(Task&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}
        Task& operator=(Task&& other) noexcept {
            if (this != &other) {
                if (handle_) handle_.destroy();
                handle_ = std::exchange(other.handle_, {});
            }
            return *this;
        }

        Task(const Task&) = delete;
        Task& operator=(const Task&) = delete;

        ~Task() {
            if (handle_) handle_.destroy();
        }

        [[nodiscard]] bool done() const noexcept {
            return !handle_ || handle_.done();
        }

        std::expected<void, std::string> take() {
            if (!handle_) return std::unexpected("No task to take from");
            if (!handle_.done()) return std::unexpected("Task is not completed yet");

            auto& p = handle_.promise();
            if (p.exception) {
                try {
                    std::rethrow_exception(p.exception);
                } catch (const std::exception& e) {
                    return std::unexpected(e.what());
                } catch (...) {
                    return std::unexpected("Unknown exception");
                }
            }

            handle_.destroy();
            handle_ = {};
            return {}; // success
        }

    private:
        std::coroutine_handle<promise_type> handle_{};
    };

    template <typename T>
    class Task {
    public:
        struct promise_type {
            std::optional<T> value;
            std::exception_ptr exception;

            Task get_return_object() noexcept {
                return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
            }

            std::suspend_never initial_suspend() noexcept { return {}; }
            std::suspend_always final_suspend() noexcept { return {}; }

            void unhandled_exception() noexcept { exception = std::current_exception(); }

            template <typename U>
            void return_value(U&& v) noexcept(std::is_nothrow_constructible_v<T, U&&>) {
                value.emplace(std::forward<U>(v));
            }
        };

        Task() noexcept = default;
        explicit Task(std::coroutine_handle<promise_type> h) noexcept : handle_(h) {}

        Task(Task&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}
        Task& operator=(Task&& other) noexcept {
            if (this != &other) {
                if (handle_) handle_.destroy();
                handle_ = std::exchange(other.handle_, {});
            }
            return *this;
        }

        Task(const Task&) = delete;
        Task& operator=(const Task&) = delete;

        ~Task() {
            if (handle_) handle_.destroy();
        }

        [[nodiscard]] bool done() const noexcept {
            return !handle_ || handle_.done();
        }

        std::expected<T, std::string> take() {
            if (!handle_) return std::unexpected("No task to take from");
            if (!handle_.done()) return std::unexpected("Task is not completed yet");

            auto& p = handle_.promise();
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
            handle_.destroy();
            handle_ = {};
            return out;
        }

    private:
        std::coroutine_handle<promise_type> handle_{};
    };
}