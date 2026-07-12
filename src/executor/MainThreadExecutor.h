//
// Created by radue on 4/15/2026.
//

#pragma once
#include <coroutine>
#include <thread>

#include <asio/io_context.hpp>
#include <asio/post.hpp>

#include "task.h"

class MainThreadExecutor : public kor::Executor {
public:
    MainThreadExecutor() : mainThreadId_(std::this_thread::get_id()) {}

    bool IsMainThread() const noexcept override {
        return std::this_thread::get_id() == mainThreadId_;
    }

    void Enqueue(std::coroutine_handle<> h) override {
        asio::post(ctx_, [h]() mutable { h.resume(); });
    }

    // Call once per frame from the main loop (replaces the manual queue drain).
    void Drain() {
        ctx_.restart();

        const auto budget = std::chrono::milliseconds(1); // 1 ms budget for processing tasks
        const auto deadline = std::chrono::steady_clock::now() + budget;

        // poll_one() runs at most one ready handler and returns 0 if none ran.
        while (ctx_.poll_one() != 0) {
            if (std::chrono::steady_clock::now() >= deadline) {
                break;
            }
        }
    }

    struct SwitchAwaiter : kor::SwitchAwaiter {
        explicit SwitchAwaiter(MainThreadExecutor* executor) noexcept : kor::SwitchAwaiter(executor) {}
        bool await_ready()  const noexcept override { return exec->IsMainThread(); }
        void await_resume() const noexcept override {}
    };

    kor::SwitchAwaiter SwitchToMainThread() noexcept {
        return static_cast<kor::SwitchAwaiter>(SwitchAwaiter(this));
    }

private:
    std::thread::id   mainThreadId_;
    asio::io_context  ctx_;
};
