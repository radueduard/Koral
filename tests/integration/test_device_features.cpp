// kor::Feature on a real device: asked for before the device is made — as a project library's static request is,
// at load — and enabled where the GPU has it; and a library loaded afterwards that requires one the device was made
// without is told so.

#include "gpu_fixture.h"

#include <vector>

#include "buffer.h"
#include "commandBuffer.h"
#include "computePipeline.h"
#include "context.h"
#include "descriptorSet.h"
#include "deviceFeatures.h"
#include "log.h"
#include "shader.h"

// Optional: the suite runs on GPUs without it too, and skips what needs it.
KORAL_REQUEST_FEATURES(kor::Feature::eAtomicFloat32);

namespace {

using kor::Context;
using kor::Feature;

TEST_F(GpuTest, ARequestedFeatureIsEnabledWhereTheGpuHasIt) {
    EXPECT_EQ(Context::Supports(Feature::eAtomicFloat32), Context::GpuHas(Feature::eAtomicFloat32));
    // Nobody asked for cooperative matrices: off, whatever the GPU has.
    EXPECT_FALSE(Context::Supports(Feature::eCooperativeMatrix));
    if (!Context::Supports(Feature::eAtomicFloat32)) GTEST_SKIP() << "the GPU has no 32-bit float atomics";

    const auto since = kor::log::LastSequence();
    auto sum = kor::Buffer::Builder<float>{}.SetData(std::vector<float>{0.f})
                   .SetUsage(kor::Buffer::Usage::eStorage | kor::Buffer::Usage::eTransferSrc | kor::Buffer::Usage::eTransferDst).Build();
    auto shader = kor::Shader::Builder{}.SetPath("atomicFloatAdd.comp.glsl").Build();
    ASSERT_TRUE(shader.Valid());
    auto pipeline = kor::ComputePipeline::Builder{}.SetComputeShader(shader).Build();
    ASSERT_TRUE(pipeline.Valid());
    auto set = kor::DescriptorSet::Builder(pipeline, 0).Write(0, sum).Build();
    kor::CommandBuffer::SingleTimeCommand([&](kor::CommandBuffer& cb) {
        cb.BindComputePipeline(pipeline).BindDescriptorSet(0, set).Dispatch(4, 1, 1);   // 256 additions
    }, kor::CommandBuffer::Usage::eCompute).Wait();
    EXPECT_FLOAT_EQ(sum->Read<float>().front(), 256.f);
    for (const auto& record : kor::log::HistorySince(since))
        EXPECT_EQ(record.message.find("VUID"), std::string::npos) << record.message;
}

TEST_F(GpuTest, ALibraryLoadedAfterTheDeviceRequiringWhatItLacksIsTold) {
    // What a library loaded now registers: one feature nobody asked for in time, one enabled anyway.
    (void)kor::detail::TakeLateFeatureErrors();
    kor::detail::RegisterFeatures("late.cpp", Feature::eCooperativeMatrix, {});
    const auto errors = kor::detail::TakeLateFeatureErrors();
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_NE(errors.front().find("late.cpp requires CooperativeMatrix"), std::string::npos) << errors.front();

    kor::detail::RegisterFeatures("fine.cpp", {}, Feature::eCooperativeMatrix);   // only would use it: fine
    if (Context::Supports(Feature::eShaderInt64))
        kor::detail::RegisterFeatures("fine.cpp", Feature::eShaderInt64, {});     // enabled already: fine
    EXPECT_TRUE(kor::detail::TakeLateFeatureErrors().empty());
}

}
