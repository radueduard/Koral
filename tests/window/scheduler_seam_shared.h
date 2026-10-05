// Scheduler::Execute / WaitFor / frameCompletion, as the windowed suite checks them.
//
// Each check takes the suite's own drawFrame, because a frame is the only thing that runs executed
// work. Completion is observed by drawing until the token is ready rather than by Wait(), so the
// checks hold for a backend that signals a frame's completion only from the next Draw.

#pragma once

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "buffer.h"
#include "commandBuffer.h"
#include "context.h"
#include "log.h"
#include "scheduler.h"
#include "task.h"
#include "token.h"

namespace seam {

using DrawFrame = std::function<void()>;

inline kor::Resource<kor::Buffer> makeBuffer(const int fill) {
    kor::Buffer::Builder<int> b;
    b.SetData(std::vector<int>(64, fill));
    b.SetUsage(kor::Buffer::Usage::eStorage | kor::Buffer::Usage::eTransferSrc | kor::Buffer::Usage::eTransferDst);
    b.SetType(kor::Buffer::Type::eDeviceLocal);
    return b.Build();
}

// Draws until the token is ready, or gives up after `limit` frames.
inline bool drawUntil(const kor::Token& token, const DrawFrame& draw, const int limit = 16) {
    for (int i = 0; i < limit && !token.Ready(); ++i) draw();
    return token.Ready();
}

inline std::uint64_t logMark() {
    const auto history = kor::log::History();
    return history.empty() ? 0ull : history.back().sequence;
}

inline void expectNoValidationErrorsSince(const std::uint64_t since) {
    for (const auto& record : kor::log::HistorySince(since)) {
        if (record.level != kor::log::Level::eError) continue;
        EXPECT_EQ(record.message.find("VUID"), std::string::npos) << record.message;
        // Only reported with synchronization validation on (see tests/README.md), which is the
        // run that shows the barriers between executed command buffers are really there.
        EXPECT_EQ(record.message.find("hazard detected"), std::string::npos) << record.message;
    }
}

// Work recorded on another thread and placed around the frame, in the order it was handed over —
// and synchronised across command buffers: the after-frame copy reads what the last of two
// before-frame fills wrote, with nothing but the order the frame ends them in to put the barriers
// between them.
inline void executedWorkRunsInOrderAroundTheFrame(const DrawFrame& draw) {
    const auto since = logMark();
    auto source = makeBuffer(0);
    auto destination = makeBuffer(0);
    ASSERT_TRUE(static_cast<bool>(source));
    ASSERT_TRUE(static_cast<bool>(destination));

    kor::Token done;
    std::thread recorder([&] {
        const auto fill = [&](const int value) {
            auto cb = kor::CommandBuffer::Create(kor::CommandBuffer::Usage::eGraphics);
            cb->Begin();
            cb->FillBuffer(source, std::vector<int>(64, value));
            return cb;
        };
        auto copy = kor::CommandBuffer::Create(kor::CommandBuffer::Usage::eGraphics);
        copy->Begin();
        copy->CopyBuffer(source, destination);

        auto& scheduler = kor::Context::Scheduler();
        // Handed over out of placement order on purpose: placement decides, not arrival.
        done = scheduler.Execute(std::move(copy), kor::Scheduler::Placement::eAfterFrame);
        scheduler.Execute(fill(5));
        scheduler.Execute(fill(9));  // after the 5, so it is what the copy sees
    });
    recorder.join();

    ASSERT_NE(done.Value(), 0u) << "Execute refused a command buffer it should have taken";
    ASSERT_TRUE(drawUntil(done, draw)) << "the frame that ran the work never completed";
    EXPECT_EQ(destination->Read<int>(), std::vector<int>(64, 9));
    expectNoValidationErrorsSince(since);
}

inline void anEndedCommandBufferIsRefused() {
    auto cb = kor::CommandBuffer::Create(kor::CommandBuffer::Usage::eGraphics);
    cb->Begin();
    cb->End();
    const kor::Token token = kor::Context::Scheduler().Execute(std::move(cb));
    EXPECT_EQ(token.Value(), 0u) << "an already-ended command buffer would have its barriers resolved out of order";
}

inline kor::Task<void> ResumeWhenTheFrameIsDone(kor::Token frame, std::atomic<bool>& resumed) {
    co_await kor::Context::SwitchToBackgroundThread();
    co_await frame;
    resumed.store(true);
}

inline void aCoroutineResumesWhenItsFrameCompletes(const DrawFrame& draw) {
    const kor::Token frame = kor::Context::Scheduler().FrameCompletion();
    std::atomic<bool> resumed{false};
    auto task = ResumeWhenTheFrameIsDone(frame, resumed);
    EXPECT_FALSE(frame.Ready()) << "the frame being built cannot have finished already";

    ASSERT_TRUE(drawUntil(frame, draw));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!resumed.load() && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    EXPECT_TRUE(resumed.load());
}

// The frame is held back until another thread says go: the GPU waits and the CPU carries on, and
// the frame completes only afterwards.
inline void aFrameWaitsForAToken(const DrawFrame& draw) {
    const auto since = logMark();
    const kor::Token go = kor::Token::Create();
    auto& scheduler = kor::Context::Scheduler();
    scheduler.WaitFor(go);
    const kor::Token frame = scheduler.FrameCompletion();

    std::atomic<bool> signalled{false};
    std::thread signaller([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        signalled.store(true);
        go.Signal();
    });

    draw();
    ASSERT_TRUE(drawUntil(frame, draw));
    EXPECT_TRUE(signalled.load()) << "the frame finished before the token it waited for";
    signaller.join();
    expectNoValidationErrorsSince(since);
}

}  // namespace seam
