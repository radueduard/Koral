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
#include "image.h"
#include "log.h"
#include "task.h"
#include "token.h"
#include "src/backends/vulkan/vulkanContext.h" // DestroyWhenUnused, for the deferred-destruction tests

using namespace kor;

namespace {

class TokenExecutorTest : public GpuTest {};

Task<void> AwaitAndRecordThread(Token token, std::thread::id& resumedOn, Token done) {
    co_await token;
    resumedOn = std::this_thread::get_id();
    done.Signal();
}

TEST_F(TokenExecutorTest, AwaitOnTheMainThreadResumesOnTheNextDrain) {
    const auto mainThread = std::this_thread::get_id();
    const Token token = Token::Create();
    const Token done  = Token::Create();
    std::thread::id resumedOn;

    auto task = AwaitAndRecordThread(token, resumedOn, done);
    ASSERT_FALSE(task.Done());

    std::thread([&] { token.Signal(); }).join();
    EXPECT_FALSE(task.Done()) << "a main-thread waiter must not be resumed on the signalling thread";

    Context::DrainMainThread();
    EXPECT_TRUE(task.Done());
    EXPECT_EQ(resumedOn, mainThread);
}

Task<void> AwaitOnBackground(Token token, std::thread::id& resumedOn, Token done) {
    co_await Context::SwitchToBackgroundThread();
    co_await token;
    resumedOn = std::this_thread::get_id();
    done.Signal();
}

TEST_F(TokenExecutorTest, AwaitOnABackgroundThreadResumesOnThePool) {
    const Token token = Token::Create();
    const Token done  = Token::Create();
    std::thread::id resumedOn;

    auto task = AwaitOnBackground(token, resumedOn, done);
    token.Signal();  // from the main thread
    done.Wait();

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
        ASSERT_FALSE(task.Done());
        token.Signal();            // queues the resume on the main thread; nothing runs yet
        ASSERT_FALSE(task.Done());
    }                              // destroyed with its resume still queued
    EXPECT_EQ(destroyed.load(), 1);

    Context::DrainMainThread();    // runs the queued resume, which must find it cancelled
    EXPECT_EQ(resumed.load(), 0) << "a destroyed coroutine was resumed from the executor's queue";
}

Task<void> WorkOnBackground(std::atomic<int>& done) {
    co_await Context::SwitchToBackgroundThread();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    done.fetch_add(1);
}

