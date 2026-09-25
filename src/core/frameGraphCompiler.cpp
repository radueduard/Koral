//
// Created by radue on 9/24/2026.
//

#include "frameGraphCompiler.h"

#include <algorithm>
#include <format>
#include <functional>
#include <map>
#include <optional>
#include <queue>
#include <ranges>

namespace kor::graph {
    namespace {
        struct ResourceInfo {
            std::optional<std::size_t> creator;   // or, for a consumed name's new name, its consumer
            std::vector<std::size_t> writers;     // declaration order
            std::vector<std::size_t> readers;
            std::optional<std::size_t> consumer;
        };

        std::string quoted(const std::vector<std::size_t>& indices, const std::vector<PassDecl>& passes) {
            std::string out;
            for (const auto i : indices) {
                if (!out.empty()) out += ", ";
                out += std::format("'{}'", passes[i].name);
            }
            return out;
        }
    }

    // The whole compiler for a graph whose passes are all enabled; compile() takes the rest out first.
    static Result<CompiledGraph> compileEnabled(const std::vector<PassDecl>& passes, const std::set<std::string>& imported) {
        const std::size_t n = passes.size();
        std::vector<std::string> problems;

        // ---- who makes, changes and reads each resource -------------------------------------
        // A std::map, so problems are reported in a stable order.
        std::map<std::string, ResourceInfo> resources;
        std::map<std::string, std::string> aliasOf;  // consume's new name -> the name it consumed
        // Reads of last frame's version, kept apart: a pass may read the previous frame's copy of
        // the very thing it creates this frame, and the two must not collapse into one use.
        std::vector<std::pair<std::size_t, std::string>> previousReads;
        for (std::size_t p = 0; p < n; ++p) {
            // One pass naming a resource twice counts once, at the strongest access it asked for.
            std::map<std::string, Use> strongest;
            for (const auto& use : passes[p].uses) {
                if (use.access == Access::eReadPrevious) {
                    previousReads.emplace_back(p, use.resource);
                    continue;
                }
                auto [it, fresh] = strongest.try_emplace(use.resource, use);
                if (!fresh && static_cast<int>(use.access) > static_cast<int>(it->second.access)) it->second = use;
            }
            for (const auto& [name, use] : strongest) {
                auto& info = resources[name];
                switch (use.access) {
                case Access::eCreate:
                    if (imported.contains(name)) {
                        problems.push_back(std::format("'{}' is imported, but pass '{}' also creates it.", name, passes[p].name));
                    } else if (info.creator) {
                        problems.push_back(std::format("'{}' is created by both '{}' and '{}'; only one pass may create it.",
                                                       name, passes[*info.creator].name, passes[p].name));
                    } else {
                        info.creator = p;
                    }
                    break;
                case Access::eWrite: info.writers.push_back(p); break;
                case Access::eRead:  info.readers.push_back(p); break;
                case Access::eConsume: {
                    if (info.consumer) {
                        problems.push_back(std::format("'{}' is consumed by both '{}' and '{}'; only one pass may consume it.",
                                                       name, passes[*info.consumer].name, passes[p].name));
                        break;
                    }
                    info.consumer = p;
                    if (use.as.empty() || use.as == name) {
                        problems.push_back(std::format("Pass '{}' consumes '{}' without giving the result a new name.", passes[p].name, name));
                        break;
                    }
                    // The new name is brought into existence by the consume, like a create.
                    auto& renamed = resources[use.as];
                    if (imported.contains(use.as) || renamed.creator)
                        problems.push_back(std::format("'{}' already exists, so pass '{}' cannot publish '{}' under that name.",
                                                       use.as, passes[p].name, name));
                    else
                        renamed.creator = p;
                    aliasOf[use.as] = name;
                    break;
                }
                case Access::eReadPrevious: break;  // collected above
                }
            }
        }
        for (const auto& [name, info] : resources) {
            if (info.creator || imported.contains(name)) continue;
            std::vector<std::size_t> users = info.writers;
            users.insert(users.end(), info.readers.begin(), info.readers.end());
            if (info.consumer) users.push_back(*info.consumer);
            std::ranges::sort(users);
            problems.push_back(std::format("{} {} '{}', but no pass creates it and it was not imported.",
                                           quoted(users, passes), users.size() == 1 ? "uses" : "use", name));
        }
        // The real resource behind a name: follow consumes back to what was created or imported.
        const auto root = [&](std::string name) {
            for (auto it = aliasOf.find(name); it != aliasOf.end(); it = aliasOf.find(name)) name = it->second;
            return name;
        };
        for (const auto& [p, name] : previousReads) {
            const std::string physical = root(name);
            if (imported.contains(physical)) {
                problems.push_back(std::format("Pass '{}' reads '{}' from the previous frame, but it is imported: the graph "
                                               "keeps previous frames only of what it creates. Keep a copy yourself.",
                                               passes[p].name, name));
            } else if (const auto it = resources.find(physical); it == resources.end() || !it->second.creator) {
                problems.push_back(std::format("Pass '{}' reads '{}' from the previous frame, but no pass creates it.",
                                               passes[p].name, name));
            }
        }
        if (!problems.empty()) {
            std::string message;
            for (const auto& problem : problems) message += (message.empty() ? "" : "\n") + problem;
            return std::unexpected(Error{.code = ErrorCode::eFrameGraphInvalid, .message = message});
        }

        // ---- dependencies ---------------------------------------------------------------------
        // Per resource: its creator first, then its writers in declaration order (each builds on the
        // last), then every reader — readers see what the writers leave, so they follow all of them.
        std::vector<std::map<std::size_t, std::string>> preds(n); // pred -> the resource that links them
        const auto depend = [&](const std::size_t before, const std::size_t after, const std::string& resource) {
            if (before != after) preds[after].try_emplace(before, resource);
        };
        for (const auto& [name, info] : resources) {
            std::optional<std::size_t> last = info.creator;
            for (const auto w : info.writers) {
                if (last) depend(*last, w, name);
                last = w;
            }
            if (last) for (const auto r : info.readers) depend(*last, r, name);
            // A consumer changes it for good, so it waits for everyone who wanted it as it was.
            if (info.consumer) {
                if (last) depend(*last, *info.consumer, name);
                for (const auto r : info.readers) depend(r, *info.consumer, name);
            }
        }

        // ---- order: Kahn's algorithm, lowest declaration index first among the ready ----------
        std::vector<std::vector<std::size_t>> succs(n);
        std::vector<std::size_t> pending(n, 0);
        for (std::size_t p = 0; p < n; ++p) {
            pending[p] = preds[p].size();
            for (const auto& pred : preds[p] | std::views::keys) succs[pred].push_back(p);
        }
        std::priority_queue<std::size_t, std::vector<std::size_t>, std::greater<>> ready;
        for (std::size_t p = 0; p < n; ++p) if (pending[p] == 0) ready.push(p);
        std::vector<std::size_t> sorted;
        while (!ready.empty()) {
            const auto p = ready.top();
            ready.pop();
            sorted.push_back(p);
            for (const auto s : succs[p]) if (--pending[s] == 0) ready.push(s);
        }
        if (sorted.size() != n) {
            // Walk back through unsorted predecessors until a pass repeats: that stretch is a cycle.
            std::size_t at = 0;
            while (pending[at] == 0) ++at;
            std::vector<std::size_t> path;
            std::vector<bool> seen(n, false);
            while (!seen[at]) {
                seen[at] = true;
                path.push_back(at);
                for (const auto& pred : preds[at] | std::views::keys) {
                    if (pending[pred] != 0) { at = pred; break; }
                }
            }
            const auto start = std::ranges::find(path, at);
            std::vector<std::size_t> cycle(start, path.end());
            std::ranges::reverse(cycle); // walked backwards; report it in the order it runs round
            std::string chain = std::format("'{}'", passes[cycle.front()].name);
            for (std::size_t i = 0; i < cycle.size(); ++i) {
                const auto from = cycle[i], to = cycle[(i + 1) % cycle.size()];
                chain += std::format(" -({})-> '{}'", preds[to].at(from), passes[to].name);
            }
            return std::unexpected(Error{
                .code = ErrorCode::eFrameGraphInvalid,
                .message = std::format("These passes depend on each other in a circle, so no order runs "
                                       "them all: {}", chain)});
        }

        // ---- culling: keep what has an effect outside the graph, and everything it needs --------
        std::vector<bool> keep(n, false);
        std::vector<std::size_t> stack;
        for (std::size_t p = 0; p < n; ++p) {
            bool isRoot = passes[p].sideEffect;
            for (const auto& use : passes[p].uses)
                if (use.access != Access::eRead && use.access != Access::eCreate && imported.contains(root(use.resource)))
                    isRoot = true;
            if (isRoot) { keep[p] = true; stack.push_back(p); }
        }
        const auto propagate = [&] {
            while (!stack.empty()) {
                const auto p = stack.back();
                stack.pop_back();
                for (const auto& pred : preds[p] | std::views::keys)
                    if (!keep[pred]) { keep[pred] = true; stack.push_back(pred); }
            }
        };
        propagate();
        // A kept pass reading last frame's version needs this frame to produce it for the next: every
        // pass that creates, writes or consumes the physical resource is kept too. Those may read
        // previous frames of their own, so this runs until nothing more is kept.
        std::set<std::string> history;
        for (bool grew = true; grew;) {
            grew = false;
            for (const auto& [reader, name] : previousReads) {
                if (!keep[reader]) continue;
                const std::string physical = root(name);
                history.insert(physical);
                for (const auto& [resource, info] : resources) {
                    if (root(resource) != physical) continue;
                    std::vector<std::size_t> producers = info.writers;
                    if (info.creator) producers.push_back(*info.creator);
                    if (info.consumer) producers.push_back(*info.consumer);
                    for (const auto p : producers)
                        if (!keep[p]) { keep[p] = true; stack.push_back(p); grew = true; }
                }
            }
            propagate();
        }

        CompiledGraph out;
        std::vector<std::size_t> position(n, SIZE_MAX);
        for (const auto p : sorted) {
            if (!keep[p]) continue;
            position[p] = out.order.size();
            out.order.push_back(p);
        }
        for (std::size_t p = 0; p < n; ++p) if (!keep[p]) out.culled.push_back(p);

        // ---- levels -------------------------------------------------------------------------------
        out.level.resize(out.order.size(), 0);
        for (std::size_t i = 0; i < out.order.size(); ++i) {
            for (const auto& pred : preds[out.order[i]] | std::views::keys)
                if (keep[pred]) out.level[i] = std::max(out.level[i], out.level[position[pred]] + 1);
        }

        // ---- lifetimes of what the graph creates, per physical resource -----------------------------
        std::map<std::string, std::pair<std::size_t, std::size_t>> spans;  // root -> first, last
        for (const auto& [name, info] : resources) {
            const std::string physical = root(name);
            if (imported.contains(physical)) continue;  // not the graph's to allocate
            const auto touch = [&](const std::size_t p) {
                if (!keep[p]) return;
                auto [it, fresh] = spans.try_emplace(physical, position[p], position[p]);
                if (!fresh) {
                    it->second.first = std::min(it->second.first, position[p]);
                    it->second.second = std::max(it->second.second, position[p]);
                }
            };
            if (info.creator) touch(*info.creator);
            for (const auto w : info.writers) touch(w);
            for (const auto r : info.readers) touch(r);
            if (info.consumer) touch(*info.consumer);
        }
        for (const auto& physical : history) {
            // Copied into the next frame's history after the last pass, and read before the first.
            if (!out.order.empty()) spans[physical] = { 0, out.order.size() - 1 };
        }
        for (const auto& [name, span] : spans) out.lifetimes.push_back({name, span.first, span.second});
        out.history.assign(history.begin(), history.end());
        std::ranges::sort(out.lifetimes, {}, &CompiledGraph::Lifetime::first);
        for (const auto& alias : aliasOf | std::views::keys) out.aliases[alias] = root(alias);
        return out;
    }

