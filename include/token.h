//
// Created by radue on 9/23/2026.
//

#pragma once

#include <coroutine>
#include <cstdint>
#include <memory>
#include <utility>

#include "api.h"

namespace kor {
    namespace detail { struct TimelineState; struct TokenAccess; struct WaiterSlot; }

    class Token;

    /**
     * @brief An ordered stream of events from one producer: a counter that only goes up.
     *
     * Each call to next() reserves the next value and hands back the Token that will be signalled
     * when the producer gets there. Reaching a value reaches every value before it, so a timeline
     * belongs to **one** producer that finishes its work in the order it reserved it — a loop, a
     * queue, the frame. Two producers that can finish out of order need two timelines.
     *
     * @code
     * kor::Timeline steps;
     * kor::Token first  = steps.next();   // value 1
     * kor::Token second = steps.next();   // value 2
     * second.signal();                    // reaches 2, and therefore 1 as well
     * @endcode
     */
    class KORAL_API Timeline {
    public:
        /** @brief A new timeline, at value 0 with nothing reserved. */
        Timeline();

        /** @brief Reserves the next value and returns the token that stands for it. */
        [[nodiscard]] Token next();

        /**
         * @brief The token for a given value, without reserving anything.
         *
         * For two sides that already agree on the numbering — step n of a loop, frame n — so each
         * can name the same event without handing tokens back and forth:
         *
         * @code
         * // loop:  co_await requests.at(n); step(n); done.at(n).signal();
         * // frame: requests.at(n).signal(); ... done.at(n).wait();
         * @endcode
         */
        [[nodiscard]] Token at(std::uint64_t value) const;

        /** @brief The highest value reached so far. */
        [[nodiscard]] std::uint64_t value() const noexcept;

    private:
        std::shared_ptr<detail::TimelineState> _state;
    };

    /**
     * @brief A point on a Timeline: something that will happen, which code can wait for.
     *
     * The one synchronisation primitive Koral exposes. A token can be
     * - polled, with ready();
     * - waited on by blocking code, with wait();
     * - awaited by a coroutine, with `co_await token`, which suspends it without holding a thread;
     * - signalled from the CPU, with signal().
     *
     * A coroutine that suspends on the main thread resumes there (on the next drain); one that
     * suspends anywhere else resumes on the background pool.
     *
     * @code
     * kor::Token loaded = kor::Token::Create();
     *
     * kor::Task<void> Consumer(kor::Token loaded) {
     *     co_await loaded;                // no thread is held while this waits
     *     use(data);
     * }
     *
     * kor::Task<void> Producer(kor::Token loaded) {
     *     co_await kor::Context::SwitchToBackgroundThread();
     *     data = readFromDisk();
     *     loaded.signal();                // resumes Consumer
     * }
     * @endcode
     *
     * Tokens are cheap to copy; every copy refers to the same event. A default-constructed token
     * refers to no event and is always ready.
     *
     * @warning wait() on the main thread for something only a main-thread coroutine will signal
     *          never returns: that coroutine resumes when the main thread drains, which is what
     *          wait() is blocking.
     */
    class KORAL_API Token {
    public:
        /** @brief A token for no event: always ready; signalling it does nothing. */
        Token() noexcept = default;

        /** @brief A one-shot token on a timeline of its own. */
        [[nodiscard]] static Token Create();

        /** @brief Whether the event has happened. */
        [[nodiscard]] bool ready() const noexcept;

        /** @brief Blocks the calling thread until the event has happened. */
        void wait() const;

        /**
         * @brief Marks the event as happened, and resumes everything waiting on it.
         *
         * Also reaches every earlier value on the same timeline. Signalling a token whose value has
         * already been reached does nothing and logs a warning — it is almost always two producers
         * sharing one timeline.
         */
        void signal() const;

        /** @brief This token's value on its timeline (0 for a default-constructed token). */
        [[nodiscard]] std::uint64_t value() const noexcept { return _value; }

        /** @brief Whether two tokens stand for the same event. */
        friend bool operator==(const Token&, const Token&) noexcept = default;

        struct Awaiter;

        /** @brief Makes the token awaitable: `co_await token` suspends until it is signalled. */
        Awaiter operator co_await() const noexcept;

    private:
        friend class Timeline;
        friend struct detail::TokenAccess;
        Token(std::shared_ptr<detail::TimelineState> state, std::uint64_t value) noexcept
            : _state(std::move(state)), _value(value) {}

        // Registers `h` to be resumed once the value is reached, and returns the slot it waits in —
        // or null when the value has already been reached, and the coroutine carries on at once.
        std::shared_ptr<detail::WaiterSlot> suspend(std::coroutine_handle<> h) const;

        // The coroutine waiting in `slot` is being destroyed while it waits: see that nothing ever
        // resumes it. A no-op once it has been resumed.
        void cancel(const std::shared_ptr<detail::WaiterSlot>& slot) const noexcept;

        std::shared_ptr<detail::TimelineState> _state;
        std::uint64_t _value = 0;
    };

    struct Token::Awaiter {
        Token token; // a copy, so the event outlives the suspension even if the awaited token doesn't
        std::shared_ptr<detail::WaiterSlot> slot; // set while suspended

        explicit Awaiter(Token awaited) noexcept : token(std::move(awaited)) {}
        Awaiter(const Awaiter&) = delete;
        Awaiter& operator=(const Awaiter&) = delete;

        // The awaiter lives in the coroutine's frame for as long as the coroutine waits, so
        // destroying a waiting coroutine — its Task going out of scope — runs this. Without it the
        // timeline would later resume a frame that no longer exists.
        ~Awaiter() { if (slot) token.cancel(slot); }

        bool await_ready() const noexcept { return token.ready(); }
        bool await_suspend(const std::coroutine_handle<> h) { slot = token.suspend(h); return slot != nullptr; }
        void await_resume() const noexcept {}
    };

    inline Token::Awaiter Token::operator co_await() const noexcept { return Awaiter{*this}; }
}
