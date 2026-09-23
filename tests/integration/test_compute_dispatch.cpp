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
#include "bufferView.h"
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
    bufBuilder.setUsage(Buffer::Usage::eStorage | Buffer::Usage::eTransferSrc | Buffer::Usage::eTransferDst);
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
        DescriptorSet::Builder(pipeline, 0)
            .write(0, buffer)
            .build();

    // --- record + submit on the compute queue -----------------------------
    const ResourceRef<const Buffer> bufRef(buffer);
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BindComputePipeline(pipeline);
        cb.BindDescriptorSet(0, descriptorSet);
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
    bufBuilder.setUsage(Buffer::Usage::eStorage | Buffer::Usage::eTransferSrc | Buffer::Usage::eTransferDst);
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
        DescriptorSet::Builder(pipeline, 0)
            .write(0, buffer)
            .build();
    ASSERT_TRUE(descriptorSet.valid());

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BindComputePipeline(pipeline);
        cb.BindDescriptorSet(0, descriptorSet);
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
    bufBuilder.setUsage(Buffer::Usage::eStorage
                        | Buffer::Usage::eTransferSrc
                        | Buffer::Usage::eTransferDst
                        | Buffer::Usage::eShaderDeviceAddress);
    auto buffer = bufBuilder.build();
    ASSERT_TRUE(buffer.valid());

    Buffer::Builder<std::uint32_t> srcBuilder;
    srcBuilder.setData(input);
    srcBuilder.setUsage(Buffer::Usage::eTransferSrc);
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
    cb->BindComputePipeline(pipeline);
    // Writes the device-address buffer, then runs a shader that may read it through a
    // pointer. Nothing here tells the engine those are the same buffer.
    cb->CopyBuffer(source, buffer);
    // The address the shader will chase. Pushing it is what makes this a realistic
    // device-address workload rather than a shader with an unset push constant.
    const glm::u64 address = buffer->deviceAddress();
    cb->PushConstantBlock(address);
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

// Writing a binding by the name the shader gives it, rather than by a number restated in C++.
// doubleValues.comp.glsl declares `layout(set = 0, binding = 0) buffer Data { ... } data;`, so the
// binding answers to both names it has: the instance's, and the block type's.
TEST_F(GpuTest, ADescriptorSetCanBeWrittenByBindingName) {
    std::vector<std::uint32_t> input(kCount);
    std::iota(input.begin(), input.end(), 1u);

    Buffer::Builder<std::uint32_t> bufBuilder;
    bufBuilder.setData(input)
              .setUsage(Buffer::Usage::eStorage | Buffer::Usage::eTransferSrc | Buffer::Usage::eTransferDst);
    auto buffer = bufBuilder.build();
    ASSERT_TRUE(buffer.valid());

    const ResourceRef<const Shader> shader =
        Shader::Builder{}.setLang<Shader::Lang::eGLSL>().setStage(Shader::Stage::eCompute)
            .setPath(kor::shaderPath("doubleValues.comp.glsl")).getOrBuild("test.named.double");

    auto pipeline = ComputePipeline::Builder{}.setComputeShader(shader).build();
    ASSERT_TRUE(pipeline.valid());

    // The instance name. No binding number anywhere in this test.
    auto byInstance = DescriptorSet::Builder(pipeline, 0).write("data", buffer).build();
    ASSERT_TRUE(byInstance.valid()) << (byInstance.error() ? byInstance.error()->history() : "");

    // The block type's name finds the same binding, which is what makes a block declared without
    // an instance name — `buffer Data { ... };` — addressable at all.
    auto byBlock = DescriptorSet::Builder(pipeline, 0).write("Data", buffer).build();
    ASSERT_TRUE(byBlock.valid()) << (byBlock.error() ? byBlock.error()->history() : "");

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BindComputePipeline(pipeline);
        cb.BindDescriptorSet(0, byInstance);
        cb.Dispatch(kCount / kLocalSize, 1, 1);
    }, CommandBuffer::Usage::eCompute);

    const std::vector<std::uint32_t> output = buffer->Read<std::uint32_t>();
    ASSERT_EQ(output.size(), input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        EXPECT_EQ(output[i], input[i] * 2u) << "at index " << i;
    }
}

// A name no binding answers to is a mistake worth explaining: the set is poisoned, and the message
// lists the names that do exist rather than leaving the reader to go and read the shader.
TEST_F(GpuTest, AnUnknownBindingNameIsReportedWithTheOnesThatExist) {
    Buffer::Builder<std::uint32_t> bufBuilder;
    bufBuilder.setData(std::vector<std::uint32_t>(kCount, 1u)).setUsage(Buffer::Usage::eStorage);
    auto buffer = bufBuilder.build();
    ASSERT_TRUE(buffer.valid());

    const ResourceRef<const Shader> shader =
        Shader::Builder{}.setLang<Shader::Lang::eGLSL>().setStage(Shader::Stage::eCompute)
            .setPath(kor::shaderPath("doubleValues.comp.glsl")).getOrBuild("test.badname.double");

    auto pipeline = ComputePipeline::Builder{}.setComputeShader(shader).build();
    ASSERT_TRUE(pipeline.valid());

    auto set = DescriptorSet::Builder(pipeline, 0).write("nosuchthing", buffer).build();
    ASSERT_FALSE(set.valid()) << "a name nothing answers to was accepted";
    ASSERT_NE(set.error(), nullptr);

    const auto message = set.error()->history();
    EXPECT_NE(message.find("nosuchthing"), std::string::npos) << message;
    EXPECT_NE(message.find("data"), std::string::npos)
        << "the message did not say what the set actually has: " << message;
}

