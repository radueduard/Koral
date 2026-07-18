//
// Created by radue on 18.07.2026.
//

#include "log.h"

#include <format>
#include <mutex>
#include <unordered_map>

namespace kor::log
{
    namespace {
        // Distinct messages tracked before the limiter stops counting. A run with thousands of
        // genuinely different errors would otherwise grow this map without bound; past the cap
        // new messages are simply always emitted, which is the safe direction to fail.
        constexpr std::size_t kMaxTrackedMessages = 4096;

        struct State {
            std::mutex mutex;
            std::unordered_map<std::string, std::size_t> counts;
            std::size_t limit = 10;
        };

        State& state()
        {
            static State s;
            return s;
        }
    }

    Repeat track(const std::string& message)
    {
        auto& s = state();
        const std::scoped_lock lock(s.mutex);

        if (s.limit == 0) return Repeat::eShow;
        if (s.counts.size() >= kMaxTrackedMessages && !s.counts.contains(message)) return Repeat::eShow;

        const std::size_t seen = ++s.counts[message];
        if (seen < s.limit)  return Repeat::eShow;
        // The last showing is flagged rather than silent, so the reader learns the problem is
        // recurring and that the log is about to go quiet about it.
        if (seen == s.limit) return Repeat::eShowLast;
        return Repeat::eSuppress;
    }

    bool shouldEmit(const std::string& message)
    {
        return track(message) != Repeat::eSuppress;
    }

    std::string suppressionNotice()
    {
        return std::format("[log] the message above has now been shown {} times and will be "
                           "suppressed from here on (kor::log::setRepeatLimit to change).",
                           repeatLimit());
    }

    void setRepeatLimit(const std::size_t limit)
    {
        auto& s = state();
        const std::scoped_lock lock(s.mutex);
        s.limit = limit;
    }

    std::size_t repeatLimit()
    {
        auto& s = state();
        const std::scoped_lock lock(s.mutex);
        return s.limit;
    }

    void resetRepeatCounts()
    {
        auto& s = state();
        const std::scoped_lock lock(s.mutex);
        s.counts.clear();
    }
}
