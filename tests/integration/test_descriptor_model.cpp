// The descriptor model, and what rides on it: bindless indices, binding by name, embedded samplers and
// the descriptor heap a project fills itself. The suite runs twice — once per model, the second time as
// Heap.* with KORAL_DESCRIPTORS=heap — and everything but the heap-only parts must give the same answers.

#include "gpu_fixture.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "bindless.h"
#include "buffer.h"
#include "commandBuffer.h"
#include "computePipeline.h"
#include "context.h"
#include "descriptor.h"
#include "descriptorHeap.h"
#include "descriptorSet.h"
#include "image.h"
#include "log.h"
#include "sampler.h"
#include "shader.h"

namespace {

using kor::Bindless;
using kor::Buffer;
using kor::CommandBuffer;
using kor::ComputePipeline;
using kor::DescriptorSet;
using kor::Image;
using kor::Sampler;
using kor::Shader;

constexpr kor::u32 Count = 100;
constexpr kor::Vec4 Color{ 0.25f, 0.5f, 0.75f, 1.f };

bool HeapRun()
{
    const char* forced = std::getenv("KORAL_DESCRIPTORS");
    return forced && std::string(forced) == "heap" && kor::Context::GpuHas(kor::Feature::eDescriptorHeap);
}

kor::ResourceRef<const Shader> Entry(const char* module, const char* entry)
{
    Shader::AddSearchPath(KORAL_TEST_SHADERS_DIR);
    return Shader::Builder{}.SetEntryPoint(module, entry).GetOrBuild();
}

void ExpectNoValidationErrors(const std::uint64_t since)
{
    for (const auto& record : kor::log::HistorySince(since))
        EXPECT_EQ(record.message.find("VUID"), std::string::npos) << record.message;
}

kor::Resource<Buffer> Values(const float scale = 1.f)
{
    std::vector<float> values(Count);
    for (kor::u32 i = 0; i < Count; ++i) values[i] = static_cast<float>(i) * scale;
    return Buffer::Builder<float>{}.SetData(values)
        .SetUsage(Buffer::Usage::eStorage | Buffer::Usage::eTransferDst | Buffer::Usage::eTransferSrc).Build();
}

kor::Resource<Buffer> Output()
{
    return Buffer::Builder<kor::Vec4>{}.SetData(std::vector<kor::Vec4>(Count, kor::Vec4{ -1.f }))
        .SetUsage(Buffer::Usage::eStorage | Buffer::Usage::eTransferDst | Buffer::Usage::eTransferSrc).Build();
}

kor::Resource<Image> Texture()
{
    const std::vector<kor::Vec4> texels(4, Color);
    return Image::Builder{}
        .SetType(Image::Type::e2D)
        .SetFormat(Image::Format::eRGBA32_SFLOAT)
        .SetExtent(kor::UVec2{ 2, 2 })
        .SetUsage(Image::Usage::eSampled | Image::Usage::eTransferDst)
        .SetData(texels.data(), texels.size() * sizeof(kor::Vec4))
        .Build();
}

kor::Resource<Sampler> Linear()
{
    return Sampler::Builder{}.SetMinFilter(kor::Filter::eLinear).SetMagFilter(kor::Filter::eLinear).Build();
}

void ExpectColor(const kor::Vec4& got)
{
    EXPECT_NEAR(got.x, Color.x, 1e-3f);
    EXPECT_NEAR(got.y, Color.y, 1e-3f);
    EXPECT_NEAR(got.z, Color.z, 1e-3f);
    EXPECT_NEAR(got.w, Color.w, 1e-3f);
}

TEST_F(GpuTest, TheDeviceIsMadeForTheModelAskedFor) {
    const auto active = kor::Context::ActiveDescriptorModel();
    EXPECT_EQ(active == kor::DescriptorModel::eHeap, kor::Context::Supports(kor::Feature::eDescriptorHeap));
    EXPECT_EQ(active == kor::DescriptorModel::eHeap, HeapRun());
    EXPECT_STREQ(kor::DescriptorModelName(active), HeapRun() ? "heap" : "sets");
}

TEST_F(GpuTest, AStorageBufferIsReadThroughItsBindlessIndex) {
    const auto pipeline = ComputePipeline::Builder{}.SetComputeShader(Entry("descriptorModel", "bindlessBuffer")).Build();
    ASSERT_TRUE(pipeline.Valid()) << pipeline.Failure()->History();
    EXPECT_TRUE(pipeline->UsesBindless());

    const auto input = Values();
    const auto output = Output();
    const std::uint64_t before = kor::log::LastSequence();

    const auto index = Bindless::StorageBuffer(input);
    ASSERT_TRUE(index.Valid());
    EXPECT_EQ(index.kind, kor::BindlessKind::eStorageBuffer);
    // Asked again, the same resource keeps its index.
    EXPECT_EQ(Bindless::StorageBuffer(input), index);
    EXPECT_GT(Bindless::Capacity(kor::BindlessKind::eStorageBuffer), index.index);

    const auto set = DescriptorSet::Builder(pipeline, 0).Write("output", output).Build();
    ASSERT_TRUE(set.Valid()) << set.Failure()->History();
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BindComputePipeline(pipeline).BindDescriptorSet(0, set)
          .PushConstant("source", index).PushConstant("count", Count)
          .Dispatch((Count + 63) / 64, 1, 1);
        EXPECT_TRUE(cb.Errors().empty());
    }, CommandBuffer::Usage::eCompute).Wait();

    const auto result = output->Read<kor::Vec4>();
    for (kor::u32 i = 0; i < Count; ++i) ASSERT_FLOAT_EQ(result[i].x, static_cast<float>(i) * 2) << i;
    ExpectNoValidationErrors(before);
    Bindless::Release(index);
}

