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
        // Make the uploaded data visible to the compute shader...
        cb.BufferBarrier(kor::BufferBarrier(bufRef, kor::ResourceAccess::ComputeReadWrite));
        cb.Dispatch(kCount / kLocalSize, 1, 1);
        // ...and make the shader's writes available to the subsequent readback copy.
        cb.BufferBarrier(kor::BufferBarrier(bufRef, kor::ResourceAccess::TransferSrc));
    }, CommandBuffer::Usage::eCompute);

    // --- verify -----------------------------------------------------------
    const std::vector<std::uint32_t> output = buffer->Read<std::uint32_t>();
    ASSERT_EQ(output.size(), input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        EXPECT_EQ(output[i], input[i] * 2u) << "at index " << i;
    }
}

} // namespace
