// Integration tests for where a coroutine resumes after `co_await token`.
//
// These need the executors, which only exist once a context does — the headless one GpuEnvironment
// brings up. The token logic itself is covered by tests/test_token.cpp.

#include "gpu_fixture.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include "buffer.h"
#include "commandBuffer.h"
#include "context.h"
#include "log.h"
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

// ---- GPU-backed tokens -----------------------------------------------------------------------
//
// Handing a token to Submit() gives its timeline a semaphore; from then on the GPU can move it,
// and the reactor thread is what notices.

namespace {

std::uint64_t logMark() {
    const auto history = kor::log::history();
    return history.empty() ? 0ull : history.back().sequence;
}

void expectNoValidationErrorsSince(const std::uint64_t since) {
    for (const auto& record : kor::log::historySince(since)) {
        if (record.level != kor::log::Level::eError) continue;
        EXPECT_EQ(record.message.find("VUID"), std::string::npos) << record.message;
    }
}

std::unique_ptr<CommandBuffer> recordEmpty() {
    auto cb = CommandBuffer::Create(CommandBuffer::Usage::eCompute);
    cb->Begin();
    cb->End();
    return cb;
}

Task<void> AwaitOnBackgroundThenSignal(Token token, Token done) {
    co_await Context::SwitchToBackgroundThread();
    co_await token;
    done.signal();
}

TEST_F(TokenExecutorTest, AGpuSignalResumesAParkedCoroutine) {
    const auto since = logMark();
    const Token gpuDone = Token::Create();
    const Token resumed = Token::Create();

    auto task = AwaitOnBackgroundThenSignal(gpuDone, resumed);
    const auto cb = recordEmpty();
    ASSERT_TRUE(cb->Submit({.signal = {gpuDone}}));

    resumed.wait();   // only the reactor can get us here
    EXPECT_TRUE(gpuDone.ready());
    cb->WaitForFence();
    expectNoValidationErrorsSince(since);
}

TEST_F(TokenExecutorTest, TheGpuWaitsForATokenTheCpuSignalsLater) {
    const auto since = logMark();
    const Token cpuGo   = Token::Create();
    const Token gpuDone = Token::Create();

    const auto cb = recordEmpty();
    ASSERT_TRUE(cb->Submit({.waitFor = {cpuGo}, .signal = {gpuDone}}));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(gpuDone.ready()) << "the GPU ran ahead of a token nobody had signalled";

    cpuGo.signal();
    gpuDone.wait();
    EXPECT_TRUE(gpuDone.ready());
    cb->WaitForFence();
    expectNoValidationErrorsSince(since);
}

TEST_F(TokenExecutorTest, SubmissionsChainThroughATokenWithoutTheCpu) {
    const auto since = logMark();
    const Token first = Token::Create(), second = Token::Create();

    const auto a = recordEmpty();
    const auto b = recordEmpty();
    // In order: both land on the same queue, which runs its submissions in turn, so b waiting on
    // something only a *later* submission signals would stall the queue for good.
    ASSERT_TRUE(a->Submit({.signal = {first}}));
    ASSERT_TRUE(b->Submit({.waitFor = {first}, .signal = {second}}));

    second.wait();
    EXPECT_TRUE(first.ready());
    a->WaitForFence();
    b->WaitForFence();
    expectNoValidationErrorsSince(since);
}

TEST_F(TokenExecutorTest, ABlockingWaitStartedBeforeTheGpuGotTheTokenStillReturns) {
    const Token token = Token::Create();
    std::atomic<bool> returned{false};

    // Parked on the CPU side, before the token has any semaphore to wait on.
    std::thread waiter([&] { token.wait(); returned.store(true); });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    ASSERT_FALSE(returned.load());

    const auto cb = recordEmpty();
    ASSERT_TRUE(cb->Submit({.signal = {token}}));
    waiter.join();
    EXPECT_TRUE(returned.load());
    cb->WaitForFence();
}

TEST_F(TokenExecutorTest, ATimelineCanBeSignalledByTheGpuFrameAfterFrame) {
    const auto since = logMark();
    Timeline frames;
    const auto cb = CommandBuffer::Create(CommandBuffer::Usage::eCompute);

    for (int n = 1; n <= 8; ++n) {
        cb->Begin();
        cb->End();
        // Fire and forget: the token is a temporary. The buffer keeps its semaphore alive.
        ASSERT_TRUE(cb->Submit({.signal = {frames.next()}}));
        frames.at(n).wait();
    }
    EXPECT_EQ(frames.value(), 8u);
    expectNoValidationErrorsSince(since);
}

TEST_F(TokenExecutorTest, SignallingATokenThatAlreadyHappenedIsAnError) {
    const Token done = Token::Create();
    done.signal();

    const auto cb = recordEmpty();
    EXPECT_FALSE(cb->Submit({.signal = {done}}));
    cb->WaitForFence();
}

TEST_F(TokenExecutorTest, TheCpuCanSignalAGpuBackedTimelineToo) {
    Timeline tl;
    const auto cb = recordEmpty();
    ASSERT_TRUE(cb->Submit({.signal = {tl.next()}}));   // value 1, and the timeline now has a semaphore
    tl.at(1).wait();

    const Token cpuSide = tl.next();                     // value 2, from the CPU
    std::atomic<int> resumed{0};
    auto task = [](Token t, std::atomic<int>& r) -> Task<void> {
        co_await Context::SwitchToBackgroundThread();
        co_await t;
        r.store(1);
    }(cpuSide, resumed);

    cpuSide.signal();
    tl.at(2).wait();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!task.done() && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    EXPECT_EQ(resumed.load(), 1);
    cb->WaitForFence();
}

}  // namespace

