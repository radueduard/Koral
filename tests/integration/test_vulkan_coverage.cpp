// Broad coverage tests for Vulkan-backend classes that the focused round-trip
// tests never touch: samplers, every descriptor-binding type, runtime descriptor
// writes, image variety (3D / arrays / mips), and the transfer/label command
// paths. These don't assert much beyond "it runs on a real device without a
// validation error"; their job is to drive the backend switch statements and
// builders that would otherwise be dead in coverage.

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
#include "sampler.h"

using gfx::Buffer;
using gfx::CommandBuffer;
using gfx::Descriptor;
using gfx::DescriptorSet;
using gfx::DescriptorSetLayout;
using gfx::DescriptorType;
using gfx::Image;
using gfx::ImageView;
using gfx::ResourceRef;
using gfx::Sampler;

namespace {

// Build samplers across the filter/address/mip/compare permutations so the
// sampler builder and its enum-conversion helpers are all exercised.
TEST_F(GpuTest, SamplerBuildVariants) {
    auto linear =
        Sampler::Builder{}
            .setMinFilter(gfx::Filter::eLinear)
            .setMagFilter(gfx::Filter::eLinear)
            .setMipmapMode(Sampler::MipmapMode::eLinear)
            .setAddressModeU(Sampler::AddressMode::eRepeat)
            .setAddressModeV(Sampler::AddressMode::eMirroredRepeat)
            .setAddressModeW(Sampler::AddressMode::eClampToEdge)
            .setMaxLod(4.f)
            .build();
    ASSERT_TRUE(static_cast<bool>(linear));

    auto nearest =
        Sampler::Builder{}
            .setMinFilter(gfx::Filter::eNearest)
            .setMagFilter(gfx::Filter::eNearest)
            .setMipmapMode(Sampler::MipmapMode::eNearest)
            .setAddressModeU(Sampler::AddressMode::eClampToBorder)
            .setCompareEnable(true)
            .setCompareOp(gfx::CompareOp::eLess)
            .build();
    ASSERT_TRUE(static_cast<bool>(nearest));
}

// Helper: an 8x8 image with the given usage, plus a default 2D view.
gfx::Resource<Image> makeImage(gfx::Flags<Image::Usage> usage,
                               Image::Format format = Image::Format::eRGBA8_UNORM) {
    return Image::Builder{}
        .setType(Image::Type::e2D)
        .setFormat(format)
        .setExtent(glm::uvec2{8, 8})
        .setUsage(usage)
        .build();
}

// Build a descriptor set for each non-buffer/non-RT binding type. This drives the
// eSampler / eSampledImage / eStorageImage / eCombinedImageSampler / eUniformBuffer
// arms of the descriptor-set constructor switch, which the compute test (storage
// buffer only) leaves uncovered.
TEST_F(GpuTest, DescriptorTypesBuild) {
    auto sampledImg = makeImage(gfx::Flags(Image::Usage::eSampled) | Image::Usage::eTransferDst);
    auto storageImg = makeImage(gfx::Flags(Image::Usage::eStorage) | Image::Usage::eTransferDst);
    auto sampledView = ImageView::Builder(ResourceRef<const Image>(sampledImg)).build();
    auto storageView = ImageView::Builder(ResourceRef<const Image>(storageImg)).build();

    auto sampler = Sampler::Builder{}.build();

    Buffer::RawBuilder ub;
    ub.setRawSize(256).addUsage(Buffer::Usage::eUniform).setType(Buffer::Type::eDynamic);
    auto uniform = ub.build();

    // --- eSampler ---------------------------------------------------------
    {
        auto layout = DescriptorSetLayout::Builder{}.addBinding(0, DescriptorType::eSampler).build();
        auto set = DescriptorSet::Builder(*layout)
                       .write(0, Descriptor(ResourceRef<const Sampler>(sampler)))
                       .build();
        ASSERT_TRUE(static_cast<bool>(set));
    }
    // --- eSampledImage ----------------------------------------------------
    {
        auto layout = DescriptorSetLayout::Builder{}.addBinding(0, DescriptorType::eSampledImage).build();
        auto set = DescriptorSet::Builder(*layout)
                       .write(0, Descriptor(ResourceRef<const ImageView>(sampledView)))
                       .build();
        ASSERT_TRUE(static_cast<bool>(set));
    }
    // --- eStorageImage ----------------------------------------------------
    {
        auto layout = DescriptorSetLayout::Builder{}.addBinding(0, DescriptorType::eStorageImage).build();
        auto set = DescriptorSet::Builder(*layout)
                       .write(0, Descriptor(ResourceRef<const ImageView>(storageView)))
                       .build();
        ASSERT_TRUE(static_cast<bool>(set));
    }
    // --- eCombinedImageSampler -------------------------------------------
    {
        auto layout = DescriptorSetLayout::Builder{}.addBinding(0, DescriptorType::eCombinedImageSampler).build();
        auto set = DescriptorSet::Builder(*layout)
                       .write(0, Descriptor(ResourceRef<const ImageView>(sampledView), ResourceRef<const Sampler>(sampler)))
                       .build();
        ASSERT_TRUE(static_cast<bool>(set));
    }
    // --- eUniformBuffer + multi-binding set -------------------------------
    {
        auto layout = DescriptorSetLayout::Builder{}
                          .addBinding(0, DescriptorType::eUniformBuffer)
                          .addBinding(1, DescriptorType::eCombinedImageSampler)
                          .build();
        auto set = DescriptorSet::Builder(*layout)
                       .write(0, Descriptor(ResourceRef<const Buffer>(uniform)))
                       .write(1, Descriptor(ResourceRef<const ImageView>(sampledView), ResourceRef<const Sampler>(sampler)))
                       .build();
        ASSERT_TRUE(static_cast<bool>(set));
    }
}

// Exercise the runtime DescriptorSet::Write() path (as opposed to writes baked in
// at build time), which is a second, separate switch in the backend.
TEST_F(GpuTest, DescriptorRuntimeWrite) {
    auto sampler = Sampler::Builder{}.build();
    auto img = makeImage(gfx::Flags(Image::Usage::eStorage) | Image::Usage::eSampled | Image::Usage::eTransferDst);
    auto view = ImageView::Builder(ResourceRef<const Image>(img)).build();

    Buffer::RawBuilder sb;
    sb.setRawSize(256).addUsage(Buffer::Usage::eStorage).setType(Buffer::Type::eDynamic);
    auto storage = sb.build();
    Buffer::RawBuilder ub;
    ub.setRawSize(256).addUsage(Buffer::Usage::eUniform).setType(Buffer::Type::eDynamic);
    auto uniform = ub.build();

    auto layout = DescriptorSetLayout::Builder{}
                      .addBinding(0, DescriptorType::eStorageBuffer)
                      .addBinding(1, DescriptorType::eStorageImage)
                      .addBinding(2, DescriptorType::eSampler)
                      .addBinding(3, DescriptorType::eSampledImage)
                      .addBinding(4, DescriptorType::eUniformBuffer)
                      .addBinding(5, DescriptorType::eCombinedImageSampler)
                      .build();

    // Every layout binding must be written at build time (unwritten bindings hold
    // default-invalid descriptors that the constructor would choke on).
    auto set = DescriptorSet::Builder(*layout)
                   .write(0, Descriptor(ResourceRef<const Buffer>(storage)))
                   .write(1, Descriptor(ResourceRef<const ImageView>(view)))
                   .write(2, Descriptor(ResourceRef<const Sampler>(sampler)))
                   .write(3, Descriptor(ResourceRef<const ImageView>(view)))
                   .write(4, Descriptor(ResourceRef<const Buffer>(uniform)))
                   .write(5, Descriptor(ResourceRef<const ImageView>(view), ResourceRef<const Sampler>(sampler)))
                   .build();
    ASSERT_TRUE(static_cast<bool>(set));

    // Now re-issue each binding through the runtime DescriptorSet::Write() path,
    // which is a separate switch from the build-time writes above.
    set->Write(0, Descriptor(ResourceRef<const Buffer>(storage)), 0);
    set->Write(1, Descriptor(ResourceRef<const ImageView>(view)), 0);
    set->Write(2, Descriptor(ResourceRef<const Sampler>(sampler)), 0);
    set->Write(3, Descriptor(ResourceRef<const ImageView>(view)), 0);
    set->Write(4, Descriptor(ResourceRef<const Buffer>(uniform)), 0);
    set->Write(5, Descriptor(ResourceRef<const ImageView>(view), ResourceRef<const Sampler>(sampler)), 0);
    set->DebugPrint(); // exercise the debug dump path
    SUCCEED();
}

// 3D and array images plus a single-channel format, to walk the image-creation
// and view-creation paths that a plain 2D RGBA8 image never reaches.
TEST_F(GpuTest, ImageDimensionVariety) {
    auto image3d = Image::Builder{}
                       .setType(Image::Type::e3D)
                       .setFormat(Image::Format::eRGBA8_UNORM)
                       .setExtent(glm::uvec3{8, 8, 4})
                       .addUsage(Image::Usage::eTransferDst)
                       .addUsage(Image::Usage::eSampled)
                       .build();
    ASSERT_TRUE(static_cast<bool>(image3d));
    EXPECT_EQ(image3d->getExtent(), glm::uvec3(8, 8, 4));

    auto view3d = ImageView::Builder(ResourceRef<const Image>(image3d))
                      .setViewType(ImageView::Type::e3D)
                      .build();
    ASSERT_TRUE(static_cast<bool>(view3d));

    auto arrayImg = Image::Builder{}
                        .setType(Image::Type::e2D)
                        .setFormat(Image::Format::eR8_UNORM)
                        .setExtent(glm::uvec2{8, 8})
                        .setArrayLayers(3)
                        .addUsage(Image::Usage::eTransferDst)
                        .addUsage(Image::Usage::eSampled)
                        .build();
    ASSERT_TRUE(static_cast<bool>(arrayImg));

    auto arrayView = ImageView::Builder(ResourceRef<const Image>(arrayImg))
                         .setViewType(ImageView::Type::e2DArray)
                         .setArrayLayerCount(3)
                         .build();
    ASSERT_TRUE(static_cast<bool>(arrayView));
}

// GenerateMipmaps walks every mip level with vkCmdBlitImage and inserts the
// per-level barriers — a large uncovered stretch of the command buffer.
TEST_F(GpuTest, GenerateMipmapsRuns) {
    auto image = Image::Builder{}
                     .setType(Image::Type::e2D)
                     .setFormat(Image::Format::eRGBA8_UNORM)
                     .setExtent(glm::uvec2{8, 8})
                     .setMipLevels(4) // 8 -> 4 -> 2 -> 1
                     .addUsage(Image::Usage::eTransferSrc)
                     .addUsage(Image::Usage::eTransferDst)
                     .addUsage(Image::Usage::eSampled)
                     .build();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.ClearColorImage(ResourceRef<const Image>(image), glm::vec4{0.25f, 0.5f, 0.75f, 1.f});
        cb.GenerateMipmaps(ResourceRef<const Image>(image));
    }, CommandBuffer::Usage::eGraphics);
    SUCCEED();
}

