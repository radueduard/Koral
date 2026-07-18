// Unit tests for the repeat suppression in log.h / log.cpp.
//
// A problem inside the frame loop reports itself on every frame; without a cap it buries the
// first, useful report. These tests pin the cap's behaviour by capturing spdlog's output.

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include <spdlog/sinks/ostream_sink.h>

#include "log.h"

namespace {

// Redirects spdlog to a string for the duration of a test, restoring the previous logger after.
class CapturedLog {
public:
    CapturedLog()
        : _previous(spdlog::default_logger())
    {
        auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(_stream);
        auto logger = std::make_shared<spdlog::logger>("test", sink);
        logger->set_level(spdlog::level::trace);
        logger->set_pattern("%v");
        spdlog::set_default_logger(logger);
        kor::log::resetRepeatCounts();
    }

    ~CapturedLog()
    {
        spdlog::set_default_logger(_previous);
        kor::log::resetRepeatCounts();
    }

    [[nodiscard]] std::string text() { spdlog::default_logger()->flush(); return _stream.str(); }

    // Occurrences of `needle`, counting overlaps out (the messages here do not overlap).
    [[nodiscard]] std::size_t count(const std::string& needle)
    {
        const std::string haystack = text();
        std::size_t n = 0;
        for (std::size_t at = haystack.find(needle); at != std::string::npos;
             at = haystack.find(needle, at + needle.size()))
            ++n;
        return n;
    }

private:
    std::ostringstream _stream;
    std::shared_ptr<spdlog::logger> _previous;
};

TEST(LogRepeat, IdenticalErrorsStopAtTheLimit) {
    CapturedLog log;
    ASSERT_EQ(kor::log::repeatLimit(), 10u);

    for (int i = 0; i < 1000; ++i) kor::log::error("poisoned resource dereferenced");

    // Ten showings and no more, however many times the frame loop hits it.
    EXPECT_EQ(log.count("poisoned resource dereferenced"), 10u);
    // ...and the reader is told the log is about to go quiet rather than just losing the tail.
    EXPECT_NE(log.text().find("will be suppressed"), std::string::npos);
}

TEST(LogRepeat, DistinctMessagesGetSeparateBudgets) {
    CapturedLog log;

    for (int i = 0; i < 50; ++i) {
        kor::log::error("first problem");
        kor::log::error("second problem");
    }

    EXPECT_EQ(log.count("first problem"), 10u);
    EXPECT_EQ(log.count("second problem"), 10u);
}

// The key is the rendered text, so a message carrying a varying value is counted per value —
// one broken shader going quiet must not silence a different broken shader.
TEST(LogRepeat, FormattedValuesAreCountedSeparately) {
    CapturedLog log;

    for (int i = 0; i < 50; ++i)
        for (const auto* name : {"a.vert.glsl", "b.frag.glsl"})
            kor::log::error("shader '{}' failed", name);

    EXPECT_EQ(log.count("shader 'a.vert.glsl' failed"), 10u);
    EXPECT_EQ(log.count("shader 'b.frag.glsl' failed"), 10u);
}

TEST(LogRepeat, WarningsAreCappedToo) {
    CapturedLog log;
    for (int i = 0; i < 50; ++i) kor::log::warn("a recurring warning");
    EXPECT_EQ(log.count("a recurring warning"), 10u);
}

TEST(LogRepeat, LimitIsConfigurableAndZeroDisablesSuppression) {
    {
        CapturedLog log;
        kor::log::setRepeatLimit(3);
        for (int i = 0; i < 50; ++i) kor::log::error("thrice only");
        EXPECT_EQ(log.count("thrice only"), 3u);
    }
    {
        CapturedLog log;
        kor::log::setRepeatLimit(0);
        for (int i = 0; i < 25; ++i) kor::log::error("never suppressed");
        EXPECT_EQ(log.count("never suppressed"), 25u);
    }
    kor::log::setRepeatLimit(10);   // restore the default for the rest of the suite
}

TEST(LogRepeat, ResetRestoresTheBudget) {
    CapturedLog log;
    for (int i = 0; i < 50; ++i) kor::log::error("recurring");
    EXPECT_EQ(log.count("recurring"), 10u);

    kor::log::resetRepeatCounts();
    for (int i = 0; i < 50; ++i) kor::log::error("recurring");
    EXPECT_EQ(log.count("recurring"), 20u);
}

} // namespace
