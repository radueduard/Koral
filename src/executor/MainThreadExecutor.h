//
// Created by radue on 4/15/2026.
//

#pragma once
#include <coroutine>
#include <functional>
#include <thread>

#include <asio/io_context.hpp>
#include <asio/post.hpp>

#include "task.h"
#include "scene.h"

class MainThreadExecutor : public kor::Executor {
public:
    MainThreadExecutor() : mainThreadId_(std::this_thread::get_id()) {}

    bool IsMainThread() const noexcept override {
        return std::this_thread::get_id() == mainThreadId_;
    }

    // The scene current where the coroutine suspended is current again where it resumes, so
    // `Window::` and its kind keep meaning its scene across a thread switch.
    void Enqueue(std::coroutine_handle<> h) override {
        asio::post(ctx_, [h, scene = kor::detail::CurrentSceneLife()]() mutable {
            kor::detail::SceneScope scope(scene);
            h.resume();
        });
    }

    void Post(std::function<void()> work) override {
        asio::post(ctx_, std::move(work));
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