// Image-to-image blit (down-scale) between two separate images.
TEST_F(GpuTest, BlitBetweenImages) {
    auto src = makeImage(gfx::Flags(Image::Usage::eTransferSrc) | Image::Usage::eTransferDst);
    auto dst = Image::Builder{}
                   .setType(Image::Type::e2D)
                   .setFormat(Image::Format::eRGBA8_UNORM)
                   .setExtent(glm::uvec2{4, 4})
                   .addUsage(Image::Usage::eTransferDst)
                   .addUsage(Image::Usage::eTransferSrc)
                   .build();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.ClearColorImage(ResourceRef<const Image>(src), glm::vec4{1.f, 1.f, 0.f, 1.f});
        cb.Blit(ResourceRef<const Image>(src), ResourceRef<const Image>(dst), gfx::Blit{
            .srcExtent = {8, 8, 1},
            .dstExtent = {4, 4, 1},
            .filtering = gfx::Filter::eLinear,
        });
    }, CommandBuffer::Usage::eGraphics);
    SUCCEED();
}

// Buffer-to-buffer copy, fill and clear on the transfer/compute queue.
TEST_F(GpuTest, BufferTransferOps) {
    std::vector<std::uint32_t> data(64, 7u);
    Buffer::Builder<std::uint32_t> srcB;
    srcB.setData(data);
    srcB.addUsage(Buffer::Usage::eTransferSrc);
    srcB.addUsage(Buffer::Usage::eTransferDst);
    srcB.setType(Buffer::Type::eDeviceLocal);
    auto src = srcB.build();

    Buffer::RawBuilder dstB;
    dstB.setRawSize(static_cast<glm::i64>(data.size() * sizeof(std::uint32_t)))
        .addUsage(Buffer::Usage::eTransferDst)
        .addUsage(Buffer::Usage::eTransferSrc)
        .setType(Buffer::Type::eReadback);
    auto dst = dstB.build();

    std::uint32_t fillValue = 0xABCDABCDu;
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyBuffer(ResourceRef<const Buffer>(src), ResourceRef<const Buffer>(dst));
        cb.FillBuffer(ResourceRef<const Buffer>(src), &fillValue, 0, sizeof(std::uint32_t) * 8);
        cb.ClearBuffer(ResourceRef<const Buffer>(src));
    }, CommandBuffer::Usage::eTransfer);

    const std::vector<std::uint32_t> out = dst->Read<std::uint32_t>();
    ASSERT_EQ(out.size(), data.size());
    for (auto v : out) EXPECT_EQ(v, 7u);
}

