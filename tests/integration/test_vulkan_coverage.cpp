// Broad coverage tests for Vulkan-backend classes that the focused round-trip
// tests never touch: samplers, every descriptor-binding type, runtime descriptor
// writes, image variety (3D / arrays / mips), and the transfer/label command
// paths. These don't assert much beyond "it runs on a real device without a
// validation error"; their job is to drive the backend switch statements and
// builders that would otherwise be dead in coverage.

#include <array>
#include "gpu_fixture.h"

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "buffer.h"
#include "commandBuffer.h"
#include "context.h"
#include "descriptor.h"
#include "descriptorSet.h"
#include "descriptorSetLayout.h"
#include "framebuffer.h"
#include "image.h"
#include "imageView.h"
#include "log.h"
#include "sampler.h"

using kor::Buffer;
using kor::CommandBuffer;
using kor::Descriptor;
using kor::DescriptorSet;
using kor::DescriptorSetLayout;
using kor::DescriptorType;
using kor::Image;
using kor::ImageView;
using kor::ResourceRef;
using kor::Sampler;

namespace {

// Build samplers across the filter/address/mip/compare permutations so the
// sampler builder and its enum-conversion helpers are all exercised.
// Run() must park its lambda in the recorded stream, not execute it on the spot.
//
// It is the one way into the raw backend command buffer — a library with a renderer of its own
// records its draws through it — and commands are emitted at End(), not as they are called. A Run that
// fired immediately would put those raw calls ahead of the entire recorded frame instead of
// where they were written, which is exactly what stopped the GUI from appearing in 0.0.8: it
// drew first and the scene painted over it. The Vulkan backend ran the lambda inline while the
// OpenGL one had always enqueued, so this also pins the two to the same behaviour.
TEST_F(GpuTest, RunIsDeferredUntilEnd) {
    int ranAt = 0;      // 0 = not yet, 1 = during recording, 2 = during End()
    int phase = 1;

    const auto cb = kor::CommandBuffer::Create(kor::CommandBuffer::Usage::eGraphics);
    cb->Begin();
    cb->Run([&](kor::CommandBuffer&) { ranAt = phase; });

    EXPECT_EQ(ranAt, 0) << "Run executed while the frame was still being recorded";

    phase = 2;
    cb->End();

    EXPECT_EQ(ranAt, 2) << "Run never executed, or executed outside End()";
}

TEST_F(GpuTest, SamplerBuildVariants) {
    auto linear =
        Sampler::Builder{}
            .SetMinFilter(kor::Filter::eLinear)
            .SetMagFilter(kor::Filter::eLinear)
            .SetMipmapMode(Sampler::MipmapMode::eLinear)
            .SetAddressModeU(Sampler::AddressMode::eRepeat)
            .SetAddressModeV(Sampler::AddressMode::eMirroredRepeat)
            .SetAddressModeW(Sampler::AddressMode::eClampToEdge)
            .SetMaxLod(4.f)
            .Build();
    ASSERT_TRUE(static_cast<bool>(linear));

    auto nearest =
        Sampler::Builder{}
            .SetMinFilter(kor::Filter::eNearest)
            .SetMagFilter(kor::Filter::eNearest)
            .SetMipmapMode(Sampler::MipmapMode::eNearest)
            .SetAddressModeU(Sampler::AddressMode::eClampToBorder)
            .SetCompareEnable(true)
            .SetCompareOp(kor::CompareOp::eLess)
            .Build();
    ASSERT_TRUE(static_cast<bool>(nearest));
}

// Helper: an 8x8 image with the given usage, plus a default 2D view.
kor::Resource<Image> makeImage(kor::Flags<Image::Usage> usage,
                               Image::Format format = Image::Format::eRGBA8_UNORM) {
    return Image::Builder{}
        .SetType(Image::Type::e2D)
        .SetFormat(format)
        .SetExtent(glm::uvec2{8, 8})
        .SetUsage(usage)
        .Build();
}

// Build a descriptor set for each non-buffer/non-RT binding type. This drives the
// eSampler / eSampledImage / eStorageImage / eCombinedImageSampler / eUniformBuffer
// arms of the descriptor-set constructor switch, which the compute test (storage
// buffer only) leaves uncovered.
TEST_F(GpuTest, DescriptorTypesBuild) {
    auto sampledImg = makeImage(kor::Flags(Image::Usage::eSampled) | Image::Usage::eTransferDst);
    auto storageImg = makeImage(kor::Flags(Image::Usage::eStorage) | Image::Usage::eTransferDst);
    auto sampledView = ImageView::Builder(sampledImg).Build();
    auto storageView = ImageView::Builder(storageImg).Build();

    auto sampler = Sampler::Builder{}.Build();

    Buffer::RawBuilder ub;
    ub.SetRawSize(256).SetUsage(Buffer::Usage::eUniform).SetType(Buffer::Type::eDynamic);
    auto uniform = ub.Build();

    // --- eSampler ---------------------------------------------------------
    {
        auto layout = DescriptorSetLayout::Builder{}.AddBinding(0, DescriptorType::eSampler).Build();
        auto set = DescriptorSet::Builder(*layout)
                       .Write(0, sampler)
                       .Build();
        ASSERT_TRUE(static_cast<bool>(set));
    }
    // --- eSampledImage ----------------------------------------------------
    {
        auto layout = DescriptorSetLayout::Builder{}.AddBinding(0, DescriptorType::eSampledImage).Build();
        auto set = DescriptorSet::Builder(*layout)
                       .Write(0, sampledView)
                       .Build();
        ASSERT_TRUE(static_cast<bool>(set));
    }
    // --- eStorageImage ----------------------------------------------------
    {
        auto layout = DescriptorSetLayout::Builder{}.AddBinding(0, DescriptorType::eStorageImage).Build();
        auto set = DescriptorSet::Builder(*layout)
                       .Write(0, storageView)
                       .Build();
        ASSERT_TRUE(static_cast<bool>(set));
    }
    // --- eCombinedImageSampler -------------------------------------------
    {
        auto layout = DescriptorSetLayout::Builder{}.AddBinding(0, DescriptorType::eCombinedImageSampler).Build();
        auto set = DescriptorSet::Builder(*layout)
                       .Write(0, sampledView, sampler)
                       .Build();
        ASSERT_TRUE(static_cast<bool>(set));
    }
    // --- eUniformBuffer + multi-binding set -------------------------------
    {
        auto layout = DescriptorSetLayout::Builder{}
                          .AddBinding(0, DescriptorType::eUniformBuffer)
                          .AddBinding(1, DescriptorType::eCombinedImageSampler)
                          .Build();
        auto set = DescriptorSet::Builder(*layout)
                       .Write(0, uniform)
                       .Write(1, sampledView, sampler)
                       .Build();
        ASSERT_TRUE(static_cast<bool>(set));
    }
}

// Exercise the runtime DescriptorSet::Rebind() path (as opposed to writes baked in
// at build time), which is a second, separate switch in the backend.
TEST_F(GpuTest, DescriptorRuntimeWrite) {
    auto sampler = Sampler::Builder{}.Build();
    auto img = makeImage(kor::Flags(Image::Usage::eStorage) | Image::Usage::eSampled | Image::Usage::eTransferDst);
    auto view = ImageView::Builder(img).Build();

    Buffer::RawBuilder sb;
    sb.SetRawSize(256).SetUsage(Buffer::Usage::eStorage).SetType(Buffer::Type::eDynamic);
    auto storage = sb.Build();
    Buffer::RawBuilder ub;
    ub.SetRawSize(256).SetUsage(Buffer::Usage::eUniform).SetType(Buffer::Type::eDynamic);
    auto uniform = ub.Build();

    auto layout = DescriptorSetLayout::Builder{}
                      .AddBinding(0, DescriptorType::eStorageBuffer)
                      .AddBinding(1, DescriptorType::eStorageImage)
                      .AddBinding(2, DescriptorType::eSampler)
                      .AddBinding(3, DescriptorType::eSampledImage)
                      .AddBinding(4, DescriptorType::eUniformBuffer)
                      .AddBinding(5, DescriptorType::eCombinedImageSampler)
                      .Build();

    // Every layout binding must be written at build time (unwritten bindings hold
    // default-invalid descriptors that the constructor would choke on).
    auto set = DescriptorSet::Builder(*layout)
                   .Write(0, storage)
                   .Write(1, view)
                   .Write(2, sampler)
                   .Write(3, view)
                   .Write(4, uniform)
                   .Write(5, view, sampler)
                   .Build();
    ASSERT_TRUE(static_cast<bool>(set));

    // Now re-issue each binding through the runtime DescriptorSet::Rebind() path,
    // which is a separate switch from the build-time writes above.
    set->Rebind(0, storage, 0);
    set->Rebind(1, view, 0);
    set->Rebind(2, sampler, 0);
    set->Rebind(3, view, 0);
    set->Rebind(4, uniform, 0);
    set->Rebind(5, view, sampler, 0);
    set->DebugPrint(); // exercise the debug dump path
    SUCCEED();
}

// 3D and array images plus a single-channel format, to walk the image-creation
// and view-creation paths that a plain 2D RGBA8 image never reaches.
TEST_F(GpuTest, ImageDimensionVariety) {
    auto image3d = Image::Builder{}
                       .SetType(Image::Type::e3D)
                       .SetFormat(Image::Format::eRGBA8_UNORM)
                       .SetExtent(glm::uvec3{8, 8, 4})
                       .SetUsage(Image::Usage::eTransferDst | Image::Usage::eSampled)
                       .Build();
    ASSERT_TRUE(static_cast<bool>(image3d));
    EXPECT_EQ(image3d->Extent(), glm::uvec3(8, 8, 4));

    auto view3d = ImageView::Builder(image3d)
                      .SetViewType(ImageView::Type::e3D)
                      .Build();
    ASSERT_TRUE(static_cast<bool>(view3d));

    auto arrayImg = Image::Builder{}
                        .SetType(Image::Type::e2D)
                        .SetFormat(Image::Format::eR8_UNORM)
                        .SetExtent(glm::uvec2{8, 8})
                        .SetArrayLayers(3)
                        .SetUsage(Image::Usage::eTransferDst | Image::Usage::eSampled)
                        .Build();
    ASSERT_TRUE(static_cast<bool>(arrayImg));

    auto arrayView = ImageView::Builder(arrayImg)
                         .SetViewType(ImageView::Type::e2DArray)
                         .SetArrayLayerCount(3)
                         .Build();
    ASSERT_TRUE(static_cast<bool>(arrayView));
}

// GenerateMipmaps walks every mip level with vkCmdBlitImage and inserts the
// per-level barriers — a large uncovered stretch of the command buffer.
TEST_F(GpuTest, GenerateMipmapsRuns) {
    auto image = Image::Builder{}
                     .SetType(Image::Type::e2D)
                     .SetFormat(Image::Format::eRGBA8_UNORM)
                     .SetExtent(glm::uvec2{8, 8})
                     .SetMipLevels(4) // 8 -> 4 -> 2 -> 1
                     .SetUsage(Image::Usage::eTransferSrc | Image::Usage::eTransferDst | Image::Usage::eSampled)
                     .Build();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.ClearColorImage(image, glm::vec4{0.25f, 0.5f, 0.75f, 1.f});
        cb.GenerateMipmaps(image);
    }, CommandBuffer::Usage::eGraphics).Wait();
    SUCCEED();
}

