//
// Created by radue on 4/15/2026.
//

#pragma once
#include <coroutine>
#include <thread>

#include <asio/thread_pool.hpp>
#include <asio/post.hpp>

#include "task.h"

class BackgroundExecutor : public kor::Executor {
public:
    explicit BackgroundExecutor(size_t threadCount = std::thread::hardware_concurrency())
        : pool_(threadCount == 0 ? 1 : threadCount) {}

    ~BackgroundExecutor() override { Shutdown(); }

    void Enqueue(std::coroutine_handle<> h) override {
        asio::post(pool_, [h]() mutable { h.resume(); });
    }

    void Shutdown() {
        pool_.join();   // waits for all pending work and stops worker threads
    }

    struct SwitchAwaiter : kor::SwitchAwaiter {
        explicit SwitchAwaiter(BackgroundExecutor* executor) : kor::SwitchAwaiter(executor) {}
        bool await_ready()  const noexcept override { return false; }
        void await_resume() const noexcept override {}
    };

    kor::SwitchAwaiter SwitchToBackground() noexcept {
        return static_cast<kor::SwitchAwaiter>(SwitchAwaiter(this));
    }

private:
    asio::thread_pool pool_;
};