// Multisample resolve: a 4x MSAA color image resolved down into a single-sample
// image. Drives the two-argument Resolve path and the MSAA image-creation branch.
TEST_F(GpuTest, ResolveMultisampleToSingle) {
    auto msaa = Image::Builder{}
                    .setType(Image::Type::e2D)
                    .setFormat(Image::Format::eRGBA8_UNORM)
                    .setExtent(glm::uvec2{8, 8})
                    .setSampleCount(gfx::SampleCount::e4)
                    .addUsage(Image::Usage::eColorAttachment)
                    .addUsage(Image::Usage::eTransferSrc)
                    .build();
    ASSERT_TRUE(static_cast<bool>(msaa));

    auto single = makeImage(gfx::Flags(Image::Usage::eTransferDst) | Image::Usage::eTransferSrc);

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        // Give the MSAA image a defined layout/content via a render clear.
        auto view = ImageView::Builder(ResourceRef<const Image>(msaa)).build();
        auto fb = gfx::Framebuffer::Builder{}
                      .addColorAttachment(ResourceRef<const ImageView>(view), glm::vec4{0.2f, 0.4f, 0.6f, 1.f})
                      .build();
        cb.BeginRendering(ResourceRef<const gfx::Framebuffer>(fb));
        cb.EndRendering();
        cb.Resolve(ResourceRef<const Image>(msaa), ResourceRef<const Image>(single));
    }, CommandBuffer::Usage::eGraphics);
    SUCCEED();
}