TEST_F(GpuTest, ATextureIsSampledThroughBindlessIndices) {
    const auto pipeline = ComputePipeline::Builder{}.SetComputeShader(Entry("descriptorModel", "bindlessTexture")).Build();
    ASSERT_TRUE(pipeline.Valid()) << pipeline.Failure()->History();
    const auto texture = Texture();
    const auto sampler = Linear();
    const auto output = Output();
    const std::uint64_t before = kor::log::LastSequence();

    const auto image = Bindless::Sampled(texture);
    const auto filter = Bindless::Sampler(sampler);
    ASSERT_TRUE(image.Valid());
    ASSERT_TRUE(filter.Valid());

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        // The pushed index carries its image into the dispatch's uses: no barrier is written for it.
        cb.BindComputePipeline(pipeline).Bind("output", output)
          .PushConstant("texture", image).PushConstant("sampler", filter)
          .Dispatch(1, 1, 1);
        EXPECT_TRUE(cb.Errors().empty());
    }, CommandBuffer::Usage::eCompute).Wait();

    ExpectColor(output->Read<kor::Vec4>()[0]);
    ExpectNoValidationErrors(before);
}

TEST_F(GpuTest, AStorageImageIsWrittenThroughItsBindlessIndex) {
    const auto pipeline = ComputePipeline::Builder{}.SetComputeShader(Entry("descriptorModel", "bindlessImage")).Build();
    ASSERT_TRUE(pipeline.Valid()) << pipeline.Failure()->History();
    const auto image = Image::Builder{}
        .SetType(Image::Type::e2D)
        .SetFormat(Image::Format::eRGBA32_SFLOAT)
        .SetExtent(kor::UVec2{ 2, 2 })
        .SetUsage(Image::Usage::eStorage | Image::Usage::eTransferSrc)
        .Build();
    ASSERT_TRUE(image.Valid());
    const auto readback = Buffer::Builder<kor::Vec4>{}.SetInstanceCount(4)
        .SetUsage(Buffer::Usage::eTransferDst).SetType(Buffer::Type::eReadback).Build();
    const std::uint64_t before = kor::log::LastSequence();

    const auto index = Bindless::Storage(image);
    ASSERT_TRUE(index.Valid());
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        // The pushed index puts the image in the general layout for the dispatch, and the copy after it
        // is ordered after the writes: both from the push alone.
        cb.BindComputePipeline(pipeline).PushConstant("texture", index).Dispatch(1, 1, 1);
        cb.CopyImageToBuffer(image, readback);
        EXPECT_TRUE(cb.Errors().empty());
    }, CommandBuffer::Usage::eCompute).Wait();

    for (const auto& texel : readback->Read<kor::Vec4>()) ExpectColor(texel);
    ExpectNoValidationErrors(before);
}

