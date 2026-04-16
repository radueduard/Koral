//
// Created by radue on 4/15/2026.
//

#pragma once
#include <coroutine>
#include <mutex>
#include <queue>
#include <thread>

#include "Task.h"

class MainThreadExecutor : public gfx::Executor {
public:
    MainThreadExecutor() : mainThreadId_(std::this_thread::get_id()) {}

    bool IsMainThread() const noexcept override {
        return std::this_thread::get_id() == mainThreadId_;
    }

    void Enqueue(std::coroutine_handle<> h) override {
        std::scoped_lock lock(mutex_);
        queue_.push(h);
    }

    // Call once per frame from ParallelTextureLoading::Update()
    void Drain() {
        std::queue<std::coroutine_handle<>> local;
        {
            std::scoped_lock lock(mutex_);
            std::swap(local, queue_);
        }
        while (!local.empty()) {
            auto h = local.front();
            local.pop();
            if (h) h.resume();
        }
    }

    struct SwitchAwaiter : gfx::SwitchAwaiter {
        explicit SwitchAwaiter(MainThreadExecutor* executor) noexcept : gfx::SwitchAwaiter(executor) {}
        bool await_ready() const noexcept override { return exec->IsMainThread(); }
        void await_resume() const noexcept override {}
    };

    gfx::SwitchAwaiter SwitchToMainThread() noexcept { return static_cast<gfx::SwitchAwaiter>(SwitchAwaiter(this)); }

private:
    std::thread::id mainThreadId_;
    std::mutex mutex_;
    std::queue<std::coroutine_handle<>> queue_;
};