// Negative tests: the CopyBufferToImage / CopyImageToBuffer validation branches
// (mip level, array layer, row length, layer count). Each is recorded on a
// manually-created command buffer and inspected without submitting.
TEST_F(GpuTest, CopyValidationBranches) {
    auto image = Image::Builder{}
                     .setType(Image::Type::e2D)
                     .setFormat(Image::Format::eRGBA8_UNORM)
                     .setExtent(glm::uvec2{8, 8})
                     .addUsage(Image::Usage::eTransferSrc)
                     .addUsage(Image::Usage::eTransferDst)
                     .build();

    Buffer::RawBuilder rb;
    rb.setRawSize(static_cast<glm::i64>(8) * 8 * 4)
      .addUsage(Buffer::Usage::eTransferSrc)
      .addUsage(Buffer::Usage::eTransferDst)
      .setType(Buffer::Type::eStaging);
    auto buf = rb.build();

    const auto expectReject = [&](auto&& record) {
        auto cb = CommandBuffer::Create(CommandBuffer::Usage::eTransfer);
        cb->Begin();
        record(*cb);
        cb->End();
        EXPECT_FALSE(cb->ok());
        EXPECT_FALSE(cb->errors().empty());
    };

    // mip level out of range
    expectReject([&](CommandBuffer& cb) {
        cb.CopyBufferToImage(ResourceRef<const Buffer>(buf), ResourceRef<const Image>(image), gfx::Copy{ .imageMipLevel = 5 });
    });
    // base array layer out of range
    expectReject([&](CommandBuffer& cb) {
        cb.CopyBufferToImage(ResourceRef<const Buffer>(buf), ResourceRef<const Image>(image), gfx::Copy{ .imageBaseArrayLayer = 4 });
    });
    // buffer row length smaller than the image extent
    expectReject([&](CommandBuffer& cb) {
        cb.CopyBufferToImage(ResourceRef<const Buffer>(buf), ResourceRef<const Image>(image), gfx::Copy{ .bufferRowLength = 2, .imageExtent = {8, 8, 1} });
    });
    // buffer offset past the end of the buffer
    expectReject([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(ResourceRef<const Image>(image), ResourceRef<const Buffer>(buf), gfx::Copy{ .bufferOffset = 999999 });
    });
}

// Debug-label commands (scoped + single markers). No-ops without a debugger, but
// they still record and must not fail the command buffer.
TEST_F(GpuTest, DebugLabelsRecord) {
    auto image = makeImage(gfx::Flags(Image::Usage::eTransferDst) | Image::Usage::eTransferSrc);
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginDebugLabel("outer", glm::vec4{1.f, 0.f, 0.f, 1.f});
        cb.InsertDebugLabel("marker");
        cb.DebugLabel("scoped", [&](CommandBuffer& inner) {
            inner.ClearColorImage(ResourceRef<const Image>(image), glm::vec4{0.f, 1.f, 0.f, 1.f});
        });
        cb.EndDebugLabel();
    }, CommandBuffer::Usage::eGraphics);
    SUCCEED();
}

} // namespace