    Result<CompiledGraph> compile(const std::vector<PassDecl>& passes, const std::set<std::string>& imported) {
        // Disabled passes come out before anything else, and what was left is compiled as if they
        // had never been declared — except that what depended on them has to be dealt with first.
        std::map<std::string, std::string> passThrough;  // a disabled consumer's name -> the name it modified
        std::map<std::string, std::size_t> unavailable;  // a name -> the disabled pass that made it
        std::vector<std::size_t> live;                   // indices into passes
        CompiledGraph out;
        for (std::size_t p = 0; p < passes.size(); ++p) {
            if (passes[p].enabled) { live.push_back(p); continue; }
            out.disabled.push_back(p);
            for (const auto& use : passes[p].uses) {
                if (use.access == Access::eCreate) unavailable.try_emplace(use.resource, p);
                if (use.access == Access::eConsume) passThrough.try_emplace(use.as, use.resource);
            }
        }
        if (out.disabled.empty()) return compileEnabled(passes, imported);

        // A disabled consumer changes nothing, so what it would have published is what it was given.
        const auto resolve = [&](std::string name) {
            for (auto it = passThrough.find(name); it != passThrough.end(); it = passThrough.find(name)) name = it->second;
            return name;
        };
        std::vector<PassDecl> remaining;
        for (const auto p : live) {
            PassDecl decl = passes[p];
            for (auto& use : decl.uses) use.resource = resolve(std::move(use.resource));
            remaining.push_back(std::move(decl));
        }
        // A pass needing what only a disabled pass creates is skipped, and what it would have made
        // is then missing too — so this runs until nothing more drops out.
        for (bool dropped = true; dropped;) {
            dropped = false;
            for (std::size_t i = 0; i < remaining.size(); ++i) {
                const auto& uses = remaining[i].uses;
                const auto missing = std::ranges::find_if(uses, [&](const Use& use) {
                    return use.access != Access::eCreate && unavailable.contains(use.resource);
                });
                if (missing == uses.end()) continue;
                out.skipped.push_back({live[i], missing->resource, unavailable.at(missing->resource)});
                for (const auto& use : uses) {
                    if (use.access == Access::eCreate) unavailable.try_emplace(use.resource, live[i]);
                    if (use.access == Access::eConsume) unavailable.try_emplace(use.as, live[i]);
                }
                live.erase(live.begin() + static_cast<std::ptrdiff_t>(i));
                remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(i));
                dropped = true;
                break;
            }
        }

        auto compiled = compileEnabled(remaining, imported);
        if (!compiled) return compiled;
        // Back to indices into what the caller declared.
        for (auto& p : compiled->order) p = live[p];
        for (auto& p : compiled->culled) p = live[p];
        std::ranges::sort(out.skipped, {}, &CompiledGraph::Skipped::pass);
        compiled->disabled = std::move(out.disabled);
        compiled->skipped = std::move(out.skipped);
        for (const auto& name : passThrough | std::views::keys) {
            const std::string to = resolve(name);
            const auto it = compiled->aliases.find(to);
            compiled->aliases.try_emplace(name, it != compiled->aliases.end() ? it->second : to);
        }
        return compiled;
    }
}
