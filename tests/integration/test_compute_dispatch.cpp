// Integration test for a compute-shader dispatch round-trip against a real
// Vulkan device. Exercises: GLSL->SPIR-V compilation, descriptor-set-layout
// reflection, ComputePipeline creation, descriptor set binding, buffer barriers,
// Dispatch, and SingleTimeCommand submit on the compute queue.
//
// The shader (shaders/doubleValues.comp.glsl) doubles every uint in a storage
// buffer in place; we upload a known sequence, dispatch, read back, and check
// every element doubled.

#include "gpu_fixture.h"

#include <cstdint>
#include <numeric>
#include <vector>

#include "buffer.h"
#include "commandBuffer.h"
#include "computePipeline.h"
#include "context.h"
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

TEST_F(GpuTest, ComputeDoublesStorageBuffer) {
    // --- input data -------------------------------------------------------
    std::vector<std::uint32_t> input(kCount);
    std::iota(input.begin(), input.end(), 1u); // 1,2,3,...,256

    // --- storage buffer (device-local; upload + readback via staging) -----
    Buffer::Builder<std::uint32_t> bufBuilder;
    bufBuilder.setData(input);
    bufBuilder.addUsage(Buffer::Usage::eStorage);
    bufBuilder.addUsage(Buffer::Usage::eTransferSrc);
    bufBuilder.addUsage(Buffer::Usage::eTransferDst);
    bufBuilder.setType(Buffer::Type::eDeviceLocal);
    auto buffer = bufBuilder.build();

    // --- compute shader + pipeline ---------------------------------------
    const ResourceRef<const Shader> shader =
        Shader::Builder{}
            .setLang<Shader::Lang::eGLSL>()
            .setStage(Shader::Stage::eCompute)
            .setPath(kor::shaderPath("doubleValues.comp.glsl"))
            .getOrBuild("test.doubleValues");

    ComputePipeline::Builder pipeBuilder;
    pipeBuilder.setComputeShader(shader);
    auto pipeline = pipeBuilder.build();

    // --- descriptor set: bind the storage buffer at set 0, binding 0 ------
    auto descriptorSet =
        DescriptorSet::Builder(kor::ResourceRef<const kor::Pipeline>(pipeline), 0)
            .write(0, Descriptor(ResourceRef<const Buffer>(buffer)))
            .build();

    // --- record + submit on the compute queue -----------------------------
    const ResourceRef<const Buffer> bufRef(buffer);
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BindComputePipeline(ResourceRef<const ComputePipeline>(pipeline));
        cb.BindDescriptorSet(0, ResourceRef<const DescriptorSet>(descriptorSet));
        // No barriers. The buffer is bound through the descriptor set, so the engine knows
        // the dispatch reads and writes it (the shader's SSBO carries neither NonReadable nor
        // NonWritable), and that the readback copy that follows needs those writes visible.
        cb.Dispatch(kCount / kLocalSize, 1, 1);
    }, CommandBuffer::Usage::eCompute);

    // --- verify -----------------------------------------------------------
    const std::vector<std::uint32_t> output = buffer->Read<std::uint32_t>();
    ASSERT_EQ(output.size(), input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        EXPECT_EQ(output[i], input[i] * 2u) << "at index " << i;
    }
}


// The decisive test for automatic barriers: two dispatches in a row, both reading and
// writing the same storage buffer, with nothing in between. The second must observe the
// first, so the engine has to insert a barrier between them -- there is no other machinery
// that would (the upload and readback paths each carry their own synchronisation, which is
// why the single-dispatch test above passes either way).
//
// Doubling twice is 4x. Without a barrier the second dispatch may read values the first has
// not finished writing, which is both a wrong result and a write-after-write hazard that
// synchronization validation reports. Run with KORAL_SYNC_VALIDATION=1 to see it.
TEST_F(GpuTest, BackToBackDispatchesAreSynchronised) {
    std::vector<std::uint32_t> input(kCount);
    std::iota(input.begin(), input.end(), 1u);

    Buffer::Builder<std::uint32_t> bufBuilder;
    bufBuilder.setData(input);
    bufBuilder.addUsage(Buffer::Usage::eStorage);
    bufBuilder.addUsage(Buffer::Usage::eTransferSrc);
    bufBuilder.addUsage(Buffer::Usage::eTransferDst);
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
        DescriptorSet::Builder(kor::ResourceRef<const kor::Pipeline>(pipeline), 0)
            .write(0, Descriptor(ResourceRef<const Buffer>(buffer)))
            .build();
    ASSERT_TRUE(descriptorSet.valid());

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BindComputePipeline(ResourceRef<const ComputePipeline>(pipeline));
        cb.BindDescriptorSet(0, ResourceRef<const DescriptorSet>(descriptorSet));
        cb.Dispatch(kCount / kLocalSize, 1, 1);
        cb.Dispatch(kCount / kLocalSize, 1, 1);
        EXPECT_TRUE(cb.ok()) << "recording failed: " << cb.result().error().toString();
    }, CommandBuffer::Usage::eCompute);

    const std::vector<std::uint32_t> output = buffer->Read<std::uint32_t>();
    ASSERT_EQ(output.size(), input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        EXPECT_EQ(output[i], input[i] * 4u) << "at index " << i;
    }
}