// The frame graph's shape: fan work out to the pool, then block the main thread until all of it is
// done. WhenAll must not route its own resumes through the main thread's queue, or the main thread
// — blocked right here — would be waiting on itself.
TEST_F(TokenExecutorTest, WaitingOnWhenAllFromTheMainThreadDoesNotDeadlock) {
    std::atomic<int> done{0};
    std::vector<Task<void>> tasks;
    for (int i = 0; i < 8; ++i) tasks.push_back(WorkOnBackground(done));
    auto all = WhenAll(std::move(tasks));

    // A regression would hang here forever; after two seconds the watchdog notes it and runs the
    // main queue itself, so the suite fails instead of hanging.
    std::atomic<bool> finished{false}, deadlocked{false};
    std::thread watchdog([&] {
        for (int i = 0; i < 200 && !finished.load(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (!finished.load()) {
            deadlocked.store(true);
            Context::DrainMainThread();
        }
    });
    all.Wait();
    finished.store(true);
    watchdog.join();
    EXPECT_FALSE(deadlocked.load()) << "WhenAll needed the main thread it was blocking";
    EXPECT_EQ(done.load(), 8);
    EXPECT_TRUE(all.Take().has_value());
}

// The long-lived-loop shape from the v2 design: a background coroutine that parks on a request
// timeline, does a step, and reports on a done timeline. It holds no thread while parked.
Task<void> StepLoop(Timeline& requests, Timeline& done, const int steps, std::atomic<int>& state) {
    co_await Context::SwitchToBackgroundThread();
    for (int n = 1; n <= steps; ++n) {
        co_await requests.At(n);
        state.store(n);
        done.At(n).Signal();
    }
}

TEST_F(TokenExecutorTest, TwoTimelinesDriveARecurringRendezvous) {
    constexpr int kSteps = 100;
    Timeline requests, done;
    std::atomic<int> state{0};

    auto loop = StepLoop(requests, done, kSteps, state);
    for (int n = 1; n <= kSteps; ++n) {
        requests.At(n).Signal();
        done.At(n).Wait();
        ASSERT_EQ(state.load(), n);
    }

    // The loop signalled its last step before returning; give it a moment to reach final_suspend.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!loop.Done() && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    EXPECT_TRUE(loop.Done());
}

}  // namespace

// ---- GPU-backed tokens -----------------------------------------------------------------------
//
// Handing a token to Submit() gives its timeline a semaphore; from then on the GPU can move it,
// and the reactor thread is what notices.

namespace {

std::uint64_t logMark() {
    const auto history = kor::log::History();
    return history.empty() ? 0ull : history.back().sequence;
}

void expectNoValidationErrorsSince(const std::uint64_t since) {
    for (const auto& record : kor::log::HistorySince(since)) {
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
    done.Signal();
}

TEST_F(TokenExecutorTest, AGpuSignalResumesAParkedCoroutine) {
    const auto since = logMark();
    const Token gpuDone = Token::Create();
    const Token resumed = Token::Create();

    auto task = AwaitOnBackgroundThenSignal(gpuDone, resumed);
    const auto cb = recordEmpty();
    ASSERT_TRUE(cb->Submit({.signal = {gpuDone}}));

    resumed.Wait();   // only the reactor can get us here
    EXPECT_TRUE(gpuDone.Ready());
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
    EXPECT_FALSE(gpuDone.Ready()) << "the GPU ran ahead of a token nobody had signalled";

    cpuGo.Signal();
    gpuDone.Wait();
    EXPECT_TRUE(gpuDone.Ready());
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

    second.Wait();
    EXPECT_TRUE(first.Ready());
    a->WaitForFence();
    b->WaitForFence();
    expectNoValidationErrorsSince(since);
}

TEST_F(TokenExecutorTest, ABlockingWaitStartedBeforeTheGpuGotTheTokenStillReturns) {
    const Token token = Token::Create();
    std::atomic<bool> returned{false};

    // Parked on the CPU side, before the token has any semaphore to wait on.
    std::thread waiter([&] { token.Wait(); returned.store(true); });
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
        ASSERT_TRUE(cb->Submit({.signal = {frames.Next()}}));
        frames.At(n).Wait();
    }
    EXPECT_EQ(frames.Value(), 8u);
    expectNoValidationErrorsSince(since);
}

TEST_F(TokenExecutorTest, SignallingATokenThatAlreadyHappenedIsAnError) {
    const Token done = Token::Create();
    done.Signal();

    const auto cb = recordEmpty();
    EXPECT_FALSE(cb->Submit({.signal = {done}}));
    cb->WaitForFence();
}

TEST_F(TokenExecutorTest, TheCpuCanSignalAGpuBackedTimelineToo) {
    Timeline tl;
    const auto cb = recordEmpty();
    ASSERT_TRUE(cb->Submit({.signal = {tl.Next()}}));   // value 1, and the timeline now has a semaphore
    tl.At(1).Wait();

    const Token cpuSide = tl.Next();                     // value 2, from the CPU
    std::atomic<int> resumed{0};
    auto task = [](Token t, std::atomic<int>& r) -> Task<void> {
        co_await Context::SwitchToBackgroundThread();
        co_await t;
        r.store(1);
    }(cpuSide, resumed);

    cpuSide.Signal();
    tl.At(2).Wait();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!task.Done() && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    EXPECT_EQ(resumed.load(), 1);
    cb->WaitForFence();
}

}  // namespace

// ---- SingleTimeCommand returns a token ---------------------------------------------------------

namespace {

Resource<Buffer> makeDeviceBuffer(const std::vector<int>& data) {
    Buffer::Builder<int> b;
    b.SetData(data);
    b.SetUsage(Buffer::Usage::eStorage | Buffer::Usage::eTransferSrc | Buffer::Usage::eTransferDst);
    b.SetType(Buffer::Type::eDeviceLocal);
    return b.Build();
}

Task<void> AwaitOneOff(std::function<void(CommandBuffer&)> work, Token resumed) {
    co_await Context::SwitchToBackgroundThread();
    co_await CommandBuffer::SingleTimeCommand(work);
    resumed.Signal();
}

TEST_F(TokenExecutorTest, SingleTimeCommandReturnsATokenForItsCompletion) {
    const auto since = logMark();
    auto buffer = makeDeviceBuffer(std::vector<int>(64, 7));
    ASSERT_TRUE(static_cast<bool>(buffer));

    const Token done = CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.ClearBuffer(buffer);
    });
    done.Wait();
    EXPECT_TRUE(done.Ready());
    EXPECT_EQ(buffer->Read<int>(), std::vector<int>(64, 0));
    expectNoValidationErrorsSince(since);
}

