//
// Created by radue on 18.07.2026.
//

#include "log.h"

#include <chrono>
#include <optional>
#include <deque>
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

    // ---- history --------------------------------------------------------------------------------
    //
    // Its own state, and its own lock: recording a message must not contend with the repeat limiter,
    // and the two are asked different questions. One buffer per *process* — this function lives in
    // libKoral and is exported, which is the whole reason a log panel can see messages that a scene
    // or a module logged. @see kor::log::record

    namespace {
        struct History {
            std::mutex mutex;
            std::deque<Record> records;
            std::size_t limit = 2048;
            std::optional<std::chrono::steady_clock::time_point> start;
            /// Never reset, not even by clearHistory: a reader holding an old number must not be told
            /// that older records are new again.
            std::uint64_t nextSequence = 1;
        };

        History& history_()
        {
            static History h;
            return h;
        }
    }

    void record(const Level level, std::string message)
    {
        auto& h = history_();
        const std::scoped_lock lock(h.mutex);
        if (h.limit == 0) return;

        // Time is measured from the first message rather than from process start: what a reader wants
        // is "when, relative to the run", and the first log line is as good a zero as any.
        const auto now = std::chrono::steady_clock::now();
        if (!h.start) h.start = now;
        const auto seconds = std::chrono::duration<double>(now - *h.start).count();

        h.records.push_back(Record{ level, std::move(message), seconds, h.nextSequence++ });
        while (h.records.size() > h.limit) h.records.pop_front();
    }

    std::vector<Record> history()
    {
        auto& h = history_();
        const std::scoped_lock lock(h.mutex);
        return { h.records.begin(), h.records.end() };
    }

    std::vector<Record> historySince(const std::uint64_t sequence)
    {
        auto& h = history_();
        const std::scoped_lock lock(h.mutex);

        // Newest last, so walk back only as far as the caller's mark rather than over the whole log.
        auto first = h.records.end();
        while (first != h.records.begin() && std::prev(first)->sequence > sequence) --first;
        return { first, h.records.end() };
    }

    std::uint64_t lastSequence()
    {
        auto& h = history_();
        const std::scoped_lock lock(h.mutex);
        return h.records.empty() ? 0 : h.records.back().sequence;
    }

    void setHistoryLimit(const std::size_t limit)
    {
        auto& h = history_();
        const std::scoped_lock lock(h.mutex);
        h.limit = limit;
        while (h.records.size() > h.limit) h.records.pop_front();
    }

    std::size_t historyLimit()
    {
        auto& h = history_();
        const std::scoped_lock lock(h.mutex);
        return h.limit;
    }

    void clearHistory()
    {
        auto& h = history_();
        const std::scoped_lock lock(h.mutex);
        h.records.clear();
    }
}
