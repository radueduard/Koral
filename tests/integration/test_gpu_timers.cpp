// Integration test for CommandBuffer::BeginTimer / EndTimer against a real device.
//
// What is worth pinning down here is not "a number came back" but *when* it comes back. The
// timestamps do not exist until the GPU has run the commands that write them, so a scope's result
// is deliberately unavailable in the recording that opened it, and is collected at the start of the
// next one — which is the first moment the command buffer is provably free. These tests hold that
// contract, the nesting, and the two ways a scope can be malformed.

#include "gpu_fixture.h"

#include <cstdint>
#include <numeric>
#include <vector>

#include "buffer.h"
#include "commandBuffer.h"
#include "computePipeline.h"
#include "descriptor.h"
#include "descriptorSet.h"
#include "shader.h"

using kor::Buffer;
using kor::CommandBuffer;
using kor::ComputePipeline;
using kor::Descriptor;
using kor::DescriptorSet;
using kor::ResourceRef;
using kor::Shader;

namespace {

constexpr std::uint32_t kCount = 256;      // multiple of local_size_x (64)
constexpr std::uint32_t kLocalSize = 64;

// Enough dispatches that the scope spans real device work rather than two adjacent timestamps.
constexpr std::uint32_t kDispatches = 16;

// A compute pipeline over a storage buffer — something for a timed scope to contain. Reuses the
// same shader the dispatch tests do: it doubles every uint in place, and what it computes is
// irrelevant here, only that the GPU spends time on it.
struct Workload {
    kor::Resource<Buffer> buffer;
    kor::Resource<ComputePipeline> pipeline;
    kor::Resource<DescriptorSet> descriptorSet;

    static Workload build() {
        std::vector<std::uint32_t> input(kCount);
        std::iota(input.begin(), input.end(), 1u);

        Buffer::Builder<std::uint32_t> bufBuilder;
        bufBuilder.setData(input);
        bufBuilder.addUsage(Buffer::Usage::eStorage);
        auto buffer = bufBuilder.build();

        Shader::Builder shaderBuilder;
        shaderBuilder.setPath("doubleValues.comp.glsl");
        auto shader = shaderBuilder.build();

        ComputePipeline::Builder pipeBuilder;
        pipeBuilder.setComputeShader(shader);
        auto pipeline = pipeBuilder.build();

        auto descriptorSet =
            DescriptorSet::Builder(ResourceRef<const kor::Pipeline>(pipeline), 0)
                .write(0, Descriptor(ResourceRef<const Buffer>(buffer)))
                .build();

        return Workload{ std::move(buffer), std::move(pipeline), std::move(descriptorSet) };
    }

