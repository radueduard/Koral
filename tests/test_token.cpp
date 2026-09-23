// Unit tests for kor::Token / kor::Timeline (token.h / token.cpp), CPU side only.
//
// No context exists in this binary, so there are no executors: a coroutine parked on a token is
// resumed inline by whichever thread signals it. The executor hand-off is covered by the
// integration suite, which has a headless context.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "task.h"
#include "token.h"

using namespace kor;

namespace {

Task<void> AwaitThenCount(Token token, std::atomic<int>& resumed) {
    co_await token;
    resumed.fetch_add(1);
}

TEST(Token, DefaultTokenIsAlwaysReady) {
    const Token none;
    EXPECT_TRUE(none.ready());
    EXPECT_EQ(none.value(), 0u);
    none.wait();    // returns at once
    none.signal();  // does nothing
}

TEST(Token, SignalMakesItReady) {
    const Token t = Token::Create();
    EXPECT_FALSE(t.ready());
    t.signal();
    EXPECT_TRUE(t.ready());
    t.wait();
}

TEST(Token, CopiesShareTheEvent) {
    const Token a = Token::Create();
    const Token b = a;
    EXPECT_EQ(a, b);
    b.signal();
    EXPECT_TRUE(a.ready());
    EXPECT_NE(a, Token::Create());
}

TEST(Timeline, ReservesIncreasingValues) {
    Timeline tl;
    EXPECT_EQ(tl.next().value(), 1u);
    EXPECT_EQ(tl.next().value(), 2u);
    EXPECT_EQ(tl.next().value(), 3u);
    EXPECT_EQ(tl.value(), 0u);
}

TEST(Timeline, AtNamesTheSameEventWithoutReserving) {
    Timeline tl;
    EXPECT_EQ(tl.at(3), tl.at(3));
    tl.at(3).signal();
    EXPECT_TRUE(tl.at(2).ready());
    EXPECT_FALSE(tl.at(4).ready());
    EXPECT_EQ(tl.next().value(), 1u);  // at() reserved nothing
}

TEST(Timeline, ReachingAValueReachesEveryEarlierOne) {
    Timeline tl;
    const Token first  = tl.next();
    const Token second = tl.next();
    const Token third  = tl.next();

    second.signal();
    EXPECT_TRUE(first.ready());
    EXPECT_TRUE(second.ready());
    EXPECT_FALSE(third.ready());
    EXPECT_EQ(tl.value(), 2u);
}

TEST(Timeline, SignallingAnAlreadyReachedValueDoesNotGoBackwards) {
    Timeline tl;
    const Token first  = tl.next();
    const Token second = tl.next();

    second.signal();
    first.signal();   // already reached: a warning, not a rewind
    second.signal();  // twice: likewise
    EXPECT_EQ(tl.value(), 2u);
}

TEST(Token, WaitBlocksUntilAnotherThreadSignals) {
    const Token t = Token::Create();
    std::atomic<bool> signalled{false};

    std::thread producer([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        signalled.store(true);
        t.signal();
    });

    t.wait();
    EXPECT_TRUE(signalled.load());
    producer.join();
}

TEST(Token, AwaitingAReadyTokenDoesNotSuspend) {
    const Token t = Token::Create();
    t.signal();

    std::atomic<int> resumed{0};
    auto task = AwaitThenCount(t, resumed);
    EXPECT_TRUE(task.done());
    EXPECT_EQ(resumed.load(), 1);
    EXPECT_TRUE(task.take().has_value());
}

TEST(Token, AwaitSuspendsUntilSignalled) {
    const Token t = Token::Create();

    std::atomic<int> resumed{0};
    auto task = AwaitThenCount(t, resumed);
    EXPECT_FALSE(task.done());
    EXPECT_EQ(resumed.load(), 0);

    t.signal();  // no executors: resumes the coroutine right here
    EXPECT_TRUE(task.done());
    EXPECT_EQ(resumed.load(), 1);
}

TEST(Token, AwaitingTheTemporaryOfAnExpiredTokenStillWorks) {
    // The awaiter keeps its own copy, so the coroutine is fine even once every other copy is gone.
    std::atomic<int> resumed{0};
    Timeline tl;
    auto task = AwaitThenCount(tl.next(), resumed);
    EXPECT_FALSE(task.done());

    tl.next().signal();  // value 2 — reaches the awaited value 1 too
    EXPECT_TRUE(task.done());
}

TEST(Timeline, ResumesOnlyTheWaitersWhoseValueWasReached) {
    Timeline tl;
    const Token t1 = tl.next(), t2 = tl.next(), t3 = tl.next();

    std::atomic<int> r1{0}, r2{0}, r3{0};
    auto a = AwaitThenCount(t1, r1);
    auto b = AwaitThenCount(t2, r2);
    auto c = AwaitThenCount(t3, r3);

    t2.signal();
    EXPECT_EQ(r1.load(), 1);
    EXPECT_EQ(r2.load(), 1);
    EXPECT_EQ(r3.load(), 0);

    t3.signal();
    EXPECT_EQ(r3.load(), 1);
}

TEST(Token, ManyWaitersOnOneEventAllResume) {
    const Token t = Token::Create();
    std::atomic<int> resumed{0};

    std::vector<Task<void>> tasks;
    for (int i = 0; i < 64; ++i) tasks.push_back(AwaitThenCount(t, resumed));
    EXPECT_EQ(resumed.load(), 0);

    t.signal();
    EXPECT_EQ(resumed.load(), 64);
}

struct CountsDestruction {
    std::atomic<int>& destroyed;
    ~CountsDestruction() { destroyed.fetch_add(1); }
};

Task<void> WaitHoldingALocal(Token token, std::atomic<int>& destroyed, std::atomic<int>& resumed) {
    CountsDestruction local{destroyed};
    co_await token;
    resumed.fetch_add(1);
}

// A Task destroyed while its coroutine waits destroys the coroutine. The timeline must forget it:
// resuming it later would run a frame that no longer exists.
TEST(Token, ACoroutineDestroyedWhileWaitingIsNeverResumed) {
    const Token t = Token::Create();
    std::atomic<int> destroyed{0}, resumed{0};
    {
        auto task = WaitHoldingALocal(t, destroyed, resumed);
        ASSERT_FALSE(task.done());
    }
    EXPECT_EQ(destroyed.load(), 1) << "destroying the Task should have destroyed the waiting coroutine";

    t.signal();
    EXPECT_EQ(resumed.load(), 0) << "a destroyed coroutine was resumed";
}

TEST(Token, OtherWaitersStillResumeAfterOneIsDestroyed) {
    const Token t = Token::Create();
    std::atomic<int> destroyed{0}, resumed{0};
    auto kept = WaitHoldingALocal(t, destroyed, resumed);
    {
        auto dropped = WaitHoldingALocal(t, destroyed, resumed);
    }
    t.signal();
    EXPECT_EQ(resumed.load(), 1);
    EXPECT_TRUE(kept.done());
}

// The window that matters: a signal landing between a coroutine's await_ready and its
// registration. Losing one of these parks the coroutine forever, so race them many times.
TEST(Token, ConcurrentSignalAndAwaitNeverLosesAWakeup) {
    constexpr int kRounds = 2000;
    std::atomic<int> resumed{0};

    for (int i = 0; i < kRounds; ++i) {
        const Token t = Token::Create();
        std::atomic<bool> go{false};

        std::thread signaller([&] {
            while (!go.load(std::memory_order_acquire)) {}
            t.signal();
        });

        go.store(true, std::memory_order_release);
        auto task = AwaitThenCount(t, resumed);
        signaller.join();
        ASSERT_TRUE(task.done()) << "lost wakeup in round " << i;
    }
    EXPECT_EQ(resumed.load(), kRounds);
}

}  // namespace
