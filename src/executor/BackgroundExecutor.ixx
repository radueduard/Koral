//
// Created by radue on 4/15/2026.
//

module;

#include <coroutine>
#include <mutex>
#include <thread>
#include <queue>
#include <vector>

export module gfx.backgroundExecutor;
import gfx.task;

export class BackgroundExecutor : public gfx::Executor {
public:
    explicit BackgroundExecutor(size_t threadCount = std::thread::hardware_concurrency()) {
        if (threadCount == 0) threadCount = 1;
        for (size_t i = 0; i < threadCount; ++i) {
            workers_.emplace_back([this] {
                WorkerLoop();
            });
        }
    }

    ~BackgroundExecutor() override { Shutdown(); }

    void Enqueue(const std::coroutine_handle<> h) override {
        {
            std::scoped_lock lock(mutex_);
            queue_.push(h);
        }
        cv_.notify_one();
    }

    void Shutdown() {
        if (bool expected = false; !stopping_.compare_exchange_strong(expected, true)) return;
        cv_.notify_all();
        for (auto& t : workers_) if (t.joinable()) t.join();
    }

    struct SwitchAwaiter : gfx::SwitchAwaiter {
        explicit SwitchAwaiter(BackgroundExecutor *executor) : gfx::SwitchAwaiter(executor) {}
        bool await_ready() const noexcept override { return false; }
        void await_resume() const noexcept override {}
    };

    gfx::SwitchAwaiter SwitchToBackground() noexcept { return static_cast<gfx::SwitchAwaiter>(SwitchAwaiter(this)); }

private:
    void WorkerLoop() {
        while (true) {
            std::coroutine_handle<> h;
            {
                std::unique_lock lock(mutex_);
                cv_.wait(lock, [this] { return stopping_.load() || !queue_.empty(); });
                if (stopping_.load() && queue_.empty()) return;
                h = queue_.front();
                queue_.pop();
            }
            if (h) h.resume();
        }
    }

    std::vector<std::thread> workers_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<std::coroutine_handle<>> queue_;
    std::atomic<bool> stopping_{false};
};