    void record(CommandBuffer& cb) const {
        cb.BindComputePipeline(ResourceRef<const ComputePipeline>(pipeline));
        cb.BindDescriptorSet(0, ResourceRef<const DescriptorSet>(descriptorSet));
        for (std::uint32_t i = 0; i < kDispatches; ++i)
            cb.Dispatch(kCount / kLocalSize, 1, 1);
    }
};

// Record, submit and wait. Leaves the buffer submitted-and-complete, which is the state a
// following Begin() collects the timings from.
void runAndWait(CommandBuffer& cb, const std::function<void(CommandBuffer&)>& body) {
    cb.Begin();
    body(cb);
    cb.End();
    ASSERT_TRUE(cb.Submit()) << "submit failed";
    cb.WaitForFence();
}


TEST_F(GpuTest, TimerReportsPositiveGpuTimeAfterTheNextRecording) {
    const auto cb = CommandBuffer::Create(CommandBuffer::Usage::eCompute);
    if (!cb->supportsTimers()) GTEST_SKIP() << "queue reports no valid timestamp bits";

    const auto work = Workload::build();
    ASSERT_TRUE(work.pipeline.valid());

    runAndWait(*cb, [&](CommandBuffer& c) {
        c.Timer("dispatches", [&](CommandBuffer& inner) { work.record(inner); });
        EXPECT_TRUE(c.ok()) << "recording failed: " << c.result().error().toString();
    });

    // The deferral, stated as a test: the work is finished on the GPU, but nothing has collected
    // the timestamps yet, because collecting them is what the next Begin() does.
    EXPECT_TRUE(cb->getTimings().empty())
        << "timings appeared before the recording that collects them";

    cb->Begin();

    const auto& timings = cb->getTimings();
    ASSERT_EQ(timings.size(), 1u);
    EXPECT_EQ(timings[0].label, "dispatches");
    EXPECT_EQ(timings[0].depth, 0u);
    EXPECT_GT(timings[0].milliseconds, 0.0) << "a scope around " << kDispatches
                                            << " dispatches measured no GPU time at all";
    // Sanity bound rather than a performance assertion: catches a unit or period mix-up, which
    // would land orders of magnitude out, without being sensitive to how fast the device is.
    EXPECT_LT(timings[0].milliseconds, 1000.0);

    cb->End();
}


TEST_F(GpuTest, NestedTimersReportTheirDepthAndOrder) {
    const auto cb = CommandBuffer::Create(CommandBuffer::Usage::eCompute);
    if (!cb->supportsTimers()) GTEST_SKIP() << "queue reports no valid timestamp bits";

    const auto work = Workload::build();
    ASSERT_TRUE(work.pipeline.valid());

    runAndWait(*cb, [&](CommandBuffer& c) {
        c.BeginTimer("outer");
        c.BeginTimer("inner");
        work.record(c);
        c.EndTimer();
        work.record(c);
        c.EndTimer();
        EXPECT_TRUE(c.ok()) << "recording failed: " << c.result().error().toString();
    });

    cb->Begin();

    const auto& timings = cb->getTimings();
    ASSERT_EQ(timings.size(), 2u);
    // Reported in the order the scopes were *opened*, so the enclosing one comes first.
    EXPECT_EQ(timings[0].label, "outer");
    EXPECT_EQ(timings[0].depth, 0u);
    EXPECT_EQ(timings[1].label, "inner");
    EXPECT_EQ(timings[1].depth, 1u);
    // The inner scope is contained in the outer one, so it cannot have taken longer. Compared with
    // a small tolerance: the two pairs of timestamps are independent counters, not a subdivision.
    EXPECT_LE(timings[1].milliseconds, timings[0].milliseconds + 0.5);

    cb->End();
}


TEST_F(GpuTest, StaleTimingsSurviveARecordingThatTimesNothing) {
    const auto cb = CommandBuffer::Create(CommandBuffer::Usage::eCompute);
    if (!cb->supportsTimers()) GTEST_SKIP() << "queue reports no valid timestamp bits";

    const auto work = Workload::build();
    ASSERT_TRUE(work.pipeline.valid());

    runAndWait(*cb, [&](CommandBuffer& c) {
        c.Timer("measured", [&](CommandBuffer& inner) { work.record(inner); });
    });

    // A second pass with no timers at all. Its Begin() collects the first pass's results; its own
    // lack of scopes must not then wipe them, or a UI reading them would flicker to empty whenever
    // a frame happened not to measure anything.
    runAndWait(*cb, [&](CommandBuffer& c) { work.record(c); });
    ASSERT_EQ(cb->getTimings().size(), 1u);

    cb->Begin();
    const auto& timings = cb->getTimings();
    ASSERT_EQ(timings.size(), 1u);
    EXPECT_EQ(timings[0].label, "measured");
    cb->End();
}


// The one-shot path: a command buffer that is submitted once and never re-recorded, which is what
// a job's compute pass is. Waiting for the next Begin() would mean waiting forever, so CollectTimer
// goes and fetches the timestamps itself.
TEST_F(GpuTest, CollectTimerReadsAResultWithoutAnotherRecording) {
    const auto cb = CommandBuffer::Create(CommandBuffer::Usage::eCompute);
    if (!cb->supportsTimers()) GTEST_SKIP() << "queue reports no valid timestamp bits";

    const auto work = Workload::build();
    ASSERT_TRUE(work.pipeline.valid());

    runAndWait(*cb, [&](CommandBuffer& c) {
        c.Timer("sort", [&](CommandBuffer& inner) { work.record(inner); });
    });

    // No second Begin() anywhere in this test — that is the whole point.
    const auto milliseconds = cb->CollectTimer("sort");
    ASSERT_TRUE(milliseconds.has_value()) << milliseconds.error().message;
    EXPECT_GT(*milliseconds, 0.0);
    EXPECT_LT(*milliseconds, 1000.0);

    // Idempotent: the results stay readable once fetched, so a caller may ask again — or ask for a
    // second scope — without the first call having consumed them.
    const auto again = cb->CollectTimer("sort");
    ASSERT_TRUE(again.has_value()) << again.error().message;
    EXPECT_DOUBLE_EQ(*again, *milliseconds);

    // And the plural form sees the same set.
    ASSERT_EQ(cb->CollectTimings().size(), 1u);
    EXPECT_EQ(cb->CollectTimings().front().label, "sort");
}

// A name that was never recorded is a mistake in the caller's code, and has to read as one rather
// than as "not ready yet" — the two have completely different fixes.
TEST_F(GpuTest, CollectTimerNamesTheTimerItCannotFind) {
    const auto cb = CommandBuffer::Create(CommandBuffer::Usage::eCompute);
    if (!cb->supportsTimers()) GTEST_SKIP() << "queue reports no valid timestamp bits";

    const auto work = Workload::build();
    ASSERT_TRUE(work.pipeline.valid());

    runAndWait(*cb, [&](CommandBuffer& c) {
        c.Timer("sort", [&](CommandBuffer& inner) { work.record(inner); });
    });

    const auto missing = cb->CollectTimer("scan");
    ASSERT_FALSE(missing.has_value());
    EXPECT_NE(missing.error().message.find("scan"), std::string::npos) << missing.error().message;
    // Specifically not the in-flight message: the work is done, the name is simply wrong.
    EXPECT_EQ(missing.error().message.find("has not finished"), std::string::npos)
        << "a misspelled name was reported as unfinished work: " << missing.error().message;
}

// getTimings() stays a plain accessor — it must not go and fetch, or the recurring path would
// collect at unpredictable moments instead of once per Begin().
TEST_F(GpuTest, GetTimingsDoesNotFetchButCollectTimingsDoes) {
    const auto cb = CommandBuffer::Create(CommandBuffer::Usage::eCompute);
    if (!cb->supportsTimers()) GTEST_SKIP() << "queue reports no valid timestamp bits";

    const auto work = Workload::build();
    ASSERT_TRUE(work.pipeline.valid());

    runAndWait(*cb, [&](CommandBuffer& c) {
        c.Timer("sort", [&](CommandBuffer& inner) { work.record(inner); });
    });

    EXPECT_TRUE(cb->getTimings().empty()) << "getTimings() collected results on its own";
    EXPECT_EQ(cb->CollectTimings().size(), 1u);
    EXPECT_EQ(cb->getTimings().size(), 1u) << "what CollectTimings fetched must stay readable";
}


TEST_F(GpuTest, UnmatchedEndTimerFailsTheRecording) {
    const auto cb = CommandBuffer::Create(CommandBuffer::Usage::eCompute);
    if (!cb->supportsTimers()) GTEST_SKIP() << "queue reports no valid timestamp bits";

    cb->Begin();
    cb->EndTimer();

    EXPECT_FALSE(cb->ok());
    ASSERT_FALSE(cb->errors().empty());
    EXPECT_NE(cb->errors().front().message.find("EndTimer"), std::string::npos)
        << cb->errors().front().message;

    cb->End();
}


TEST_F(GpuTest, UnclosedTimerIsReportedAtEnd) {
    const auto cb = CommandBuffer::Create(CommandBuffer::Usage::eCompute);
    if (!cb->supportsTimers()) GTEST_SKIP() << "queue reports no valid timestamp bits";

    cb->Begin();
    cb->BeginTimer("forgotten");
    EXPECT_TRUE(cb->ok()) << "opening a scope is not itself an error";
    cb->End();

    EXPECT_FALSE(cb->ok()) << "a scope left open can never resolve, and must be reported";
    ASSERT_FALSE(cb->errors().empty());
    EXPECT_NE(cb->errors().front().message.find("forgotten"), std::string::npos)
        << cb->errors().front().message;

    // And nothing is published from a recording that could not resolve.
    cb->Begin();
    EXPECT_TRUE(cb->getTimings().empty());
    cb->End();
}

} // namespace