TEST_F(GpuTest, AResourcePushedByNameGoesInAsItsBindlessIndex) {
    const auto pipeline = ComputePipeline::Builder{}.SetComputeShader(Entry("descriptorModel", "bindlessTexture")).Build();
    ASSERT_TRUE(pipeline.Valid()) << pipeline.Failure()->History();
    const auto texture = Texture();
    const auto sampler = Linear();
    const auto output = Output();
    const std::uint64_t before = kor::log::LastSequence();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        // No kor::Bindless call: the texture is sampled-only, so it can only be a sampled image.
        cb.BindComputePipeline(pipeline).Bind("output", output)
          .PushConstant("texture", texture).PushConstant("sampler", sampler)
          .Dispatch(1, 1, 1);
        EXPECT_TRUE(cb.Errors().empty());
    }, CommandBuffer::Usage::eCompute).Wait();

    ExpectColor(output->Read<kor::Vec4>()[0]);
    // The push asked for the same index an explicit call is handed.
    EXPECT_EQ(Bindless::Sampled(texture).kind, kor::BindlessKind::eSampledImage);
    ExpectNoValidationErrors(before);
}

TEST_F(GpuTest, AnImageThatCouldBeEitherIsSettledByWhatThePipelineReads) {
    const auto pipeline = ComputePipeline::Builder{}.SetComputeShader(Entry("descriptorModel", "bindlessImage")).Build();
    ASSERT_TRUE(pipeline.Valid()) << pipeline.Failure()->History();
    const auto image = Image::Builder{}
        .SetType(Image::Type::e2D)
        .SetFormat(Image::Format::eRGBA32_SFLOAT)
        .SetExtent(kor::UVec2{ 2, 2 })
        .SetUsage(Image::Usage::eStorage | Image::Usage::eSampled | Image::Usage::eTransferSrc)
        .Build();
    ASSERT_TRUE(image.Valid());
    const auto readback = Buffer::Builder<kor::Vec4>{}.SetInstanceCount(4)
        .SetUsage(Buffer::Usage::eTransferDst).SetType(Buffer::Type::eReadback).Build();
    const std::uint64_t before = kor::log::LastSequence();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        // Sampled or storage: the shader reads only the storage-image table, so storage.
        cb.BindComputePipeline(pipeline).PushConstant("texture", image).Dispatch(1, 1, 1);
        cb.CopyImageToBuffer(image, readback);
        EXPECT_TRUE(cb.Errors().empty());
    }, CommandBuffer::Usage::eCompute).Wait();

    for (const auto& texel : readback->Read<kor::Vec4>()) ExpectColor(texel);
    ExpectNoValidationErrors(before);
}

TEST_F(GpuTest, AResourceNothingSettlesTheTableOfIsRefused) {
    // This pipeline reads neither image table, so an image that can be sampled and stored is ambiguous.
    const auto pipeline = ComputePipeline::Builder{}.SetComputeShader(Entry("descriptorModel", "bindlessBuffer")).Build();
    ASSERT_TRUE(pipeline.Valid()) << pipeline.Failure()->History();
    const auto image = Image::Builder{}
        .SetType(Image::Type::e2D)
        .SetFormat(Image::Format::eRGBA32_SFLOAT)
        .SetExtent(kor::UVec2{ 2, 2 })
        .SetUsage(Image::Usage::eStorage | Image::Usage::eSampled)
        .Build();
    const auto vertices = Buffer::Builder<float>{}.SetInstanceCount(4).SetUsage(Buffer::Usage::eVertex).Build();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BindComputePipeline(pipeline).PushConstant("texture", image);
        ASSERT_FALSE(cb.Errors().empty());
        EXPECT_NE(cb.Errors().front().message.find("Bindless::Sampled"), std::string::npos) << cb.Errors().front().message;
    }, CommandBuffer::Usage::eCompute).Wait();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BindComputePipeline(pipeline).PushConstant("source", vertices);   // neither storage nor uniform
        ASSERT_FALSE(cb.Errors().empty());
        EXPECT_NE(cb.Errors().front().message.find("neither"), std::string::npos) << cb.Errors().front().message;
    }, CommandBuffer::Usage::eCompute).Wait();
}

