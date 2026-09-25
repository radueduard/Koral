//
// Created by radue on 9/24/2026.
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "api.h"
#include "error.h"

/**
 * The frame graph's compiler: from what each pass says it reads, writes and creates, work out the
 * order the passes run in, which ones are not needed at all, and how long each resource the graph
 * creates has to live. Pure CPU logic — it knows nothing of images or command buffers — so all of it
 * can be tested without a device.
 *
 * Recording needs none of this: passes may be recorded in any order and in parallel, because the
 * barriers are resolved when the frame *ends* their command buffers, which it does in the order
 * worked out here. The order is what the GPU sees.
 */
namespace kor::graph {
    enum class Access : std::uint8_t {
        eRead,   ///< Reads what the passes before it left.
        eWrite,  ///< Modifies it in place (a read-modify-write counts as a write).
        eCreate, ///< Brings it into existence; exactly one pass may, unless it is imported.
        /**
         * Modifies it in place and publishes the result under a new name (Use::as). Runs after every
         * pass that reads or writes the old name — they see it as it was — and before any that uses
         * the new one. Both names are the same physical resource. One pass may consume a name.
         */
        eConsume,
        /**
         * Reads it as it stood at the end of the *previous* frame. No ordering with this frame's
         * passes at all — it may run before this frame's creator — but it keeps every pass that
         * produces the resource, since the next frame needs what they leave. Only for resources the
         * graph creates: an imported one has no previous frame the graph keeps.
         */
        eReadPrevious,
    };

    struct Use {
        std::string resource;
        Access access;
        std::string as {};  ///< eConsume only: the name the modified resource goes by afterwards.
        /**
         * eRead and eReadPrevious: the state the read needs the resource in; 0 is unknown. See
         * `layout` for what passes on different queues may do with it at the same time.
         */
        std::uint32_t state = 0;
        /**
         * The state is a layout the resource has to be moved into — an image. Reads of a resource
         * without one (a buffer) in a known state never keep passes on different queues apart. Reads
         * of one with a layout do only when every read of it this frame is in that one state and GPU
         * passes make it before any reads it: the pass that makes it last then moves it into that
         * state for all of them (CompiledGraph::handoffs), so no reader changes it under another.
         */
        bool layout = false;
    };

    struct PassDecl {
        std::string name;
        std::vector<Use> uses;
        /** Kept even when nothing reads what it makes — work whose effect is outside the graph. */
        bool sideEffect = false;
        /**
         * Switched off. It is left out, and what depended on it degrades instead of failing: if it
         * consumed a resource, the new name refers to the resource unchanged; passes that need
         * something only it creates are skipped along with it (CompiledGraph::skipped).
         */
        bool enabled = true;
        /**
         * Runs on the CPU, not the GPU: work whose results GPU passes use — filling a buffer the
         * CPU writes, working out a draw list. It runs before the GPU has done anything this frame,
         * so everything it depends on must be CPU work too (or imported), and it cannot read a
         * previous frame the GPU produced.
         */
        bool cpu = false;
        /**
         * Runs on the async compute queue, alongside the graphics passes it is not ordered against.
         * Ignored for a CPU pass.
         */
        bool async = false;
    };

    struct CompiledGraph {
        /** Declaration indices of the passes that run, in the order they run. */
        std::vector<std::size_t> order;
        /** Declaration indices of the passes nothing needed. */
        std::vector<std::size_t> culled;
        /** Declaration indices of the passes that were not enabled. */
        std::vector<std::size_t> disabled;
        /** A pass left out because a disabled pass (or one skipped for that) makes what it needs. */
        struct Skipped {
            std::size_t pass;
            std::string resource;  ///< The first missing input it names.
            std::size_t source;    ///< The pass that would have made it.
        };
        std::vector<Skipped> skipped;
        /**
         * One per entry of `order`: the length of the longest chain of dependencies leading to it.
         * Passes on the same level depend on nothing in each other, so their work can overlap.
         */
        std::vector<std::uint32_t> level;
        /**
         * One per entry of `order`: the positions in `order` of the passes it has to wait for — what
         * it reads, writes or consumes comes from them. Ascending.
         */
        std::vector<std::vector<std::size_t>> dependencies;

        /** One per entry of `order`: runs on the async compute queue. */
        std::vector<bool> async;
        /**
         * One per entry of `order`: the pass on the *other* queue it has to wait for, if any — the
         * latest one it depends on there, which a queue running in order makes stand for every
         * earlier one. Only GPU passes; a CPU pass is done before any GPU work is submitted.
         */
        std::vector<std::optional<std::size_t>> waits;
        /**
         * Orderings the compiler added, as (earlier, later) positions in `order`: passes on different
         * queues that nothing orders would run at the same time, and when they share a resource one
         * would change it — its layout, say — under the other. So the later one waits.
         */
        std::vector<std::pair<std::size_t, std::size_t>> serialized;

        /** A resource the pass at `pass` leaves in `state` when it is done, for readers on both queues. */
        struct Handoff {
            std::size_t pass;
            std::string resource;   ///< The physical resource.
            std::uint32_t state;
            bool operator==(const Handoff&) const = default;
        };
        std::vector<Handoff> handoffs;

        /** A resource the graph creates, and the span of `order` positions that use it. */
        struct Lifetime {
            std::string resource;
            std::size_t first;
            std::size_t last;
            /**
             * An async pass uses it. Positions in `order` then say nothing about when it is in use
             * relative to the other queue's passes, so it must not share memory.
             */
            bool async = false;
        };
        /** Created resources some kept pass uses, in order of first use. Imported ones are not listed. */
        std::vector<Lifetime> lifetimes;

        /**
         * Physical resources (created, never aliases) some kept pass reads from the previous frame.
         * Each needs a copy kept from one frame to the next. In name order.
         */
        std::vector<std::string> history;

        /**
         * Every name a consume introduced, and the resource (created or imported) it is really —
         * including those a disabled consumer passes through unchanged.
         */
        std::map<std::string, std::string> aliases;
    };

    /**
     * @param passes In declaration order, which also breaks ties between passes free to run in either
     *               order — so a graph declared in a sensible order runs in that order.
     * @param imported Resources that come from outside the graph (the screen, the scene's own images).
     *               Writing one is visible outside, so a pass that does is never culled.
     * @return The schedule, or eFrameGraphInvalid naming the resources and passes at fault: a resource
     *         nothing creates or imports, one created twice or both created and imported, or a cycle.
     */
    [[nodiscard]] KORAL_API Result<CompiledGraph> compile(const std::vector<PassDecl>& passes,
                                                         const std::set<std::string>& imported = {});

    /**
     * @brief Which resources can live in the same memory: those never in use at the same time.
     *
     * Greedy over the lifetimes in the order they start: each takes the first slot of its own kind
     * that its previous occupant has finished with — strictly before this one starts, since a pass
     * that reads one and writes the other needs both at once.
     *
     * @param lifetimes As CompiledGraph::lifetimes.
     * @param keys One per lifetime. Only equal keys share a slot (same format and size, say); an
     *             empty key never shares.
     * @return One slot per lifetime, numbered from 0. Lifetimes with the same slot share it.
     */
    [[nodiscard]] KORAL_API std::vector<std::size_t> packLifetimes(const std::vector<CompiledGraph::Lifetime>& lifetimes,
                                                                   const std::vector<std::string>& keys);
}