// Image-to-image blit (down-scale) between two separate images.
TEST_F(GpuTest, BlitBetweenImages) {
    auto src = makeImage(kor::Flags(Image::Usage::eTransferSrc) | Image::Usage::eTransferDst);
    auto dst = Image::Builder{}
                   .SetType(Image::Type::e2D)
                   .SetFormat(Image::Format::eRGBA8_UNORM)
                   .SetExtent(glm::uvec2{4, 4})
                   .SetUsage(Image::Usage::eTransferDst | Image::Usage::eTransferSrc)
                   .Build();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.ClearColorImage(src, glm::vec4{1.f, 1.f, 0.f, 1.f});
        cb.Blit(src, dst, kor::Blit{
            .srcExtent = {8, 8, 1},
            .dstExtent = {4, 4, 1},
            .filtering = kor::Filter::eLinear,
        });
    }, CommandBuffer::Usage::eGraphics).Wait();
    SUCCEED();
}

// Buffer-to-buffer copy, fill and clear on the transfer/compute queue.
TEST_F(GpuTest, BufferTransferOps) {
    std::vector<std::uint32_t> data(64, 7u);
    Buffer::Builder<std::uint32_t> srcB;
    srcB.SetData(data);
    srcB.SetUsage(Buffer::Usage::eTransferSrc | Buffer::Usage::eTransferDst);
    srcB.SetType(Buffer::Type::eDeviceLocal);
    auto src = srcB.Build();

    Buffer::RawBuilder dstB;
    dstB.SetRawSize(static_cast<glm::i64>(data.size() * sizeof(std::uint32_t)))
        .SetUsage(Buffer::Usage::eTransferDst | Buffer::Usage::eTransferSrc)
        .SetType(Buffer::Type::eReadback);
    auto dst = dstB.Build();

    // Eight of them, because FillBuffer copies the bytes it is given rather than replicating a
    // value: asking for 8 * sizeof(uint32_t) from a single uint32_t read past the end of it.
    const std::array<std::uint32_t, 8> fillValues { 0xABCDABCDu, 0xABCDABCDu, 0xABCDABCDu, 0xABCDABCDu,
                                                    0xABCDABCDu, 0xABCDABCDu, 0xABCDABCDu, 0xABCDABCDu };
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyBuffer(src, dst);
        cb.FillBuffer(src, fillValues);
        cb.ClearBuffer(src);
    }, CommandBuffer::Usage::eTransfer).Wait();

    const std::vector<std::uint32_t> out = dst->Read<std::uint32_t>();
    ASSERT_EQ(out.size(), data.size());
    for (auto v : out) EXPECT_EQ(v, 7u);
}