// ---- SingleTimeCommand returns a token ---------------------------------------------------------

namespace {

Resource<Buffer> makeDeviceBuffer(const std::vector<int>& data) {
    Buffer::Builder<int> b;
    b.setData(data);
    b.setUsage(Buffer::Usage::eStorage | Buffer::Usage::eTransferSrc | Buffer::Usage::eTransferDst);
    b.setType(Buffer::Type::eDeviceLocal);
    return b.build();
}

Task<void> AwaitOneOff(std::function<void(CommandBuffer&)> work, Token resumed) {
    co_await Context::SwitchToBackgroundThread();
    co_await CommandBuffer::SingleTimeCommand(work);
    resumed.signal();
}

TEST_F(TokenExecutorTest, SingleTimeCommandReturnsATokenForItsCompletion) {
    const auto since = logMark();
    auto buffer = makeDeviceBuffer(std::vector<int>(64, 7));
    ASSERT_TRUE(static_cast<bool>(buffer));

    const Token done = CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.ClearBuffer(buffer);
    });
    done.wait();
    EXPECT_TRUE(done.ready());
    EXPECT_EQ(buffer->Read<int>(), std::vector<int>(64, 0));
    expectNoValidationErrorsSince(since);
}

TEST_F(TokenExecutorTest, ACoroutineCanAwaitAOneOff) {
    const auto since = logMark();
    auto buffer = makeDeviceBuffer(std::vector<int>(64, 7));
    ASSERT_TRUE(static_cast<bool>(buffer));
    const Token resumed = Token::Create();

    // Recorded on a pool thread. Command pools are not per-thread yet, so this is only sound
    // because the main thread records nothing meanwhile — it is blocked on `resumed` below.
    auto task = AwaitOneOff([&](CommandBuffer& cb) { cb.ClearBuffer(buffer); }, resumed);
    resumed.wait();
    EXPECT_EQ(buffer->Read<int>(), std::vector<int>(64, 0));
    expectNoValidationErrorsSince(since);
}

// Fire and forget: nothing is kept that could keep a command buffer alive — a Token holds the
// timeline, not the submission — so only the retire list stands between each one and being freed
// while the GPU runs it. That is exactly what the validation layer reports, so a clean log is the
// evidence. The tokens are kept only so the buffer can outlive every clear aimed at it: two
// one-offs are unordered, so waiting on the last one would say nothing about the others.
TEST_F(TokenExecutorTest, FireAndForgetOneOffsAreReleasedOnlyOnceTheGpuIsDone) {
    const auto since = logMark();
    auto buffer = makeDeviceBuffer(std::vector<int>(4096, 7));
    ASSERT_TRUE(static_cast<bool>(buffer));

    std::vector<Token> clears;
    for (int i = 0; i < 64; ++i)
        clears.push_back(CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) { cb.ClearBuffer(buffer); }));

    for (const auto& t : clears) t.wait();
    expectNoValidationErrorsSince(since);
}

}  // namespace
