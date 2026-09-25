//
// Created by radue on 9/23/2026.
//

#include "tokenState.h"
#include "task.h"
#include "scene.h"

#include <algorithm>

#include "context.h"
#include "log.h"
#include "../executor/BackgroundExecutor.h"
#include "../executor/MainThreadExecutor.h"

namespace kor::detail {
    // The main thread if that is where the coroutine is, the background pool otherwise. With no
    // context there are no executors, and the signalling thread resumes it directly.
    Executor* TimelineState::resumeExecutor() noexcept {
        auto* main = Context::_mainThreadExecutor;
        if (main && main->IsMainThread()) return main;
        if (Context::_backgroundExecutor) return Context::_backgroundExecutor;
        return main;
    }

    std::uint64_t TimelineState::current() {
        std::lock_guard lock(mutex);
        const std::uint64_t cpu = reached.load(std::memory_order_relaxed);
        return gpu ? std::max(cpu, gpu->counter()) : cpu;
    }

    bool TimelineState::isReached(const std::uint64_t value) {
        if (reached.load(std::memory_order_acquire) >= value) return true;
        std::lock_guard lock(mutex);
        return gpu && gpu->counter() >= value;
    }

    TimelineState::Waiter TimelineState::suspend(const std::uint64_t value, const std::coroutine_handle<> handle,
                                                 const bool resumeInline) {
        std::lock_guard lock(mutex);
        // Re-checked under the lock: a signal between await_ready and here would otherwise leave
        // this coroutine parked on a value nobody is going to reach again.
        if (reached.load(std::memory_order_acquire) >= value) return nullptr;
        if (gpu && gpu->counter() >= value) return nullptr;
        auto waiter = std::make_shared<WaiterSlot>(value, handle, resumeInline ? nullptr : resumeExecutor(), CurrentSceneLife());
        waiters.push_back(waiter);
        // The GPU tells nobody when it gets there; the backend has to be looking.
        if (gpu) gpu->watch();
        return waiter;
    }

    void TimelineState::cancel(const Waiter& waiter) noexcept {
        // Lost the race to a resume: the coroutine is running, or has run, and was not destroyed
        // mid-wait after all.
        if (!waiter->claim(WaiterSlot::eCancelled)) return;
        std::lock_guard lock(mutex);
        std::erase(waiters, waiter);
    }

    void TimelineState::wait(const std::uint64_t value) {
        std::unique_lock lock(mutex);
        for (;;) {
            if (reached.load(std::memory_order_acquire) >= value) return;
            if (gpu) {
                // Wait on the GPU counter itself: a CPU signal moves it too, so this covers both.
                GpuTimeline* g = gpu.get();
                lock.unlock();
                g->wait(value);
                observe(g->counter());
                return;
            }
            // Woken by a CPU signal, or by promote() — after which the branch above takes over,
            // since a GPU signal never comes through here.
            reachedChanged.wait(lock);
        }
    }

    std::vector<TimelineState::Waiter> TimelineState::advance(const std::uint64_t value) {
        reached.store(value, std::memory_order_release);
        // Stable partition keeps the rest in registration order; waiters are few, so a vector
        // beats an ordered container here.
        const auto split = std::stable_partition(waiters.begin(), waiters.end(),
            [value](const Waiter& w) { return w->value > value; });
        std::vector<Waiter> due(split, waiters.end());
        waiters.erase(split, waiters.end());
        return due;
    }

    void TimelineState::dispatch(const std::vector<Waiter>& due) {
        reachedChanged.notify_all();
        // Outside the lock: an inline resume may run arbitrary code, including signalling this
        // same timeline again.
        for (const auto& w : due) {
            // Claimed only when the resume actually runs: until then the coroutine may still be
            // destroyed, and its awaiter's Cancel() must be able to win.
            if (w->executor) {
                w->executor->Post([w] {
                    if (!w->claim(WaiterSlot::eResumed)) return;
                    SceneScope scope(w->scene);
                    w->handle.resume();
                });
            } else if (w->claim(WaiterSlot::eResumed)) {
                SceneScope scope(w->scene);
                w->handle.resume();
            }
        }
    }

    void TimelineState::reach(const std::uint64_t value) {
        std::vector<Waiter> due;
        {
            std::lock_guard lock(mutex);
            std::uint64_t now = reached.load(std::memory_order_relaxed);
            if (gpu) now = std::max(now, gpu->counter());
            if (value <= now) {
                log::Warn("Token {} signalled, but its timeline had already reached {}; "
                          "is more than one producer signalling the same timeline?", value, now);
                return;
            }
            if (gpu) gpu->signal(value);
            due = advance(value);
        }
        dispatch(due);
    }

    void TimelineState::observe(const std::uint64_t value) {
        std::vector<Waiter> due;
        {
            std::lock_guard lock(mutex);
            if (value <= reached.load(std::memory_order_relaxed)) return;
            due = advance(value);
        }
        dispatch(due);
    }