// Multisample resolve: a 4x MSAA color image resolved down into a single-sample
// image. Drives the two-argument Resolve path and the MSAA image-creation branch.
TEST_F(GpuTest, ResolveMultisampleToSingle) {
    auto msaa = Image::Builder{}
                    .SetType(Image::Type::e2D)
                    .SetFormat(Image::Format::eRGBA8_UNORM)
                    .SetExtent(glm::uvec2{8, 8})
                    .SetSampleCount(kor::SampleCount::e4)
                    .SetUsage(Image::Usage::eColorAttachment | Image::Usage::eTransferSrc)
                    .Build();
    ASSERT_TRUE(static_cast<bool>(msaa));

    auto single = makeImage(kor::Flags(Image::Usage::eTransferDst) | Image::Usage::eTransferSrc);

    // The view/framebuffer must outlive queue submission, not just recording:
    // Vulkan forbids destroying objects a pending command buffer references, and
    // MoltenVK's deferred encoding only dereferences the VkImageView at submit
    // time (it segfaults if these are locals inside the recording lambda).
    auto view = ImageView::Builder(msaa).Build();
    auto fb = kor::Framebuffer::Builder{}
                  .AddColor({ .view = view, .clear = glm::vec4{0.2f, 0.4f, 0.6f, 1.f} })
                  .Build();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        // Give the MSAA image a defined layout/content via a render clear.
        cb.BeginRendering(fb);
        cb.EndRendering();
        cb.Resolve(msaa, single);
    }, CommandBuffer::Usage::eGraphics).Wait();
    SUCCEED();
}