TEST_F(TokenExecutorTest, ACoroutineCanAwaitAOneOff) {
    const auto since = logMark();
    auto buffer = makeDeviceBuffer(std::vector<int>(64, 7));
    ASSERT_TRUE(static_cast<bool>(buffer));
    const Token resumed = Token::Create();

    // Recorded on a pool thread, which is fine: every command buffer has a pool of its own.
    auto task = AwaitOneOff([&](CommandBuffer& cb) { cb.ClearBuffer(buffer); }, resumed);
    resumed.Wait();
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

    for (const auto& t : clears) t.Wait();
    expectNoValidationErrorsSince(since);
}

}  // namespace

// ---- Recording and submitting from several threads ---------------------------------------------
//
// Every thread records its own command buffers, submits them, and fires one-offs, all at once. The
// validation layer's thread-safety checks report a pool or a queue touched from two threads at a
// time ("THREADING ERROR"), so a clean log is the evidence.

namespace {

TEST_F(TokenExecutorTest, SeveralThreadsRecordAndSubmitAtOnce) {
    constexpr int kThreads = 8;
    constexpr int kRounds = 40;
    const auto since = logMark();

    // Resources are built up front, on this thread: the subject is recording and submission.
    std::vector<Resource<Buffer>> buffers;
    for (int i = 0; i < kThreads; ++i) {
        buffers.push_back(makeDeviceBuffer(std::vector<int>(1024, 7)));
        ASSERT_TRUE(static_cast<bool>(buffers.back()));
    }

    std::atomic<bool> go{false};
    std::atomic<int> failures{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
            const auto& buffer = buffers[t];
            for (int round = 0; round < kRounds; ++round) {
                auto cb = CommandBuffer::Create(CommandBuffer::Usage::eCompute);
                cb->Begin();
                cb->ClearBuffer(buffer);
                cb->End();
                const Token done = Token::Create();
                if (!cb->Submit({.signal = {done}})) failures.fetch_add(1);
                done.Wait();

                CommandBuffer::SingleTimeCommand([&](CommandBuffer& one) { one.ClearBuffer(buffer); }).Wait();
            }
        });
    }
    go.store(true, std::memory_order_release);
    for (auto& thread : threads) thread.join();

    EXPECT_EQ(failures.load(), 0);
    for (const auto& record : kor::log::HistorySince(since)) {
        if (record.level != kor::log::Level::eError) continue;
        EXPECT_EQ(record.message.find("THREADING"), std::string::npos) << record.message;
        EXPECT_EQ(record.message.find("VUID"), std::string::npos) << record.message;
    }
}

