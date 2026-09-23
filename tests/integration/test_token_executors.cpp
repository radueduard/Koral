// Integration tests for where a coroutine resumes after `co_await token`.
//
// These need the executors, which only exist once a context does — the headless one GpuEnvironment
// brings up. The token logic itself is covered by tests/test_token.cpp.

#include "gpu_fixture.h"

#include <atomic>
#include <chrono>
#include <thread>

#include "context.h"
#include "task.h"
#include "token.h"

using namespace kor;

namespace {

class TokenExecutorTest : public GpuTest {};

Task<void> AwaitAndRecordThread(Token token, std::thread::id& resumedOn, Token done) {
    co_await token;
    resumedOn = std::this_thread::get_id();
    done.signal();
}

TEST_F(TokenExecutorTest, AwaitOnTheMainThreadResumesOnTheNextDrain) {
    const auto mainThread = std::this_thread::get_id();
    const Token token = Token::Create();
    const Token done  = Token::Create();
    std::thread::id resumedOn;

    auto task = AwaitAndRecordThread(token, resumedOn, done);
    ASSERT_FALSE(task.done());

    std::thread([&] { token.signal(); }).join();
    EXPECT_FALSE(task.done()) << "a main-thread waiter must not be resumed on the signalling thread";

    Context::DrainMainThread();
    EXPECT_TRUE(task.done());
    EXPECT_EQ(resumedOn, mainThread);
}

Task<void> AwaitOnBackground(Token token, std::thread::id& resumedOn, Token done) {
    co_await Context::SwitchToBackgroundThread();
    co_await token;
    resumedOn = std::this_thread::get_id();
    done.signal();
}

TEST_F(TokenExecutorTest, AwaitOnABackgroundThreadResumesOnThePool) {
    const Token token = Token::Create();
    const Token done  = Token::Create();
    std::thread::id resumedOn;

    auto task = AwaitOnBackground(token, resumedOn, done);
    token.signal();  // from the main thread
    done.wait();

    EXPECT_NE(resumedOn, std::this_thread::get_id());
    EXPECT_NE(resumedOn, std::thread::id{});
}

struct CountsDestruction {
    std::atomic<int>& destroyed;
    ~CountsDestruction() { destroyed.fetch_add(1); }
};

Task<void> WaitOnTheMainThread(Token token, std::atomic<int>& destroyed, std::atomic<int>& resumed) {
    CountsDestruction local{destroyed};
    co_await token;
    resumed.fetch_add(1);
}

// The narrower window: the token has been signalled, so the resume is already sitting in the main
// thread's queue, and only then is the Task destroyed. The queued resume must notice.
TEST_F(TokenExecutorTest, ACoroutineDestroyedWhileItsResumeIsQueuedIsSkipped) {
    const Token token = Token::Create();
    std::atomic<int> destroyed{0}, resumed{0};
    {
        auto task = WaitOnTheMainThread(token, destroyed, resumed);
        ASSERT_FALSE(task.done());
        token.signal();            // queues the resume on the main thread; nothing runs yet
        ASSERT_FALSE(task.done());
    }                              // destroyed with its resume still queued
    EXPECT_EQ(destroyed.load(), 1);

    Context::DrainMainThread();    // runs the queued resume, which must find it cancelled
    EXPECT_EQ(resumed.load(), 0) << "a destroyed coroutine was resumed from the executor's queue";
}

// The long-lived-loop shape from the v2 design: a background coroutine that parks on a request
// timeline, does a step, and reports on a done timeline. It holds no thread while parked.
Task<void> StepLoop(Timeline& requests, Timeline& done, const int steps, std::atomic<int>& state) {
    co_await Context::SwitchToBackgroundThread();
    for (int n = 1; n <= steps; ++n) {
        co_await requests.at(n);
        state.store(n);
        done.at(n).signal();
    }
}

TEST_F(TokenExecutorTest, TwoTimelinesDriveARecurringRendezvous) {
    constexpr int kSteps = 100;
    Timeline requests, done;
    std::atomic<int> state{0};

    auto loop = StepLoop(requests, done, kSteps, state);
    for (int n = 1; n <= kSteps; ++n) {
        requests.at(n).signal();
        done.at(n).wait();
        ASSERT_EQ(state.load(), n);
    }

    // The loop signalled its last step before returning; give it a moment to reach final_suspend.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!loop.done() && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    EXPECT_TRUE(loop.done());
}

}  // namespace
