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
        kor::log::SetHistoryLimit(2048);
        kor::log::ClearHistory();
        kor::log::ResetRepeatCounts();
    }
    void TearDown() override {
        kor::log::ClearHistory();
        kor::log::ResetRepeatCounts();
    }
};

TEST_F(LogHistory, StartsEmpty) {
    EXPECT_TRUE(kor::log::History().empty());
}

TEST_F(LogHistory, KeepsWhatWasLoggedInOrder) {
    kor::log::Info("first {}", 1);
    kor::log::Warn("second");
    kor::log::Error("third");

    const auto records = kor::log::History();
    ASSERT_EQ(records.size(), 3u);
    EXPECT_EQ(records[0].message, "first 1");
    EXPECT_EQ(records[0].level, Level::eInfo);
    EXPECT_EQ(records[1].message, "second");
    EXPECT_EQ(records[1].level, Level::eWarn);
    EXPECT_EQ(records[2].message, "third");
    EXPECT_EQ(records[2].level, Level::eError);
}

TEST_F(LogHistory, TimesAreMeasuredFromTheFirstMessageAndNeverGoBackwards) {
    kor::log::Info("one");
    kor::log::Info("two");

    const auto records = kor::log::History();
    ASSERT_EQ(records.size(), 2u);
    EXPECT_GE(records[0].time, 0.0);
    EXPECT_GE(records[1].time, records[0].time);
}

TEST_F(LogHistory, OldestMessagesFallOffTheEnd) {
    kor::log::SetHistoryLimit(4);
    for (int i = 0; i < 10; ++i) kor::log::Info("message {}", i);

    const auto records = kor::log::History();
    ASSERT_EQ(records.size(), 4u);
    // The *last* four, not the first: a panel wants what just happened.
    EXPECT_EQ(records.front().message, "message 6");
    EXPECT_EQ(records.back().message, "message 9");
}

TEST_F(LogHistory, LoweringTheLimitDropsWhatNoLongerFits) {
    for (int i = 0; i < 10; ++i) kor::log::Info("message {}", i);
    kor::log::SetHistoryLimit(3);

    const auto records = kor::log::History();
    ASSERT_EQ(records.size(), 3u);
    EXPECT_EQ(records.back().message, "message 9");
}

TEST_F(LogHistory, AZeroLimitKeepsNothing) {
    kor::log::SetHistoryLimit(0);
    kor::log::Error("this is still printed, just not kept");
    EXPECT_TRUE(kor::log::History().empty());
    EXPECT_EQ(kor::log::HistoryLimit(), 0u);
}

TEST_F(LogHistory, ClearingLeavesTheLimitAlone) {
    kor::log::SetHistoryLimit(7);
    kor::log::Info("something");
    kor::log::ClearHistory();

    EXPECT_TRUE(kor::log::History().empty());
    EXPECT_EQ(kor::log::HistoryLimit(), 7u);
}

// The panel must agree with the terminal about a flood: a message suppressed by the repeat limiter is
// not recorded either, or the history would fill with the very thing suppression exists to stop.
TEST_F(LogHistory, ASuppressedRepeatIsNotRecorded) {
    kor::log::SetRepeatLimit(3);
    for (int i = 0; i < 50; ++i) kor::log::Warn("the same problem every frame");

    const auto records = kor::log::History();
    EXPECT_EQ(records.size(), 3u);
    for (const auto& record : records) {
        EXPECT_EQ(record.level, Level::eWarn);
        EXPECT_EQ(record.message, "the same problem every frame");
    }
    kor::log::SetRepeatLimit(10);
}

// Distinct messages have distinct budgets, so a varying value in a warning does not starve the rest.
TEST_F(LogHistory, DistinctMessagesAreEachKept) {
    kor::log::SetRepeatLimit(2);
    for (int i = 0; i < 5; ++i) kor::log::Warn("resource {} is unusable", i);

    EXPECT_EQ(kor::log::History().size(), 5u);
    kor::log::SetRepeatLimit(10);
}

// info is not repeat-suppressed, and the history has to reflect that rather than inventing a rule.
TEST_F(LogHistory, RepeatedInfoIsKeptEveryTime) {
    kor::log::SetRepeatLimit(2);
    for (int i = 0; i < 6; ++i) kor::log::Info("loading");

    EXPECT_EQ(kor::log::History().size(), 6u);
    kor::log::SetRepeatLimit(10);
}

} // namespace