// One image recorded into by several threads at once, each submitting without End(): Submit ends and submits in
// one step, in turn with the others, so every command buffer is resolved against the one that is ahead of it on
// the queue — and every barrier is right, whichever thread gets there first.
TEST_F(TokenExecutorTest, SeveralThreadsRecordIntoTheSameImage) {
    constexpr int kThreads = 8;
    constexpr int kRounds = 30;
    const auto since = logMark();
    auto image = Image::Builder{}.SetType(Image::Type::e2D).SetFormat(Image::Format::eRGBA8_UNORM).SetExtent(glm::uvec2{64, 64})
                     .SetUsage(Image::Usage::eTransferDst | Image::Usage::eTransferSrc | Image::Usage::eSampled).Build();
    ASSERT_TRUE(static_cast<bool>(image));

    std::atomic<bool> go{false};
    std::atomic<int> failures{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
            for (int round = 0; round < kRounds; ++round) {
                auto cb = CommandBuffer::Create(CommandBuffer::Usage::eGraphics);
                cb->Begin();
                cb->ClearColorImage(image, glm::vec4(static_cast<float>(t) / kThreads, 0.f, 0.f, 1.f));
                // No End(): Submit ends it in the same step, in turn with the other threads, so it is resolved
                // against the work ahead of it on the queue — whichever thread got there first.
                const Token done = Token::Create();
                if (!cb->Submit({.signal = {done}})) failures.fetch_add(1);
                done.Wait();
            }
        });
    }
    go.store(true, std::memory_order_release);
    for (auto& thread : threads) thread.join();

    EXPECT_EQ(failures.load(), 0);
    for (const auto& record : kor::log::HistorySince(since)) {
        if (record.level != kor::log::Level::eError) continue;
        EXPECT_EQ(record.message.find("THREADING"), std::string::npos) << record.message;
        EXPECT_EQ(record.message.find("VUID"), std::string::npos) << record.message;
    }
}

// The trap the rule above exists for, sprung on purpose: two command buffers sharing an image, ended in one order and
// submitted in the other. The second was resolved against the first, which is not ahead of it on the queue — Submit
// says so rather than letting its barriers start from the wrong state in silence.
TEST_F(TokenExecutorTest, SubmittingInAnotherOrderThanEndedIsReported) {
    auto image = Image::Builder{}.SetType(Image::Type::e2D).SetFormat(Image::Format::eRGBA8_UNORM).SetExtent(glm::uvec2{8, 8})
                     .SetUsage(Image::Usage::eTransferDst | Image::Usage::eSampled).Build();
    auto first = CommandBuffer::Create(CommandBuffer::Usage::eGraphics);
    auto second = CommandBuffer::Create(CommandBuffer::Usage::eGraphics);
    first->Begin(); first->ClearColorImage(image); first->End();
    second->Begin(); second->ClearColorImage(image); second->End();
    ASSERT_TRUE(first->Ok() && second->Ok());

    const auto out = second->Submit();
    second->WaitForFence();
    ASSERT_FALSE(out.has_value()) << "submitted ahead of the one it was resolved against";
    EXPECT_EQ(out.error().code, kor::ErrorCode::eMissingBarrier);
    EXPECT_NE(out.error().message.find("submitted in another order than they were ended in"), std::string::npos) << out.error().message;
    (void)first->Submit();
    first->WaitForFence();

    // In order, nothing to say. (A fresh image: after a pair out of order the one above was last resolved by one
    // and last run by the other, which the next command buffer to use it is told about too.)
    auto fresh = Image::Builder{}.SetType(Image::Type::e2D).SetFormat(Image::Format::eRGBA8_UNORM).SetExtent(glm::uvec2{8, 8})
                     .SetUsage(Image::Usage::eTransferDst | Image::Usage::eSampled).Build();
    first->Begin(); first->ClearColorImage(fresh); first->End();
    second->Begin(); second->ClearColorImage(fresh); second->End();
    EXPECT_TRUE(first->Submit().has_value());
    EXPECT_TRUE(second->Submit().has_value());
    first->WaitForFence(); second->WaitForFence();
}

// A task parked on the background pool, cancelled from this thread: woken there at once — resumed by the pool, as a
// signal would have resumed it — and stopped, so Wait() returns without the token ever happening.
Task<void> WaitOnTheBackground(Token never, std::atomic<bool>& onBackground) {
    co_await Context::SwitchToBackgroundThread();
    onBackground = true;
    co_await never;
}