TEST_F(GpuTest, ASlangHandleSaysWhichTableItIndexes) {
    const auto pipeline = ComputePipeline::Builder{}.SetComputeShader(Entry("descriptorHandles", "images")).Build();
    ASSERT_TRUE(pipeline.Valid()) << pipeline.Failure()->History();
    const auto* target = pipeline->FindPushConstant("push.target");
    ASSERT_NE(target, nullptr);
    EXPECT_EQ(target->handle, static_cast<kor::u8>(kor::BindlessKind::eStorageImage));
    EXPECT_EQ(target->size, 8u);   // a handle is two words
    EXPECT_EQ(pipeline->FindPushConstant("push.sampler")->handle, static_cast<kor::u8>(kor::BindlessKind::eSampler));
    // A field of the entry point's `uniform` struct is found by its own name too.
    EXPECT_EQ(pipeline->FindPushConstant("target"), target);

    // Sampled or storage by its usage; the handle's type says storage.
    const auto image = Image::Builder{}
        .SetType(Image::Type::e2D)
        .SetFormat(Image::Format::eRGBA32_SFLOAT)
        .SetExtent(kor::UVec2{ 2, 2 })
        .SetUsage(Image::Usage::eStorage | Image::Usage::eSampled | Image::Usage::eTransferSrc)
        .Build();
    const auto texture = Texture();
    const auto sampler = Linear();
    const auto readback = Buffer::Builder<kor::Vec4>{}.SetInstanceCount(4)
        .SetUsage(Buffer::Usage::eTransferDst).SetType(Buffer::Type::eReadback).Build();
    const std::uint64_t before = kor::log::LastSequence();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BindComputePipeline(pipeline)
          .PushConstant("push.target", image)
          .PushConstant("texture", texture)                          // "push.texture", by its own name
          .PushConstant("push.sampler", Bindless::Sampler(sampler))   // an index goes into a handle too
          .Dispatch(1, 1, 1);
        cb.CopyImageToBuffer(image, readback);
        EXPECT_TRUE(cb.Errors().empty()) << (cb.Errors().empty() ? "" : cb.Errors().front().message);
    }, CommandBuffer::Usage::eCompute).Wait();

    for (const auto& texel : readback->Read<kor::Vec4>()) ExpectColor(texel);
    ExpectNoValidationErrors(before);
}

TEST_F(GpuTest, HandlesInAStructPushedWholeAreSynchronisedFor) {
    const auto pipeline = ComputePipeline::Builder{}.SetComputeShader(Entry("descriptorHandles", "images")).Build();
    ASSERT_TRUE(pipeline.Valid()) << pipeline.Failure()->History();
    const auto image = Image::Builder{}
        .SetType(Image::Type::e2D)
        .SetFormat(Image::Format::eRGBA32_SFLOAT)
        .SetExtent(kor::UVec2{ 2, 2 })
        .SetUsage(Image::Usage::eStorage | Image::Usage::eTransferSrc)
        .Build();
    const auto texture = Texture();
    const auto sampler = Linear();
    const auto readback = Buffer::Builder<kor::Vec4>{}.SetInstanceCount(4)
        .SetUsage(Buffer::Usage::eTransferDst).SetType(Buffer::Type::eReadback).Build();
    const std::uint64_t before = kor::log::LastSequence();

    // Laid out as the shader's struct: a DescriptorIndex is two words, as a handle is.
    struct Images { kor::DescriptorIndex target, texture, sampler; };
    static_assert(sizeof(Images) == 24);
    const Images push{ Bindless::Storage(image), Bindless::Sampled(texture), Bindless::Sampler(sampler) };

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        // Copied as it stands — and the bytes on the handles still tell the resolver what the dispatch reaches, so
        // the storage image is in the general layout for it and the copy after waits for its writes.
        cb.BindComputePipeline(pipeline).PushConstant("push", push).Dispatch(1, 1, 1);
        // What the resolver runs on: the dispatch declared the storage image and the texture as uses.
        EXPECT_TRUE(cb.HasTouched(image));
        EXPECT_TRUE(cb.HasTouched(texture));
        cb.CopyImageToBuffer(image, readback);
        EXPECT_TRUE(cb.Errors().empty()) << (cb.Errors().empty() ? "" : cb.Errors().front().message);
    }, CommandBuffer::Usage::eCompute).Wait();

    for (const auto& texel : readback->Read<kor::Vec4>()) ExpectColor(texel);
    ExpectNoValidationErrors(before);
}

