//
// ParallelFor: parts of a range on the background pool, with the caller helping when it waits.
//

#include "parallel.h"

#include <algorithm>
#include <thread>

#include "context.h"
#include "scene.h"
#include "../executor/BackgroundExecutor.h"

namespace kor::detail
{
    struct ParallelAccess {
        static Executor* Pool() { return Context::_backgroundExecutor; }
    };

    namespace
    {
        void runChunk(ParallelState& state, const std::size_t chunk)
        {
            const std::size_t first = state.begin + chunk * state.grain;
            const std::size_t last = std::min(state.end, first + state.grain);
            try {
                state.body(first, last);
            } catch (...) {
                std::lock_guard lock(state.failureMutex);
                if (!state.failure) state.failure = std::current_exception();
            }
            if (state.done.fetch_add(1, std::memory_order_acq_rel) + 1 == state.chunks) state.completion.Signal();
        }
    }

    // Takes parts off the shared counter until there are none: every runner — a pool thread, or the
    // thread waiting — does the same, so it does not matter who gets there first.
    void runChunks(ParallelState& state)
    {
        SceneScope scope(state.scene);
        for (std::size_t chunk; (chunk = state.next.fetch_add(1, std::memory_order_relaxed)) < state.chunks;)
            runChunk(state, chunk);
    }

    void rethrowIfFailed(const ParallelState& state)
    {
        auto& s = const_cast<ParallelState&>(state);
        std::lock_guard lock(s.failureMutex);
        if (s.failure) std::rethrow_exception(s.failure);
    }

    Work parallelChunks(const std::size_t begin, const std::size_t end, std::size_t grain,
                        std::function<void(std::size_t, std::size_t)> body)
    {
        auto state = std::make_shared<ParallelState>();
        state->body = std::move(body);
        state->begin = begin;
        state->end = std::max(begin, end);
        state->scene = CurrentSceneLife();

        const std::size_t count = state->end - state->begin;
        if (count == 0) {
            state->completion.Signal();
            return Work(state);
        }

        auto* pool = ParallelAccess::Pool();
        const std::size_t threads = std::max(1u, std::thread::hardware_concurrency());
        // A few parts per core by default: enough that a core finishing early finds more to do.
        if (grain == 0) grain = std::max<std::size_t>(1, (count + threads * 4 - 1) / (threads * 4));
        state->grain = grain;
        state->chunks = (count + grain - 1) / grain;

        if (!pool || state->chunks == 1) {
            runChunks(*state);   // nothing to share it with, or nothing worth sharing
            return Work(state);
        }

        // One runner per thread that could help — the caller counts as one, when it waits.
        const std::size_t runners = std::min(state->chunks, threads);
        for (std::size_t i = 0; i < runners; ++i)
            pool->Post([state] { runChunks(*state); });
        return Work(state);
    }
}

namespace kor
{
    Work::Work(std::shared_ptr<detail::ParallelState> state) : _state(std::move(state)) {}

    bool Work::Ready() const { return !_state || _state->completion.Ready(); }

    void Work::Wait() const
    {
        if (!_state) return;
        detail::runChunks(*_state);          // help with what is left
        _state->completion.Wait();           // then only the parts still running elsewhere
        detail::rethrowIfFailed(*_state);
    }

    Token Work::Completion() const
    {
        if (_state) return _state->completion;
        auto done = Token::Create();
        done.Signal();
        return done;
    }
}
