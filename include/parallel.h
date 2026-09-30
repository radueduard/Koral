/**
 * @file parallel.h
 * @brief Data-parallel work on every core: ParallelFor, awaited as a coroutine or waited for.
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>

#include "api.h"
#include "token.h"

namespace kor
{
    namespace detail
    {
        struct SceneLife;
        struct ParallelState;
        KORAL_API void runChunks(ParallelState& state);
        KORAL_API void rethrowIfFailed(const ParallelState& state);
    }

    /**
     * @brief Work running on every core: what ParallelFor returns.
     *
     * `co_await` it from a coroutine, which suspends and resumes where it was once every part is
     * done; or Wait() for it from ordinary code — a scene's Update — which runs parts of it on the
     * calling thread while it waits, so a core is never left idle and a short loop costs no thread
     * switch at all. Either way an exception thrown by the body is thrown again there (the first,
     * when several threw).
     *
     * Only this work is run by a thread that waits: never something unrelated from the pool, so a
     * frame is not held up by someone else's texture decode.
     */
    class KORAL_API Work {
    public:
        Work() = default;
        explicit Work(std::shared_ptr<detail::ParallelState> state);

        /** @brief Whether every part has run. */
        [[nodiscard]] bool Ready() const;

        /** @brief Runs parts on this thread until none is left, waits for the rest, then rethrows what the body threw. */
        void Wait() const;

        /** @brief Signalled once every part has run: to hand to anything that takes a Token. */
        [[nodiscard]] Token Completion() const;

        struct Awaiter {
            std::shared_ptr<detail::ParallelState> state;
            Token::Awaiter inner;
            explicit Awaiter(std::shared_ptr<detail::ParallelState> s, Token token) : state(std::move(s)), inner(std::move(token)) {}
            bool await_ready() const noexcept { return inner.await_ready(); }
            bool await_suspend(const std::coroutine_handle<> h) { return inner.await_suspend(h); }
            void await_resume() const { detail::rethrowIfFailed(*state); }
        };
        Awaiter operator co_await() const { return Awaiter{_state, Completion()}; }

    private:
        std::shared_ptr<detail::ParallelState> _state;
    };

    namespace detail
    {
        /** What a ParallelFor shares between the threads running it. */
        struct ParallelState {
            std::function<void(std::size_t, std::size_t)> body;
            std::size_t begin = 0, end = 0, grain = 1, chunks = 0;
            std::atomic<std::size_t> next{0};
            std::atomic<std::size_t> done{0};
            Token completion = Token::Create();
            std::mutex failureMutex;
            std::exception_ptr failure;
            std::shared_ptr<SceneLife> scene;   ///< The scene current where it was started, current in every part.
        };

        /** Splits [begin, end) into parts of about @p grain and starts them on the background pool. */
        KORAL_API Work parallelChunks(std::size_t begin, std::size_t end, std::size_t grain,
                                      std::function<void(std::size_t, std::size_t)> body);
    }

    /**
     * @brief Calls @p body(i) for every i in [@p begin, @p end), on every core.
     *
     * @code
     * void Physics::FixedUpdate() {
     *     kor::ParallelFor(0, bodies.size(), [&](std::size_t i) { Integrate(bodies[i], dt); }).Wait();
     * }
     *
     * kor::Task<void> Terrain::Build() {
     *     co_await kor::ParallelFor(0, chunks.size(), [&](std::size_t i) { Mesh(chunks[i]); });
     *     Upload();                                  // resumes here, where it was, once all are done
     * }
     * @endcode
     *
     * The range is split into a few parts per core — or into parts of @p grain indices, when given —
     * and each part is a plain loop over @p body, so a small body costs no more than a loop would.
     * Parts run at the same time: @p body must only touch what its index owns (or synchronise), and,
     * like a render pass's Record, not the scene's Window, Input or Time. The scene current where it
     * was started is current in every part.
     *
     * Without a background pool — no application or context yet — it runs on the calling thread.
     */
    template <typename Body>
    [[nodiscard]] Work ParallelFor(const std::size_t begin, const std::size_t end, Body&& body, const std::size_t grain = 0)
    {
        return detail::parallelChunks(begin, end, grain,
            [body = std::forward<Body>(body)](const std::size_t first, const std::size_t last) mutable {
                for (std::size_t i = first; i < last; ++i) body(i);
            });
    }

    /**
     * @brief ParallelFor with the parts themselves: @p body(first, last) for each, for a body that
     *        does better with a whole range at once — one SIMD loop, one scratch allocation per part.
     */
    template <typename Body>
    [[nodiscard]] Work ParallelForRanges(const std::size_t begin, const std::size_t end, Body&& body, const std::size_t grain = 0)
    {
        return detail::parallelChunks(begin, end, grain, std::forward<Body>(body));
    }
}