// Negative tests: the CopyBufferToImage / CopyImageToBuffer validation branches
// (mip level, array layer, row length, layer count). Each is recorded on a
// manually-created command buffer and inspected without submitting.
TEST_F(GpuTest, CopyValidationBranches) {
    auto image = Image::Builder{}
                     .SetType(Image::Type::e2D)
                     .SetFormat(Image::Format::eRGBA8_UNORM)
                     .SetExtent(glm::uvec2{8, 8})
                     .SetUsage(Image::Usage::eTransferSrc | Image::Usage::eTransferDst)
                     .Build();

    Buffer::RawBuilder rb;
    rb.SetRawSize(static_cast<glm::i64>(8) * 8 * 4)
      .SetUsage(Buffer::Usage::eTransferSrc | Buffer::Usage::eTransferDst)
      .SetType(Buffer::Type::eStaging);
    auto buf = rb.Build();

    const auto expectReject = [&](auto&& record) {
        auto cb = CommandBuffer::Create(CommandBuffer::Usage::eTransfer);
        cb->Begin();
        record(*cb);
        cb->End();
        EXPECT_FALSE(cb->Ok());
        EXPECT_FALSE(cb->Errors().empty());
    };

    // mip level out of range
    expectReject([&](CommandBuffer& cb) {
        cb.CopyBufferToImage(buf, image, kor::Copy{ .imageMipLevel = 5 });
    });
    // base array layer out of range
    expectReject([&](CommandBuffer& cb) {
        cb.CopyBufferToImage(buf, image, kor::Copy{ .imageBaseArrayLayer = 4 });
    });
    // buffer row length smaller than the image extent
    expectReject([&](CommandBuffer& cb) {
        cb.CopyBufferToImage(buf, image, kor::Copy{ .bufferRowLength = 2, .imageExtent = {8, 8, 1} });
    });
    // buffer offset past the end of the buffer
    expectReject([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(image, buf, kor::Copy{ .bufferOffset = 999999 });
    });
}

