// What resolving barriers costs as a recording gets long.
//
// The shape that provoked this is an odd-even transposition sort: one storage buffer, one pipeline,
// and thousands of back-to-back dispatches over it. Every pass writes what the previous one wrote,
// so every dispatch needs a write-after-write barrier — the resolver's densest possible output, one
// inserted record per recorded one.
//
// The test asserts scaling, not a wall-clock budget: doubling the dispatch count must roughly
// double the cost, not quadruple it. A budget would be a machine-speed assertion and would either
// be uselessly loose or fail on someone else's hardware; the ratio is a property of the algorithm.

#include "gpu_fixture.h"

#include <chrono>
#include <cstdint>
#include <iostream>
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

constexpr std::uint32_t kCount = 256;
constexpr std::uint32_t kLocalSize = 64;

TEST_F(GpuTest, BarrierResolutionScalesLinearlyWithRecordingLength) {
    std::vector<std::uint32_t> input(kCount);
    std::iota(input.begin(), input.end(), 1u);

    Buffer::Builder<std::uint32_t> bufBuilder;
    bufBuilder.setData(input);
    bufBuilder.addUsage(Buffer::Usage::eStorage);
    auto buffer = bufBuilder.build();
    ASSERT_TRUE(buffer.valid());

    Shader::Builder shaderBuilder;
    shaderBuilder.setPath("doubleValues.comp.glsl");
    auto shader = shaderBuilder.build();
    ASSERT_TRUE(shader.valid());

    ComputePipeline::Builder pipeBuilder;
    pipeBuilder.setComputeShader(shader);
    auto pipeline = pipeBuilder.build();
    ASSERT_TRUE(pipeline.valid());

    auto descriptorSet =
        DescriptorSet::Builder(ResourceRef<const kor::Pipeline>(pipeline), 0)
            .write(0, buffer)
            .build();
    ASSERT_TRUE(descriptorSet.valid());

    const auto cb = CommandBuffer::Create(CommandBuffer::Usage::eCompute);

    // Records `passes` dispatches and returns how long End() — resolve + emit — took. Nothing is
    // submitted: this measures the CPU side of recording, which is what the sort was waiting on.
    const auto timeEnd = [&](const std::uint32_t passes) {
        cb->Begin();
        cb->BindComputePipeline(ResourceRef<const ComputePipeline>(pipeline));
        cb->BindDescriptorSet(0, ResourceRef<const DescriptorSet>(descriptorSet));
        for (std::uint32_t pass = 0; pass < passes; ++pass)
            cb->Dispatch(kCount / kLocalSize, 1, 1);

        const auto start = std::chrono::steady_clock::now();
        cb->End();
        const auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        cb->Reset();
        return elapsed;
    };

    timeEnd(256);   // warm the allocator so the first real sample is not the odd one out

    // Reduced by minimum over repeats: End() can only be *slowed* by something else on the
    // machine, so the fastest run of each is the one least polluted by it.
    double small = 1e9, large = 1e9;
    for (int repeat = 0; repeat < 5; ++repeat) {
        small = std::min(small, timeEnd(1024));
        large = std::min(large, timeEnd(4096));
    }

    const double ratio = large / small;
    std::cout << "[ MEASURE  ] End() over 1024 dispatches: " << small << " ms, over 4096: "
              << large << " ms (4x the work, " << ratio << "x the time)" << std::endl;

    // Four times the dispatches. Linear resolution lands near 4x; the quadratic splice this
    // replaced landed near 16x. Generous ceiling so the test reports an algorithmic regression
    // rather than machine noise.
    EXPECT_LT(ratio, 8.0) << "barrier resolution is scaling superlinearly with recording length: "
                          << "4x the dispatches cost " << ratio << "x the time";
}

} // namespace