// `name[n]` selects an element of an array binding, so an array is addressable by name too rather
// than falling back to numbers the moment a binding has more than one slot.
TEST_F(GpuTest, ATrailingSubscriptSelectsAnArrayElementByName) {
    const ResourceRef<const Shader> shader =
        Shader::Builder{}.setLang<Shader::Lang::eGLSL>().setStage(Shader::Stage::eCompute)
            .setPath(kor::shaderPath("doubleValues.comp.glsl")).getOrBuild("test.subscript.double");

    auto pipeline = ComputePipeline::Builder{}.setComputeShader(shader).build();
    ASSERT_TRUE(pipeline.valid());

    Buffer::Builder<std::uint32_t> bufBuilder;
    bufBuilder.setData(std::vector<std::uint32_t>(kCount, 1u)).setUsage(Buffer::Usage::eStorage);
    auto buffer = bufBuilder.build();
    ASSERT_TRUE(buffer.valid());

    // `data` is a single (count 1) binding, so element 0 is the only one there is: the subscript is
    // parsed and honoured rather than being read as part of the name.
    auto set = DescriptorSet::Builder(pipeline, 0).write("data[0]", buffer).build();
    EXPECT_TRUE(set.valid()) << (set.error() ? set.error()->history() : "");

    // And one past the end is out of bounds, not a differently-named binding.
    auto outOfRange = DescriptorSet::Builder(pipeline, 0).write("data[1]", buffer).build();
    EXPECT_FALSE(outOfRange.valid()) << "element 1 of a single-element binding was accepted";
}

// A texel buffer: the same bytes a storage buffer would hold, read by the shader as formatted
// texels through a kor::BufferView. What the view adds over binding the buffer directly is the
// format — the shader fetches vec4s without declaring a struct for them.
TEST_F(GpuTest, ATexelBufferIsFetchedThroughABufferView) {
    constexpr glm::u32 kTexels = 64;

    // Four floats per texel, so texel i is {i, 0, 0, 0} and texelFetch(...).x is i.
    std::vector<float> source(kTexels * 4, 0.f);
    for (glm::u32 i = 0; i < kTexels; ++i) source[i * 4] = static_cast<float>(i);

    Buffer::Builder<float> sourceBuilder;
    sourceBuilder.setData(source)
                 .setUsage(Buffer::Usage::eTexel | Buffer::Usage::eTransferDst);
    auto sourceBuffer = sourceBuilder.build();
    ASSERT_TRUE(sourceBuffer.valid()) << (sourceBuffer.error() ? sourceBuffer.error()->history() : "");

    auto view = kor::BufferView::Builder(sourceBuffer)
        .setFormat(kor::Image::Format::eRGBA32_SFLOAT)
        .build();
    ASSERT_TRUE(view.valid()) << (view.error() ? view.error()->history() : "");
    EXPECT_EQ(view->range(), static_cast<glm::i64>(source.size() * sizeof(float)))
        << "a range of 0 should have resolved to the rest of the buffer";

    Buffer::Builder<float> destBuilder;
    destBuilder.setData(std::vector<float>(kTexels, -1.f))
               .setUsage(Buffer::Usage::eStorage | Buffer::Usage::eTransferSrc | Buffer::Usage::eTransferDst);
    auto destination = destBuilder.build();
    ASSERT_TRUE(destination.valid());

    const auto shader = Shader::Builder{}.setLang<Shader::Lang::eGLSL>().setStage(Shader::Stage::eCompute)
        .setPath(kor::shaderPath("texelBuffer.comp.glsl")).getOrBuild("test.texelBuffer");
    auto pipeline = ComputePipeline::Builder{}.setComputeShader(shader).build();
    ASSERT_TRUE(pipeline.valid()) << (pipeline.error() ? pipeline.error()->history() : "");

    auto set = DescriptorSet::Builder(pipeline, 0)
        .write("source", view)
        .write("destination", destination)
        .build();
    ASSERT_TRUE(set.valid()) << (set.error() ? set.error()->history() : "");

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BindComputePipeline(pipeline);
        cb.BindDescriptorSet(0, set);
        cb.Dispatch(kTexels / 64, 1, 1);
    }, CommandBuffer::Usage::eCompute);

    const std::vector<float> output = destination->Read<float>();
    ASSERT_EQ(output.size(), static_cast<std::size_t>(kTexels));
    for (glm::u32 i = 0; i < kTexels; ++i) {
        EXPECT_FLOAT_EQ(output[i], static_cast<float>(i)) << "at texel " << i;
    }
}

// A buffer without Buffer::Usage::eTexel cannot be viewed as texels, and the message says which
// flag to add rather than leaving it to the driver's usage-bits complaint.
TEST_F(GpuTest, ABufferViewNeedsItsBufferCreatedForTexels) {
    Buffer::Builder<float> builder;
    builder.setData(std::vector<float>(16, 0.f)).setUsage(Buffer::Usage::eStorage);  // no eTexel
    auto buffer = builder.build();
    ASSERT_TRUE(buffer.valid());

    auto view = kor::BufferView::Builder(buffer)
        .setFormat(kor::Image::Format::eRGBA32_SFLOAT)
        .build();
    ASSERT_FALSE(view.valid()) << "a buffer with no eTexel usage was viewed as texels";
    ASSERT_NE(view.error(), nullptr);
    EXPECT_NE(view.error()->history().find("eTexel"), std::string::npos) << view.error()->history();
}

} // namespace