// Debug-label commands (scoped + single markers). No-ops without a debugger, but
// they still record and must not fail the command buffer.
TEST_F(GpuTest, DebugLabelsRecord) {
    auto image = makeImage(kor::Flags(Image::Usage::eTransferDst) | Image::Usage::eTransferSrc);
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginDebugLabel("outer", glm::vec4{1.f, 0.f, 0.f, 1.f});
        cb.InsertDebugLabel("marker");
        cb.DebugLabel("scoped", [&](CommandBuffer& inner) {
            inner.ClearColorImage(image, glm::vec4{0.f, 1.f, 0.f, 1.f});
        });
        cb.EndDebugLabel();
    }, CommandBuffer::Usage::eGraphics).Wait();
    SUCCEED();
}

// Re-recording and re-submitting the same command buffer must be clean.
//
// It was not: Submit() signalled a semaphore that nothing in the engine ever waited on, and a
// binary semaphore has to be unsignalled by the time its next signal executes. So the second
// submit of any command buffer the caller reuses — the normal shape for anything iterative, and
// what a timed compute loop does every step — reported
// VUID-vkQueueSubmit-pSignalSemaphores-00067 for no reason at all. The fence was always what
// reported completion; the semaphore was write-only.
//
// Reads the validation layer's own verdict out of the log, since a signal-with-no-waiter is
// invisible from the API surface. Vacuous if the layer is not loaded, which is the same condition
// under which the whole suite stops catching Vulkan misuse.
TEST_F(GpuTest, ResubmittingACommandBufferIsValid) {
    auto image = makeImage(kor::Flags(Image::Usage::eTransferDst) | Image::Usage::eTransferSrc);

    const auto cb = CommandBuffer::Create(CommandBuffer::Usage::eGraphics);

    const auto since = [] {
        const auto history = kor::log::History();
        return history.empty() ? 0ull : history.back().sequence;
    }();

    // Three passes: the first submit is always fine, and it is the ones after it that used to
    // trip. Each is a complete record → submit → wait cycle, as a caller reusing the buffer does.
    for (int pass = 0; pass < 3; ++pass) {
        cb->Begin();
        cb->ClearColorImage(image, glm::vec4{0.f, 1.f, 0.f, 1.f});
        cb->End();
        ASSERT_TRUE(cb->Submit()) << "pass " << pass << " failed to submit";
        cb->WaitForFence();
    }

    for (const auto& record : kor::log::HistorySince(since)) {
        if (record.level != kor::log::Level::eError) continue;
        EXPECT_EQ(record.message.find("VUID"), std::string::npos)
            << "the validation layer objected to re-submitting a command buffer: " << record.message;
    }
}

} // namespace