TEST_F(TokenExecutorTest, CancellingATaskWaitingOnTheBackgroundWakesItThere) {
    const Token never = Token::Create();
    std::atomic<bool> onBackground{false};
    auto task = WaitOnTheBackground(never, onBackground);
    for (int i = 0; i < 2000 && !onBackground; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    ASSERT_TRUE(onBackground.load());
    std::this_thread::sleep_for(std::chrono::milliseconds(5));   // parked on the token by now
    EXPECT_FALSE(task.Done());

    task.Cancel();
    task.Wait();
    EXPECT_TRUE(task.IsCancelled());
}

}  // namespace

// ---- Deferred destruction ------------------------------------------------------------------------
//
// Destroying something the GPU may still be using hands its frees to the retire list, keyed by
// "everything submitted so far". A submission that waits on a token the CPU has not signalled yet is
// guaranteed to still be pending, which makes "still in use" something a test can hold steady.

namespace {

TEST_F(TokenExecutorTest, DestructionWaitsForWorkSubmittedBeforeIt) {
    const Token go = Token::Create();
    auto pending = CommandBuffer::Create(CommandBuffer::Usage::eGraphics);
    pending->Begin();
    pending->End();
    const Token pendingDone = Token::Create();
    ASSERT_TRUE(pending->Submit({.waitFor = {go}, .signal = {pendingDone}}));

    std::atomic<bool> destroyed{false};
    vk::Context::DestroyWhenUnused([&] { destroyed.store(true); });
    EXPECT_FALSE(destroyed.load()) << "destroyed while a submission from before it was still pending";

    // Something collects — and must still not free it, since the pending work has not run.
    auto collector = CommandBuffer::Create(CommandBuffer::Usage::eGraphics);
    collector->Begin();
    collector->End();
    const Token collectorDone = Token::Create();
    ASSERT_TRUE(collector->Submit({.waitFor = {go}, .signal = {collectorDone}}));
    EXPECT_FALSE(destroyed.load());

    go.Signal();
    pendingDone.Wait();
    collectorDone.Wait();

    // The next collection finds it due.
    collector->Begin();
    collector->End();
    const Token last = Token::Create();
    ASSERT_TRUE(collector->Submit({.signal = {last}}));
    EXPECT_TRUE(destroyed.load()) << "never freed after the work it waited for finished";
    last.Wait();
}

TEST_F(TokenExecutorTest, DestructionWithNothingPendingIsImmediate) {
    // Drain whatever earlier tests left, so every epoch submitted so far has been reached.
    CommandBuffer::SingleTimeCommand([](CommandBuffer&) {}).Wait();
    auto cb = CommandBuffer::Create(CommandBuffer::Usage::eGraphics);
    cb->Begin();
    cb->End();
    const Token done = Token::Create();
    ASSERT_TRUE(cb->Submit({.signal = {done}}));
    done.Wait();

    std::atomic<bool> destroyed{false};
    vk::Context::DestroyWhenUnused([&] { destroyed.store(true); });
    EXPECT_TRUE(destroyed.load());
}

// The validation layer reports destroying a buffer that a pending submission uses
// (VUID-vkDestroyBuffer-buffer-00922), so a clean log is the evidence the free was deferred.
TEST_F(TokenExecutorTest, ABufferDestroyedWhileInUseIsFreedOnlyOnceTheGpuIsDone) {
    const auto since = logMark();
    const Token go = Token::Create();
    const Token done = Token::Create();
    auto cb = CommandBuffer::Create(CommandBuffer::Usage::eGraphics);
    {
        auto buffer = makeDeviceBuffer(std::vector<int>(1024, 7));
        ASSERT_TRUE(static_cast<bool>(buffer));
        cb->Begin();
        cb->ClearBuffer(buffer);
        cb->End();
        ASSERT_TRUE(cb->Submit({.waitFor = {go}, .signal = {done}}));
    }   // destroyed here, with the clear still waiting on `go`

    go.Signal();
    done.Wait();
    // Let the deferred free happen, then look.
    CommandBuffer::SingleTimeCommand([](CommandBuffer&) {}).Wait();
    expectNoValidationErrorsSince(since);
}

}  // namespace
