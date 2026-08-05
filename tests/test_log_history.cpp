// Unit tests for kor::log's history — the buffer a log panel reads.
//
// It exists because spdlog's registry is a static per library, so a sink attached from one image sees
// only that image's messages. This buffer lives in libKoral behind an exported function instead, which
// is what lets one panel show what the engine, a scene and a module all logged. These tests pin the
// behaviour that panel depends on: order, bounds, and that a suppressed flood is suppressed here too.

#include <gtest/gtest.h>

#include <string>

#include <log.h>

using kor::log::Level;

namespace {

// The history is process-wide, so every test starts from a known state.
struct LogHistory : testing::Test
{
    void SetUp() override {
        kor::log::setHistoryLimit(2048);
        kor::log::clearHistory();
        kor::log::resetRepeatCounts();
    }
    void TearDown() override {
        kor::log::clearHistory();
        kor::log::resetRepeatCounts();
    }
};

TEST_F(LogHistory, StartsEmpty) {
    EXPECT_TRUE(kor::log::history().empty());
}

TEST_F(LogHistory, KeepsWhatWasLoggedInOrder) {
    kor::log::info("first {}", 1);
    kor::log::warn("second");
    kor::log::error("third");

    const auto records = kor::log::history();
    ASSERT_EQ(records.size(), 3u);
    EXPECT_EQ(records[0].message, "first 1");
    EXPECT_EQ(records[0].level, Level::eInfo);
    EXPECT_EQ(records[1].message, "second");
    EXPECT_EQ(records[1].level, Level::eWarn);
    EXPECT_EQ(records[2].message, "third");
    EXPECT_EQ(records[2].level, Level::eError);
}

TEST_F(LogHistory, TimesAreMeasuredFromTheFirstMessageAndNeverGoBackwards) {
    kor::log::info("one");
    kor::log::info("two");

    const auto records = kor::log::history();
    ASSERT_EQ(records.size(), 2u);
    EXPECT_GE(records[0].time, 0.0);
    EXPECT_GE(records[1].time, records[0].time);
}

TEST_F(LogHistory, OldestMessagesFallOffTheEnd) {
    kor::log::setHistoryLimit(4);
    for (int i = 0; i < 10; ++i) kor::log::info("message {}", i);

    const auto records = kor::log::history();
    ASSERT_EQ(records.size(), 4u);
    // The *last* four, not the first: a panel wants what just happened.
    EXPECT_EQ(records.front().message, "message 6");
    EXPECT_EQ(records.back().message, "message 9");
}

TEST_F(LogHistory, LoweringTheLimitDropsWhatNoLongerFits) {
    for (int i = 0; i < 10; ++i) kor::log::info("message {}", i);
    kor::log::setHistoryLimit(3);

    const auto records = kor::log::history();
    ASSERT_EQ(records.size(), 3u);
    EXPECT_EQ(records.back().message, "message 9");
}

TEST_F(LogHistory, AZeroLimitKeepsNothing) {
    kor::log::setHistoryLimit(0);
    kor::log::error("this is still printed, just not kept");
    EXPECT_TRUE(kor::log::history().empty());
    EXPECT_EQ(kor::log::historyLimit(), 0u);
}

TEST_F(LogHistory, ClearingLeavesTheLimitAlone) {
    kor::log::setHistoryLimit(7);
    kor::log::info("something");
    kor::log::clearHistory();

    EXPECT_TRUE(kor::log::history().empty());
    EXPECT_EQ(kor::log::historyLimit(), 7u);
}

// The panel must agree with the terminal about a flood: a message suppressed by the repeat limiter is
// not recorded either, or the history would fill with the very thing suppression exists to stop.
TEST_F(LogHistory, ASuppressedRepeatIsNotRecorded) {
    kor::log::setRepeatLimit(3);
    for (int i = 0; i < 50; ++i) kor::log::warn("the same problem every frame");

    const auto records = kor::log::history();
    EXPECT_EQ(records.size(), 3u);
    for (const auto& record : records) {
        EXPECT_EQ(record.level, Level::eWarn);
        EXPECT_EQ(record.message, "the same problem every frame");
    }
    kor::log::setRepeatLimit(10);
}

// Distinct messages have distinct budgets, so a varying value in a warning does not starve the rest.
TEST_F(LogHistory, DistinctMessagesAreEachKept) {
    kor::log::setRepeatLimit(2);
    for (int i = 0; i < 5; ++i) kor::log::warn("resource {} is unusable", i);

    EXPECT_EQ(kor::log::history().size(), 5u);
    kor::log::setRepeatLimit(10);
}

// info is not repeat-suppressed, and the history has to reflect that rather than inventing a rule.
TEST_F(LogHistory, RepeatedInfoIsKeptEveryTime) {
    kor::log::setRepeatLimit(2);
    for (int i = 0; i < 6; ++i) kor::log::info("loading");

    EXPECT_EQ(kor::log::history().size(), 6u);
    kor::log::setRepeatLimit(10);
}

} // namespace
