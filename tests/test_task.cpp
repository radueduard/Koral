// Unit tests for kor::Task (task.h): awaiting tasks, their completion tokens, and WhenAll.
//
// No context exists in this binary, so there are no executors: whatever resumes a coroutine does so
// inline, on the signalling thread. The executor hand-off is covered by the integration suite.

#include <gtest/gtest.h>

#include <atomic>
#include <stdexcept>
#include <thread>
#include <vector>

#include "task.h"
#include "token.h"

using namespace kor;

namespace {

Task<void> WaitFor(Token token) {
    co_await token;
}

Task<int> ValueAfter(Token token, const int value) {
    co_await token;
    co_return value;
}

Task<void> ThrowAfter(Token token) {
    co_await token;
    throw std::runtime_error("boom");
}

TEST(Task, AVoidTaskCanBeAwaited) {
    const Token go = Token::Create();
    auto inner = WaitFor(go);
    bool after = false;
    auto outer = [](Task<void>& t, bool& flag) -> Task<void> {
        co_await t;
        flag = true;
    }(inner, after);

    EXPECT_FALSE(outer.Done());
    go.Signal();
    EXPECT_TRUE(outer.Done());
    EXPECT_TRUE(after);
    EXPECT_TRUE(outer.Take().has_value());
}

TEST(Task, AwaitingATaskYieldsItsValue) {
    const Token go = Token::Create();
    auto inner = ValueAfter(go, 42);
    int seen = 0;
    auto outer = [](Task<int>& t, int& out) -> Task<void> { out = co_await t; }(inner, seen);

    go.Signal();
    EXPECT_TRUE(outer.Done());
    EXPECT_EQ(seen, 42);
}

TEST(Task, AnExceptionIsRethrownInTheAwaitingCoroutine) {
    const Token go = Token::Create();
    auto inner = ThrowAfter(go);
    bool caught = false;
    auto outer = [](Task<void>& t, bool& flag) -> Task<void> {
        try { co_await t; } catch (const std::runtime_error&) { flag = true; }
    }(inner, caught);

    go.Signal();
    EXPECT_TRUE(caught);
    EXPECT_TRUE(outer.Take().has_value());
}

TEST(Task, CompletionIsATokenAndWaitBlocksOnIt) {
    const Token go = Token::Create();
    auto task = WaitFor(go);
    const Token done = task.Completion();
    EXPECT_FALSE(done.Ready());

    std::thread signaller([&] { go.Signal(); });
    task.Wait();
    signaller.join();
    EXPECT_TRUE(done.Ready());
    EXPECT_TRUE(task.Done());
}

TEST(Task, AnEmptyTaskIsAlreadyComplete) {
    const Task<void> none;
    EXPECT_TRUE(none.Done());
    EXPECT_TRUE(none.Completion().Ready());
    none.Wait();  // returns at once
}

TEST(WhenAll, FinishesOnlyOnceEveryTaskHas) {
    std::vector<Token> gos{Token::Create(), Token::Create(), Token::Create()};
    std::vector<Task<void>> tasks;
    for (const auto& go : gos) tasks.push_back(WaitFor(go));

    auto all = WhenAll(std::move(tasks));
    gos[2].Signal();
    gos[0].Signal();
    EXPECT_FALSE(all.Done());
    gos[1].Signal();
    EXPECT_TRUE(all.Done());
    EXPECT_TRUE(all.Take().has_value());
}

TEST(WhenAll, RethrowsTheFirstFailureOnlyAfterEveryTaskFinished) {
    const Token fails = Token::Create(), slow = Token::Create();
    std::vector<Task<void>> tasks;
    tasks.push_back(ThrowAfter(fails));
    tasks.push_back(WaitFor(slow));

    auto all = WhenAll(std::move(tasks));
    fails.Signal();
    EXPECT_FALSE(all.Done()) << "gave up on a task that was still running";
    slow.Signal();
    ASSERT_TRUE(all.Done());
    const auto result = all.Take();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), "boom");
}

TEST(WhenAll, OfTokens) {
    std::vector<Token> tokens{Token::Create(), Token::Create()};
    auto all = WhenAll(tokens);
    tokens[1].Signal();
    EXPECT_FALSE(all.Done());
    tokens[0].Signal();
    EXPECT_TRUE(all.Done());
}

TEST(WhenAll, OfNothingIsDoneAtOnce) {
    auto all = WhenAll(std::vector<Task<void>>{});
    EXPECT_TRUE(all.Done());
}

// The race the old awaiter had: it checked Done() and then set a continuation, so a task finishing
// on another thread in between was never resumed. Awaiting through the completion token closes it.
TEST(Task, AwaitingATaskThatFinishesConcurrentlyNeverLosesTheWakeup) {
    constexpr int kRounds = 2000;
    for (int i = 0; i < kRounds; ++i) {
        const Token go = Token::Create();
        auto inner = WaitFor(go);
        std::atomic<bool> start{false};
        std::thread finisher([&] {
            while (!start.load(std::memory_order_acquire)) {}
            go.Signal();
        });
        start.store(true, std::memory_order_release);
        auto outer = [](Task<void>& t) -> Task<void> { co_await t; }(inner);
        finisher.join();
        ASSERT_TRUE(outer.Done()) << "lost wakeup in round " << i;
    }
}

}  // namespace