TEST_F(GpuTest, BufferHandlesInAPushConstantBlock) {
    const auto pipeline = ComputePipeline::Builder{}.SetComputeShader(Entry("descriptorHandles", "buffers")).Build();
    ASSERT_TRUE(pipeline.Valid()) << pipeline.Failure()->History();
    const auto input = Values();
    const auto output = Output();
    const std::uint64_t before = kor::log::LastSequence();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BindComputePipeline(pipeline)
          .PushConstant("source", input).PushConstant("output", output).PushConstant("count", Count)
          .Dispatch((Count + 63) / 64, 1, 1);
        EXPECT_TRUE(cb.Errors().empty()) << (cb.Errors().empty() ? "" : cb.Errors().front().message);
    }, CommandBuffer::Usage::eCompute).Wait();

    const auto result = output->Read<kor::Vec4>();
    for (kor::u32 i = 0; i < Count; ++i) ASSERT_FLOAT_EQ(result[i].x, static_cast<float>(i) * 2) << i;
    ExpectNoValidationErrors(before);
}

TEST_F(GpuTest, WhatAHandleCannotHoldIsRefused) {
    const auto pipeline = ComputePipeline::Builder{}.SetComputeShader(Entry("descriptorHandles", "images")).Build();
    ASSERT_TRUE(pipeline.Valid()) << pipeline.Failure()->History();
    const auto texture = Texture();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        // A sampled-image index, into a storage-image handle.
        cb.BindComputePipeline(pipeline).PushConstant("push.target", Bindless::Sampled(texture));
        ASSERT_FALSE(cb.Errors().empty());
        EXPECT_NE(cb.Errors().front().message.find("storage image"), std::string::npos) << cb.Errors().front().message;
    }, CommandBuffer::Usage::eCompute).Wait();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        // A buffer, into an image handle.
        cb.BindComputePipeline(pipeline).PushConstant("push.texture", Values());
        ASSERT_FALSE(cb.Errors().empty());
        EXPECT_NE(cb.Errors().front().message.find("sampled image"), std::string::npos) << cb.Errors().front().message;
    }, CommandBuffer::Usage::eCompute).Wait();
}

TEST_F(GpuTest, AnIndexIsRefusedForWhatCannotHaveOne) {
    const auto storageless = Buffer::Builder<float>{}.SetInstanceCount(4).SetUsage(Buffer::Usage::eUniform).Build();
    EXPECT_FALSE(Bindless::StorageBuffer(storageless).Valid());
    EXPECT_FALSE(Bindless::Storage(Texture()).Valid());    // sampled only
    EXPECT_FALSE(Bindless::StorageBuffer(kor::ResourceRef<const Buffer>{}).Valid());
}

