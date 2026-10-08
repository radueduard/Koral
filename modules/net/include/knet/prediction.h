#pragma once

// Ticks, client-side prediction and server-side input buffers: the logic of a responsive networked game,
// independent of how its messages travel. Header-only and allocation-light; ported as is to C# and Kotlin.
//
// The client applies its own input at once (Predictor::Apply) instead of waiting a round trip, sends the
// inputs not yet confirmed (Pending), and when the server's authoritative state for a tick arrives,
// Reconcile rewinds to it and replays what came after — so a misprediction is corrected without the player
// feeling the lag. The server applies each client's inputs at their ticks (InputBuffer).

#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace knet
{
    /**
     * @brief A fixed-rate simulation clock: Advance(frameTime) says how many ticks to run this frame, and
     *        Alpha() how far the frame is into the next — to interpolate rendering between the last two.
     */
    class TickClock {
    public:
        explicit TickClock(double ticksPerSecond = 60.0, int maxTicksPerAdvance = 8)
            : _interval(1.0 / ticksPerSecond), _maxTicks(maxTicksPerAdvance) {}

        /** @brief Adds @p seconds of real time; returns how many ticks are due (at most the cap: a long stall is dropped, not replayed). */
        int Advance(double seconds) {
            _accumulator += std::max(0.0, seconds);
            int ticks = int(_accumulator / _interval);
            if (ticks > _maxTicks) {
                ticks = _maxTicks;
                _accumulator = 0.0;
            } else {
                _accumulator -= ticks * _interval;
            }
            _tick += std::uint32_t(ticks);
            return ticks;
        }
        /** @brief The current tick: how many have run. */
        [[nodiscard]] std::uint32_t Tick() const { return _tick; }
        [[nodiscard]] double Interval() const { return _interval; }
        /** @brief How far into the next tick, 0..1. */
        [[nodiscard]] float Alpha() const { return float(_accumulator / _interval); }
        /** @brief Jumps to @p tick: a client joining a server's clock. */
        void Reset(std::uint32_t tick) { _tick = tick; _accumulator = 0.0; }

    private:
        double _interval;
        int _maxTicks;
        double _accumulator = 0.0;
        std::uint32_t _tick = 0;
    };

    /**
     * @brief Client-side prediction: the local state run ahead on local input, corrected by the server's.
     * @tparam State What the simulation steps (copyable).
     * @tparam Input One tick's input (copyable).
     */
    template<class State, class Input>
    class Predictor {
    public:
        using Step = std::function<void(State&, const Input&)>;

        Predictor(State initial, Step step, std::size_t maxPending = 256)
            : _state(std::move(initial)), _step(std::move(step)), _maxPending(maxPending) {}

        /** @brief Runs @p input for the next tick, at once; returns that tick. */
        std::uint32_t Apply(const Input& input) {
            const std::uint32_t tick = ++_tick;
            _pending.emplace_back(tick, input);
            if (_pending.size() > _maxPending) _pending.pop_front();   // the server is not answering: forget the oldest
            _step(_state, input);
            return tick;
        }

        /**
         * @brief The server's state after its tick @p tick: inputs up to it are confirmed and dropped, the state is
         *        reset to @p authoritative, and the later inputs are replayed on it.
         */
        void Reconcile(std::uint32_t tick, const State& authoritative) {
            if (tick < _confirmed) return;   // older than one already applied: arrived out of order
            _confirmed = tick;
            while (!_pending.empty() && _pending.front().first <= tick) _pending.pop_front();
            _state = authoritative;
            for (const auto& [t, input] : _pending) _step(_state, input);
            ++_reconciliations;
        }

        [[nodiscard]] const State& Current() const { return _state; }
        /** @brief The last tick Apply ran. */
        [[nodiscard]] std::uint32_t Tick() const { return _tick; }
        /** @brief The last tick the server confirmed. */
        [[nodiscard]] std::uint32_t Confirmed() const { return _confirmed; }
        /** @brief Inputs the server has not confirmed: send these each tick (they are small, and packets get lost). */
        [[nodiscard]] const std::deque<std::pair<std::uint32_t, Input>>& Pending() const { return _pending; }
        [[nodiscard]] std::uint64_t Reconciliations() const { return _reconciliations; }

    private:
        State _state;
        Step _step;
        std::size_t _maxPending;
        std::deque<std::pair<std::uint32_t, Input>> _pending;
        std::uint32_t _tick = 0;
        std::uint32_t _confirmed = 0;
        std::uint64_t _reconciliations = 0;
    };

    /**
     * @brief The server side: a client's inputs by tick, received out of order and more than once, handed out in
     *        order. A tick whose input never arrived repeats the last one — the player most likely held the key.
     */
    template<class Input>
    class InputBuffer {
    public:
        explicit InputBuffer(std::size_t capacity = 256) : _capacity(capacity) {}

        /** @brief An input for @p tick; one for a tick already taken, or already held, is ignored. */
        void Add(std::uint32_t tick, const Input& input) {
            if (_taken && tick <= _lastTaken) return;
            _inputs.try_emplace(tick, input);
            while (_inputs.size() > _capacity) _inputs.erase(_inputs.begin());
        }
        /** @brief The input for @p tick: its own, or else the last one taken (nullopt before any). */
        std::optional<Input> Take(std::uint32_t tick) {
            while (!_inputs.empty() && _inputs.begin()->first < tick) _inputs.erase(_inputs.begin());
            _taken = true;
            _lastTaken = tick;
            if (const auto it = _inputs.find(tick); it != _inputs.end()) {
                _last = it->second;
                _inputs.erase(it);
                ++_received;
            } else {
                ++_missed;
            }
            return _last;
        }
        /** @brief The newest tick held: how far ahead of the server the client is. */
        [[nodiscard]] std::optional<std::uint32_t> NewestTick() const { return _inputs.empty() ? std::nullopt : std::optional(_inputs.rbegin()->first); }
        [[nodiscard]] std::size_t Size() const { return _inputs.size(); }
        [[nodiscard]] std::uint64_t Missed() const { return _missed; }
        [[nodiscard]] std::uint64_t Received() const { return _received; }

    private:
        std::size_t _capacity;
        std::map<std::uint32_t, Input> _inputs;
        std::optional<Input> _last;
        bool _taken = false;
        std::uint32_t _lastTaken = 0;
        std::uint64_t _missed = 0, _received = 0;
    };
}
