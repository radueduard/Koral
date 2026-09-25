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
    EXPECT_TRUE(none.Ready());
    EXPECT_EQ(none.Value(), 0u);
    none.Wait();    // returns at once
    none.Signal();  // does nothing
}

TEST(Token, SignalMakesItReady) {
    const Token t = Token::Create();
    EXPECT_FALSE(t.Ready());
    t.Signal();
    EXPECT_TRUE(t.Ready());
    t.Wait();
}

TEST(Token, CopiesShareTheEvent) {
    const Token a = Token::Create();
    const Token b = a;
    EXPECT_EQ(a, b);
    b.Signal();
    EXPECT_TRUE(a.Ready());
    EXPECT_NE(a, Token::Create());
}

TEST(Timeline, ReservesIncreasingValues) {
    Timeline tl;
    EXPECT_EQ(tl.Next().Value(), 1u);
    EXPECT_EQ(tl.Next().Value(), 2u);
    EXPECT_EQ(tl.Next().Value(), 3u);
    EXPECT_EQ(tl.Value(), 0u);
}

TEST(Timeline, AtNamesTheSameEventWithoutReserving) {
    Timeline tl;
    EXPECT_EQ(tl.At(3), tl.At(3));
    tl.At(3).Signal();
    EXPECT_TRUE(tl.At(2).Ready());
    EXPECT_FALSE(tl.At(4).Ready());
    EXPECT_EQ(tl.Next().Value(), 1u);  // At() reserved nothing
}

TEST(Timeline, ReachingAValueReachesEveryEarlierOne) {
    Timeline tl;
    const Token first  = tl.Next();
    const Token second = tl.Next();
    const Token third  = tl.Next();

    second.Signal();
    EXPECT_TRUE(first.Ready());
    EXPECT_TRUE(second.Ready());
    EXPECT_FALSE(third.Ready());
    EXPECT_EQ(tl.Value(), 2u);
}

TEST(Timeline, SignallingAnAlreadyReachedValueDoesNotGoBackwards) {
    Timeline tl;
    const Token first  = tl.Next();
    const Token second = tl.Next();

    second.Signal();
    first.Signal();   // already reached: a warning, not a rewind
    second.Signal();  // twice: likewise
    EXPECT_EQ(tl.Value(), 2u);
}

TEST(Token, WaitBlocksUntilAnotherThreadSignals) {
    const Token t = Token::Create();
    std::atomic<bool> signalled{false};

    std::thread producer([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        signalled.store(true);
        t.Signal();
    });

    t.Wait();
    EXPECT_TRUE(signalled.load());
    producer.join();
}

TEST(Token, AwaitingAReadyTokenDoesNotSuspend) {
    const Token t = Token::Create();
    t.Signal();

    std::atomic<int> resumed{0};
    auto task = AwaitThenCount(t, resumed);
    EXPECT_TRUE(task.Done());
    EXPECT_EQ(resumed.load(), 1);
    EXPECT_TRUE(task.Take().has_value());
}

TEST(Token, AwaitSuspendsUntilSignalled) {
    const Token t = Token::Create();

    std::atomic<int> resumed{0};
    auto task = AwaitThenCount(t, resumed);
    EXPECT_FALSE(task.Done());
    EXPECT_EQ(resumed.load(), 0);

    t.Signal();  // no executors: resumes the coroutine right here
    EXPECT_TRUE(task.Done());
    EXPECT_EQ(resumed.load(), 1);
}

TEST(Token, AwaitingTheTemporaryOfAnExpiredTokenStillWorks) {
    // The awaiter keeps its own copy, so the coroutine is fine even once every other copy is gone.
    std::atomic<int> resumed{0};
    Timeline tl;
    auto task = AwaitThenCount(tl.Next(), resumed);
    EXPECT_FALSE(task.Done());

    tl.Next().Signal();  // value 2 — reaches the awaited value 1 too
    EXPECT_TRUE(task.Done());
}

TEST(Timeline, ResumesOnlyTheWaitersWhoseValueWasReached) {
    Timeline tl;
    const Token t1 = tl.Next(), t2 = tl.Next(), t3 = tl.Next();

    std::atomic<int> r1{0}, r2{0}, r3{0};
    auto a = AwaitThenCount(t1, r1);
    auto b = AwaitThenCount(t2, r2);
    auto c = AwaitThenCount(t3, r3);

    t2.Signal();
    EXPECT_EQ(r1.load(), 1);
    EXPECT_EQ(r2.load(), 1);
    EXPECT_EQ(r3.load(), 0);

    t3.Signal();
    EXPECT_EQ(r3.load(), 1);
}

TEST(Token, ManyWaitersOnOneEventAllResume) {
    const Token t = Token::Create();
    std::atomic<int> resumed{0};

    std::vector<Task<void>> tasks;
    for (int i = 0; i < 64; ++i) tasks.push_back(AwaitThenCount(t, resumed));
    EXPECT_EQ(resumed.load(), 0);

    t.Signal();
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
        ASSERT_FALSE(task.Done());
    }
    EXPECT_EQ(destroyed.load(), 1) << "destroying the Task should have destroyed the waiting coroutine";

    t.Signal();
    EXPECT_EQ(resumed.load(), 0) << "a destroyed coroutine was resumed";
}

TEST(Token, OtherWaitersStillResumeAfterOneIsDestroyed) {
    const Token t = Token::Create();
    std::atomic<int> destroyed{0}, resumed{0};
    auto kept = WaitHoldingALocal(t, destroyed, resumed);
    {
        auto dropped = WaitHoldingALocal(t, destroyed, resumed);
    }
    t.Signal();
    EXPECT_EQ(resumed.load(), 1);
    EXPECT_TRUE(kept.Done());
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
            t.Signal();
        });

        go.store(true, std::memory_order_release);
        auto task = AwaitThenCount(t, resumed);
        signaller.join();
        ASSERT_TRUE(task.Done()) << "lost wakeup in round " << i;
    }
    EXPECT_EQ(resumed.load(), kRounds);
}

}  // namespace