TEST_F(GpuTest, AnEmbeddedSamplerNeedsNothingWritten) {
    const auto sampler = Linear();
    const auto pipeline = ComputePipeline::Builder{}
        .SetComputeShader(Entry("descriptorModel", "embeddedSamplers"))
        .SetSampler("embedded", sampler)
        .SetSampler("combined", sampler)
        .Build();
    ASSERT_TRUE(pipeline.Valid()) << pipeline.Failure()->History();
    const auto texture = Texture();
    const auto output = Output();
    const std::uint64_t before = kor::log::LastSequence();

    // No sampler anywhere: the image alone, at both the image binding and the combined one.
    const auto set = DescriptorSet::Builder(pipeline, 0)
        .Write("output", output).Write("image", texture).Write("combined", texture).Build();
    ASSERT_TRUE(set.Valid()) << set.Failure()->History();
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BindComputePipeline(pipeline).BindDescriptorSet(0, set).Dispatch(1, 1, 1);
    }, CommandBuffer::Usage::eCompute).Wait();

    const auto result = output->Read<kor::Vec4>();
    ExpectColor(result[0]);
    ExpectColor(result[1]);
    ExpectNoValidationErrors(before);

    // And a sampler written where one is embedded is the builder's mistake, said.
    const auto twice = DescriptorSet::Builder(pipeline, 0)
        .Write("output", output).Write("image", texture).Write("combined", texture).Write("embedded", sampler).Build();
    EXPECT_FALSE(twice.Valid());
}

TEST_F(GpuTest, AnEmbeddedSamplerHasToNameASamplerBinding) {
    const auto pipeline = ComputePipeline::Builder{}
        .SetComputeShader(Entry("descriptorModel", "embeddedSamplers"))
        .SetSampler("output", Linear())
        .Build();
    EXPECT_FALSE(pipeline.Valid());
    const auto missing = ComputePipeline::Builder{}
        .SetComputeShader(Entry("descriptorModel", "embeddedSamplers"))
        .SetSampler("nothing", Linear())
        .Build();
    EXPECT_FALSE(missing.Valid());
}

TEST_F(GpuTest, ResourcesAreBoundByNameWithoutASet) {
    const auto pipeline = ComputePipeline::Builder{}.SetComputeShader(Entry("bindingSplit", "computeMain")).Build();
    ASSERT_TRUE(pipeline.Valid()) << pipeline.Failure()->History();
    const auto input = Values();
    const auto output = Buffer::Builder<float>{}.SetInstanceCount(Count)
        .SetUsage(Buffer::Usage::eStorage | Buffer::Usage::eTransferSrc).Build();
    const std::uint64_t before = kor::log::LastSequence();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BindComputePipeline(pipeline).Bind("input", input).Bind("output", output)
          .PushConstant("count", Count).PushConstant("bias", 1.f)
          .Dispatch((Count + 63) / 64, 1, 1);
        EXPECT_TRUE(cb.Errors().empty());
    }, CommandBuffer::Usage::eCompute).Wait();

    const auto result = output->Read<float>();
    for (kor::u32 i = 0; i < Count; ++i) ASSERT_FLOAT_EQ(result[i], static_cast<float>(i) * 2 + 0.5f + 1.f) << i;
    ExpectNoValidationErrors(before);
}

TEST_F(GpuTest, BindingByNamePatchesTheBoundSet) {
    const auto pipeline = ComputePipeline::Builder{}.SetComputeShader(Entry("bindingSplit", "computeMain")).Build();
    ASSERT_TRUE(pipeline.Valid());
    const auto first = Values();
    const auto second = Values(10.f);
    const auto output = Buffer::Builder<float>{}.SetInstanceCount(Count)
        .SetUsage(Buffer::Usage::eStorage | Buffer::Usage::eTransferSrc).Build();
    const auto set = DescriptorSet::Builder(pipeline, 0).Write("input", first).Write("output", output).Build();
    ASSERT_TRUE(set.Valid());

    // The set says `first`; the name says `second`, and the set's `output` stays.
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BindComputePipeline(pipeline).BindDescriptorSet(0, set).Bind("input", second)
          .PushConstant("count", Count).PushConstant("bias", 0.f)
          .Dispatch((Count + 63) / 64, 1, 1);
        EXPECT_TRUE(cb.Errors().empty());
    }, CommandBuffer::Usage::eCompute).Wait();

    EXPECT_FLOAT_EQ(output->Read<float>()[3], 3.f * 10 * 2 + 0.5f);
}

