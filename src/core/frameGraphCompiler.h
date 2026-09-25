//
// Created by radue on 9/24/2026.
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
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

        /** A resource the graph creates, and the span of `order` positions that use it. */
        struct Lifetime {
            std::string resource;
            std::size_t first;
            std::size_t last;
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
