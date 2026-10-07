//
// Created by radue on 9/23/2026.
//

#pragma once

#include <atomic>
#include <condition_variable>
#include <coroutine>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include "token.h"

namespace kor { struct Executor; }

namespace kor::detail {
    struct SceneLife;
    /**
     * The GPU half of a timeline, supplied by the backend once a token is first handed to a GPU
     * submission. From then on the backend object is the source of truth: the GPU can move the
     * counter without the CPU being told, and `reached` below only catches up when somebody looks.
     */
    struct GpuTimeline {
        virtual ~GpuTimeline() = default;

        /** The value the GPU-visible counter is at now. */
        [[nodiscard]] virtual std::uint64_t counter() const = 0;
        /** Moves the counter to `value` from the CPU. */
        virtual void signal(std::uint64_t value) = 0;
        /** Blocks until the counter reaches `value`. */
        virtual void wait(std::uint64_t value) = 0;
        /** A coroutine started waiting on this timeline: make sure somebody is watching the GPU. */
        virtual void watch() = 0;
    };

    /**
     * One coroutine waiting on a timeline. Shared between the timeline (which resumes it) and the
     * coroutine's own awaiter (which cancels it if the coroutine is destroyed first); whichever
     * claims it first wins, and the claim to resume is made at the moment of resuming, on the
     * executor — so a coroutine destroyed while its resume sat in a queue is skipped too.
     */
    struct WaiterSlot {
        enum State : int { eWaiting, eResumed, eCancelled };

        std::uint64_t value;
        std::coroutine_handle<> handle;
        Executor* executor; // null: resume inline on the signalling thread
        std::shared_ptr<SceneLife> scene;   // current where it suspended, current again where it resumes
        std::atomic<int> state{eWaiting};

        WaiterSlot(const std::uint64_t v, const std::coroutine_handle<> h, Executor* e, std::shared_ptr<SceneLife> s)
            : value(v), handle(h), executor(e), scene(std::move(s)) {}

        bool claim(const State to) noexcept {
            int expected = eWaiting;
            return state.compare_exchange_strong(expected, to, std::memory_order_acq_rel);
        }
    };

    struct TimelineState {
        using Waiter = std::shared_ptr<WaiterSlot>;

        std::atomic<std::uint64_t> reached{0};
        std::atomic<std::uint64_t> reserved{0};
        // The highest value some GPU submission already handed to a queue will signal. Vulkan lets
        // most submissions wait on a value nobody has signalled yet, but not a present: everything
        // it depends on must already be on its way, so the frame has to know which is which.
        std::atomic<std::uint64_t> submitted{0};

        std::mutex mutex;
        std::condition_variable reachedChanged;
        std::vector<Waiter> waiters;
        std::unique_ptr<GpuTimeline> gpu; // guarded by `mutex`

        /** Where a coroutine suspending on this thread comes back. Needs Context's executors. */
        static Executor* resumeExecutor() noexcept;

        /** The highest value known to be reached, looking at the GPU counter if there is one. */
        [[nodiscard]] std::uint64_t current();
        [[nodiscard]] bool isReached(std::uint64_t value);
        /**
         * Parks @p handle until @p value is reached, leaving the slot it waits in in @p slot; false,
         * with nothing parked, when the value has been reached already.
         *
         * The slot is written here, under the lock, and not returned for the caller to store: once
         * the lock is let go another thread may resume the coroutine, which may run to its end and
         * free its frame — the awaiter and its slot with it — before this even returns. Nothing of
         * the awaiter may be touched after this call.
         */
        bool suspend(std::uint64_t value, std::coroutine_handle<> handle, Waiter& slot, bool resumeInline = false);
        void cancel(const Waiter& waiter) noexcept;
        // Resumes `waiter` now, its value reached or not: a task cancelled while it waits. @see Token::Interrupt
        void interrupt(const Waiter& waiter) noexcept;
        void wait(std::uint64_t value);

        /** A CPU signal: the producer says it got to `value`. Warns if it already had. */
        void reach(std::uint64_t value);
        /** Somebody looked at the GPU counter and saw `value`. Silent if that is old news. */
        void observe(std::uint64_t value);

        /**
         * Gives the timeline its GPU half if it has none yet. `make` receives the value to start the
         * counter at. Returns whether this call was the one that created it.
         */
        bool promote(const std::function<std::unique_ptr<GpuTimeline>(std::uint64_t)>& make);
        /** Drops the GPU half, carrying its last value over to the CPU side. For device teardown. */
        void demote();

        /** The lowest value any coroutine is parked on, if any is. */
        [[nodiscard]] std::optional<std::uint64_t> lowestAwaited();

    private:
        // Moves `reached` to `value` and pulls out the waiters that makes due; caller holds `mutex`.
        std::vector<Waiter> advance(std::uint64_t value);
        void dispatch(const std::vector<Waiter>& due);
    };

    /**
     * Keeps `owned` alive until `token` has happened — a command buffer whose submission is still
     * running, and whatever its records hold on to. Released by the next collectRetired() to find
     * the token reached, on whichever thread calls it.
     */
    void retireAfter(const Token& token, std::shared_ptr<void> owned);
    /** As above, until every one of `tokens` has happened. */
    void retireAfter(std::vector<Token> tokens, std::shared_ptr<void> owned);

    /**
     * One-off GPU work the CPU did not wait for — an upload, a build — whose results later GPU work
     * reads. Every later one-off submission waits for it on the GPU, and so does the next frame, so
     * nothing reads what it has not finished writing, and the CPU never stalls for it.
     */
    KORAL_API void noteUpload(const Token& token);
    /** What noteUpload() recorded and the GPU has not finished yet: what a one-off submission waits for. */
    KORAL_API std::vector<Token> pendingUploads();

    /**
     * Releases everything retired whose token has happened. With `all`, waits for the rest first and
     * releases those too — for shutdown, while whatever they hold can still be destroyed properly.
     */
    KORAL_API void collectRetired(bool all = false); // exported: the runtime's headless path calls it

    /**
     * Awaits a token and is resumed by whichever thread signals it, instead of going back to the
     * executor it suspended on. For combinators such as WhenAll, whose own frame belongs to no
     * thread: bouncing through the main thread's queue would deadlock a main thread blocked in
     * Wait() on the combinator itself.
     */
    struct InlineAwaiter {
        Token token;
        std::shared_ptr<WaiterSlot> slot;

        explicit InlineAwaiter(Token t) : token(std::move(t)) {}
        InlineAwaiter(const InlineAwaiter&) = delete;
        InlineAwaiter& operator=(const InlineAwaiter&) = delete;
        ~InlineAwaiter();

        bool await_ready() const noexcept { return token.Ready(); }
        bool await_suspend(std::coroutine_handle<> h);
        void await_resume() const noexcept {}
    };

    /** How the backends get at a token's state without it being part of the public API. */
    struct TokenAccess {
        static const std::shared_ptr<TimelineState>& state(const Token& token) { return token._state; }
    };
}