TEST_F(GpuTest, BindingANameNoBindingHasFailsTheRecording) {
    const auto pipeline = ComputePipeline::Builder{}.SetComputeShader(Entry("bindingSplit", "computeMain")).Build();
    ASSERT_TRUE(pipeline.Valid());
    const auto input = Values();
    auto cb = CommandBuffer::Create(CommandBuffer::Usage::eCompute);
    cb->Begin();
    cb->BindComputePipeline(pipeline).Bind("inptu", input);
    ASSERT_FALSE(cb->Errors().empty());
    EXPECT_NE(cb->Errors().front().message.find("'input'"), std::string::npos) << cb->Errors().front().message;
    cb->End();
}

TEST_F(GpuTest, PushConstantsAndSetsShareTheHeapsPushData) {
    const auto pipeline = ComputePipeline::Builder{}.SetComputeShader(Entry("widePush", "widePush")).Build();
    if (HeapRun()) {
        ASSERT_FALSE(pipeline.Valid());
        EXPECT_NE(pipeline.Failure()->History().find("push data"), std::string::npos) << pipeline.Failure()->History();
    } else {
        EXPECT_TRUE(pipeline.Valid()) << pipeline.Failure()->History();
    }
}

TEST_F(GpuTest, AProjectFillsItsOwnDescriptorHeap) {
    auto heap = kor::DescriptorHeap::Builder{}.SetSlotCount(4).Build();
    if (!HeapRun()) {
        ASSERT_FALSE(heap.Valid());
        EXPECT_EQ(heap.Failure()->code, kor::ErrorCode::eDescriptorHeapUnsupported);
        EXPECT_FALSE(kor::DescriptorHeap::Encode(kor::Descriptor(Values()), kor::DescriptorType::eStorageBuffer).has_value());
        return;
    }
    ASSERT_TRUE(heap.Valid()) << heap.Failure()->History();
    EXPECT_EQ(heap->SlotCount(), 4u);

    const auto pipeline = ComputePipeline::Builder{}.SetComputeShader(Entry("descriptorModel", "bindlessBuffer")).Build();
    ASSERT_TRUE(pipeline.Valid());
    const auto first = Values();
    const auto second = Values(3.f);
    const auto output = Output();
    const std::uint64_t before = kor::log::LastSequence();

    const auto run = [&](const kor::DescriptorIndex index, const kor::ResourceRef<const Buffer> read) {
        CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
            cb.BindComputePipeline(pipeline).Bind("output", output).Uses(read)
              .PushConstantBlock(index.index, 0).PushConstant("count", Count)
              .Dispatch((Count + 63) / 64, 1, 1);
            EXPECT_TRUE(cb.Errors().empty());
        }, CommandBuffer::Usage::eCompute).Wait();
        return output->Read<kor::Vec4>();
    };

    // Written from the CPU, read through the bindless table at its index.
    ASSERT_TRUE(heap->Write(2, kor::Descriptor(first), kor::DescriptorType::eStorageBuffer).has_value());
    EXPECT_FLOAT_EQ(run(heap->Index(2, kor::BindlessKind::eStorageBuffer), first)[7].x, 14.f);

    // Encoded, put in a buffer, and copied into place by the GPU.
    const auto bytes = kor::DescriptorHeap::Encode(kor::Descriptor(second), kor::DescriptorType::eStorageBuffer);
    ASSERT_TRUE(bytes.has_value());
    EXPECT_EQ(bytes->size(), kor::DescriptorHeap::SlotSize(kor::HeapRole::eResource));
    const auto staged = Buffer::Builder<std::byte>{}.SetData(*bytes)
        .SetUsage(Buffer::Usage::eTransferSrc | Buffer::Usage::eTransferDst).Build();
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyDescriptors(staged, 0, heap, 3, 1);
        EXPECT_TRUE(cb.Errors().empty());
    }, CommandBuffer::Usage::eTransfer).Wait();
    EXPECT_FLOAT_EQ(run(heap->Index(3, kor::BindlessKind::eStorageBuffer), second)[7].x, 42.f);

    // A sampler is not a resource-heap descriptor.
    EXPECT_FALSE(heap->Write(0, kor::Descriptor(Linear()), kor::DescriptorType::eSampler).has_value());
    EXPECT_FALSE(heap->Write(9, kor::Descriptor(first), kor::DescriptorType::eStorageBuffer).has_value());
    ExpectNoValidationErrors(before);
}

}