    bool TimelineState::promote(const std::function<std::unique_ptr<GpuTimeline>(std::uint64_t)>& make) {
        {
            std::lock_guard lock(mutex);
            if (gpu) return false;
            gpu = make(reached.load(std::memory_order_relaxed));
        }
        // A thread blocked in Wait() is parked on the condition variable, which only CPU signals
        // wake; send it round again so it moves over to waiting on the GPU counter.
        reachedChanged.notify_all();
        return true;
    }

    void TimelineState::demote() {
        std::vector<Waiter> due;
        {
            std::lock_guard lock(mutex);
            if (!gpu) return;
            const std::uint64_t last = gpu->counter();
            gpu.reset();
            if (last > reached.load(std::memory_order_relaxed)) due = advance(last);
        }
        dispatch(due);
    }

    std::optional<std::uint64_t> TimelineState::lowestAwaited() {
        std::lock_guard lock(mutex);
        std::optional<std::uint64_t> lowest;
        for (const auto& w : waiters)
            if (w->state.load(std::memory_order_acquire) == WaiterSlot::eWaiting && (!lowest || w->value < *lowest))
                lowest = w->value;
        return lowest;
    }
}

namespace kor::detail {
    namespace {
        struct Retired {
            std::vector<Token> tokens;
            std::shared_ptr<void> owned;

            [[nodiscard]] bool ready() const {
                return std::ranges::all_of(tokens, [](const Token& t) { return t.Ready(); });
            }
            void wait() const {
                for (const auto& t : tokens) t.Wait();
            }
        };

        std::mutex retiredMutex;
        std::vector<Retired> retired;
    }

    void retireAfter(const Token& token, std::shared_ptr<void> owned) {
        retireAfter(std::vector{token}, std::move(owned));
    }

    void retireAfter(std::vector<Token> tokens, std::shared_ptr<void> owned) {
        Retired entry{std::move(tokens), std::move(owned)};
        if (entry.ready()) return; // already done with: `owned` is released on the way out
        std::lock_guard lock(retiredMutex);
        retired.push_back(std::move(entry));
    }

    void collectRetired(const bool all) {
        std::vector<Retired> released;
        {
            std::lock_guard lock(retiredMutex);
            if (all) {
                released.swap(retired);
            } else {
                const auto split = std::stable_partition(retired.begin(), retired.end(),
                    [](const Retired& r) { return !r.ready(); });
                released.assign(std::make_move_iterator(split), std::make_move_iterator(retired.end()));
                retired.erase(split, retired.end());
            }
        }
        // Outside the lock: destroying a command buffer may retire nothing, but it runs arbitrary
        // destructors — the resources its records held — and one of those could get here again.
        for (const auto& r : released) r.wait();
        released.clear();
    }
}

namespace kor::detail {
    bool InlineAwaiter::await_suspend(const std::coroutine_handle<> h) {
        const auto& state = TokenAccess::state(token);
        if (!state) return false;
        slot = state->suspend(token.Value(), h, /*resumeInline=*/true);
        return slot != nullptr;
    }

    InlineAwaiter::~InlineAwaiter() {
        if (slot) if (const auto& state = TokenAccess::state(token)) state->cancel(slot);
    }
}

kor::Task<void> kor::WhenAll(std::vector<Task<void>> tasks) {
    for (const auto& task : tasks) co_await detail::InlineAwaiter(task.Completion());
    std::exception_ptr first;
    for (const auto& task : tasks)
        if (!first) first = task.Exception();
    if (first) std::rethrow_exception(first);
}

kor::Task<void> kor::WhenAll(std::vector<Token> tokens) {
    for (const auto& token : tokens) co_await detail::InlineAwaiter(token);
}

kor::Timeline::Timeline() : _state(std::make_shared<detail::TimelineState>()) {}

kor::Token kor::Timeline::Next() {
    return Token(_state, _state->reserved.fetch_add(1, std::memory_order_relaxed) + 1);
}

kor::Token kor::Timeline::At(const std::uint64_t value) const {
    return Token(_state, value);
}

std::uint64_t kor::Timeline::Value() const noexcept {
    return _state->current();
}

kor::Token kor::Token::Create() {
    return Timeline().Next();
}

bool kor::Token::Ready() const noexcept {
    return !_state || _state->isReached(_value);
}

void kor::Token::Wait() const {
    if (Ready()) return;
    _state->wait(_value);
}

void kor::Token::Signal() const {
    if (_state) _state->reach(_value);
}

std::shared_ptr<kor::detail::WaiterSlot> kor::Token::Suspend(const std::coroutine_handle<> h) const {
    return _state ? _state->suspend(_value, h) : nullptr;
}

void kor::Token::Cancel(const std::shared_ptr<detail::WaiterSlot>& slot) const noexcept {
    if (_state) _state->cancel(slot);
}