// The escape hatch's diagnostic. A shader reaching its data through a raw device address is
// the one case the engine cannot synchronise: reflection sees the dereference but not the
// buffer. Writing such a buffer and then dispatching that shader with no barrier in between
// must produce a precise eMissingBarrier naming both commands, not silence.
TEST_F(GpuTest, DeviceAddressHazardIsReported) {
    std::vector<std::uint32_t> input(kCount, 1u);

    Buffer::Builder<std::uint32_t> bufBuilder;
    bufBuilder.setData(input);
    bufBuilder.addUsage(Buffer::Usage::eStorage);
    bufBuilder.addUsage(Buffer::Usage::eTransferSrc);
    bufBuilder.addUsage(Buffer::Usage::eTransferDst);
    bufBuilder.addUsage(Buffer::Usage::eShaderDeviceAddress);
    auto buffer = bufBuilder.build();
    ASSERT_TRUE(buffer.valid());

    Buffer::Builder<std::uint32_t> srcBuilder;
    srcBuilder.setData(input);
    srcBuilder.addUsage(Buffer::Usage::eTransferSrc);
    auto source = srcBuilder.build();
    ASSERT_TRUE(source.valid());

    Shader::Builder shaderBuilder;
    shaderBuilder.setPath("deviceAddress.comp.glsl");
    auto shader = shaderBuilder.build();
    ASSERT_TRUE(shader.valid()) << "device-address shader failed to build";

    ComputePipeline::Builder pipeBuilder;
    pipeBuilder.setComputeShader(shader);
    auto pipeline = pipeBuilder.build();
    ASSERT_TRUE(pipeline.valid());

    // Recorded by hand rather than through SingleTimeCommand: the diagnostic is produced by
    // End(), and we want to inspect it without submitting work the engine has just told us is
    // unsynchronised.
    const auto cb = CommandBuffer::Create(CommandBuffer::Usage::eCompute);
    cb->Begin();
    cb->BindComputePipeline(ResourceRef<const ComputePipeline>(pipeline));
    // Writes the device-address buffer, then runs a shader that may read it through a
    // pointer. Nothing here tells the engine those are the same buffer.
    cb->CopyBuffer(ResourceRef<const Buffer>(source), ResourceRef<const Buffer>(buffer));
    // The address the shader will chase. Pushing it is what makes this a realistic
    // device-address workload rather than a shader with an unset push constant.
    const glm::u64 address = buffer->getDeviceAddress();
    cb->PushConstants(address);
    cb->Dispatch(kCount / kLocalSize, 1, 1);
    cb->End();

    bool reported = false;
    std::string message;
    for (const auto& error : cb->errors()) {
        if (error.code == kor::ErrorCode::eMissingBarrier) {
            reported = true;
            message = error.message;
        }
    }

    ASSERT_TRUE(reported) << "the engine stayed silent about a hazard it cannot synchronise";
    // The message has to be actionable: which buffer, both commands, and the fix.
    EXPECT_NE(message.find("CopyBuffer"), std::string::npos) << message;
    EXPECT_NE(message.find("Dispatch"), std::string::npos) << message;
    EXPECT_NE(message.find("BufferBarrier"), std::string::npos) << message;
    EXPECT_NE(message.find("test_compute_dispatch.cpp"), std::string::npos) << message;
}

} // namespace
