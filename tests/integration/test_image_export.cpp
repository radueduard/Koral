// Integration coverage for the image *export* module (modules/image-export): writing one subimage of
// an image to an ordinary container, and writing a whole one — every mip of every layer, compressed or
// not — as a KTX2.
//
// The round trips are the point. A file that exists proves nothing; a file that reads back as what
// went into it proves the mip, the layer, the region and the format all survived. The import module is
// linked here to do the reading.

#include "gpu_fixture.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <span>
#include <thread>
#include <string>
#include <vector>

#include "buffer.h"
#include "commandBuffer.h"
#include "context.h"
#include "error.h"
#include "image.h"

#include <koralImageExport.h>
#include <koralImageImport.h>

using kor::Buffer;
using kor::CommandBuffer;
using kor::Image;
using kor::ResourceRef;

namespace {

std::filesystem::path outDir() {
    return std::filesystem::temp_directory_path() / "koral_image_export";
}

/** @brief An image of one colour per mip level, so a saved mip can be told from its neighbours. */
kor::Resource<Image> mippedImage(const std::uint32_t size, const std::uint32_t mips,
                                 const std::uint32_t layers = 1) {
    auto image = Image::Builder{}
        .setType(Image::Type::e2D)
        .setFormat(Image::Format::eRGBA8_UNORM)
        .setExtent(glm::uvec2{ size, size })
        .setMipLevels(mips)
        .setArrayLayers(layers)
        .setUsage(Image::Usage::eTransferDst | Image::Usage::eTransferSrc | Image::Usage::eSampled)
        .build();

    // Fill every level of every layer with a value derived from both, by uploading it: clears only
    // reach mip 0, and what is being tested is that a *chosen* subimage comes back.
    for (std::uint32_t mip = 0; mip < mips; ++mip) {
        const std::uint32_t extent = std::max(1u, size >> mip);
        for (std::uint32_t layer = 0; layer < layers; ++layer) {
            const auto texelCount = static_cast<std::size_t>(extent) * extent;
            std::vector<glm::u8vec4> texels(texelCount, glm::u8vec4{
                static_cast<std::uint8_t>(10 + mip * 40), static_cast<std::uint8_t>(20 + layer * 30), 200, 255 });

            const auto staging = Buffer::Builder<glm::u8vec4>()
                .setDataView(std::span<const glm::u8vec4>(texels))
                .setUsage(Buffer::Usage::eTransferSrc)
                .setType(Buffer::Type::eStaging)
                .build();

            CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
                cb.CopyBufferToImage(staging, image, kor::Copy{
                    .imageOffset = { 0, 0, 0 },
                    .imageExtent = { extent, extent, 1 },
                    .imageBaseArrayLayer = layer,
                    .imageLayerCount = 1,
                    .imageMipLevel = mip,
                });
            });
        }
    }
    return image;
}

std::vector<glm::u8vec4> readTexels(const ResourceRef<const Image>& image) {
    const auto extent = image->extent();
    const auto texels = static_cast<std::size_t>(extent.x) * extent.y;

    Buffer::RawBuilder rb;
    rb.setRawSize(static_cast<glm::i64>(texels * sizeof(glm::u8vec4)))
      .setUsage(Buffer::Usage::eTransferDst)
      .setType(Buffer::Type::eReadback);
    auto readback = rb.build();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(image, readback);
    }, CommandBuffer::Usage::eTransfer);
    return readback->Read<glm::u8vec4>();
}

// ---- one subimage ------------------------------------------------------------------------------

// The default subimage is the whole of mip 0 — a screenshot — and the three arguments that name the
// file all matter: <directory>/<name><extension for the container>.
TEST_F(GpuTest, ExportWritesMipZeroByDefault) {
    auto image = mippedImage(16, 5);

    const auto saved = kimg::SaveImage(outDir(), "top", kimg::FileFormat::ePNG, image);
    ASSERT_TRUE(saved.has_value()) << saved.error().message;
    EXPECT_EQ(*saved, outDir() / "top.png");
    ASSERT_TRUE(std::filesystem::exists(*saved));

    auto reloaded = kimg::LoadImage(*saved);
    ASSERT_TRUE(static_cast<bool>(reloaded)) << (reloaded.error() ? reloaded.error()->message : "");
    EXPECT_EQ(reloaded->extent(), glm::uvec3(16, 16, 1));

    const auto texels = readTexels(reloaded);
    ASSERT_FALSE(texels.empty());
    EXPECT_EQ(texels.front().r, 10);   // mip 0, layer 0
    EXPECT_EQ(texels.front().g, 20);
}

// Any mip level, on its own, at the size that level actually is.
TEST_F(GpuTest, ExportWritesAChosenMipLevel) {
    auto image = mippedImage(16, 5);

    const auto saved = kimg::SaveImage(outDir(), "mip2", kimg::FileFormat::ePNG,
                                       image, { .mipLevel = 2 });
    ASSERT_TRUE(saved.has_value()) << saved.error().message;

    auto reloaded = kimg::LoadImage(*saved);
    ASSERT_TRUE(static_cast<bool>(reloaded));
    EXPECT_EQ(reloaded->extent(), glm::uvec3(4, 4, 1));   // 16 >> 2

    const auto texels = readTexels(reloaded);
    ASSERT_FALSE(texels.empty());
    EXPECT_EQ(texels.front().r, 10 + 2 * 40);   // the value written into mip 2
}

// Any array layer — which for a cubemap is any face.
TEST_F(GpuTest, ExportWritesAChosenArrayLayer) {
    auto image = mippedImage(8, 1, 6);

    const auto saved = kimg::SaveImage(outDir(), "layer4", kimg::FileFormat::ePNG,
                                       image, { .arrayLayer = 4 });
    ASSERT_TRUE(saved.has_value()) << saved.error().message;

    auto reloaded = kimg::LoadImage(*saved);
    ASSERT_TRUE(static_cast<bool>(reloaded));
    const auto texels = readTexels(reloaded);
    ASSERT_FALSE(texels.empty());
    EXPECT_EQ(texels.front().g, 20 + 4 * 30);   // the value written into layer 4
}

// Any rectangle of any level.
TEST_F(GpuTest, ExportWritesAChosenRegion) {
    auto image = mippedImage(16, 1);

    const auto saved = kimg::SaveImage(outDir(), "corner", kimg::FileFormat::ePNG,
                                       image,
                                       { .offset = { 4, 4, 0 }, .extent = { 8, 4, 1 } });
    ASSERT_TRUE(saved.has_value()) << saved.error().message;

    auto reloaded = kimg::LoadImage(*saved);
    ASSERT_TRUE(static_cast<bool>(reloaded));
    EXPECT_EQ(reloaded->extent(), glm::uvec3(8, 4, 1));
}

// A subimage that does not exist is refused by name, before anything is written.
TEST_F(GpuTest, ExportRefusesASubimageThatIsNotThere) {
    auto image = mippedImage(16, 2);

    const auto tooDeep = kimg::SaveImage(outDir(), "nope", kimg::FileFormat::ePNG,
                                         image, { .mipLevel = 7 });
    ASSERT_FALSE(tooDeep.has_value());
    EXPECT_NE(tooDeep.error().message.find("mip level 7"), std::string::npos);

    const auto tooWide = kimg::SaveImage(outDir(), "nope", kimg::FileFormat::ePNG,
                                         image,
                                         { .offset = { 8, 8, 0 }, .extent = { 16, 16, 1 } });
    ASSERT_FALSE(tooWide.has_value());
    EXPECT_NE(tooWide.error().message.find("does not fit"), std::string::npos);
}

// A float image needs a container that can hold floats; the writer must not quietly truncate it.
TEST_F(GpuTest, ExportKeepsFloatPrecisionInAFloatContainer) {
    auto image = Image::Builder{}
        .setType(Image::Type::e2D)
        .setFormat(Image::Format::eRGBA32_SFLOAT)
        .setExtent(glm::uvec2{ 4, 4 })
        .setUsage(Image::Usage::eTransferDst | Image::Usage::eTransferSrc)
        .build();
    // A value no 8-bit container could hold.
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.ClearColorImage(image, glm::vec4{ 12.5f, 0.25f, 3.f, 1.f });
    }, CommandBuffer::Usage::eGraphics);

    const auto saved = kimg::SaveImage(outDir(), "bright", kimg::FileFormat::eEXR, image);
    ASSERT_TRUE(saved.has_value()) << saved.error().message;

    auto reloaded = kimg::LoadImage(*saved);
    ASSERT_TRUE(static_cast<bool>(reloaded)) << (reloaded.error() ? reloaded.error()->message : "");

    Buffer::RawBuilder rb;
    rb.setRawSize(4 * 4 * static_cast<glm::i64>(sizeof(glm::vec4)))
      .setUsage(Buffer::Usage::eTransferDst)
      .setType(Buffer::Type::eReadback);
    auto readback = rb.build();
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(reloaded, readback);
    }, CommandBuffer::Usage::eTransfer);

    const auto texels = readback->Read<glm::vec4>();
    ASSERT_FALSE(texels.empty());
    EXPECT_NEAR(texels.front().r, 12.5f, 0.01f);
    EXPECT_NEAR(texels.front().g, 0.25f, 0.01f);
}

// ---- the whole image, as KTX2 -------------------------------------------------------------------

// Every mip of every layer, out and back in again. This is the round trip that says the engine can
// hand a texture to disk and get the same texture back.
TEST_F(GpuTest, ExportWritesAWholeMippedCubeAsKtx2) {
    constexpr std::uint32_t kSize = 16;
    constexpr std::uint32_t kMips = 5;
    auto image = mippedImage(kSize, kMips, 6);

    const auto saved = kimg::SaveImageSet(outDir(), "cube", image);
    ASSERT_TRUE(saved.has_value()) << saved.error().message;
    EXPECT_EQ(saved->extension(), ".ktx2");

    auto reloaded = kimg::LoadImage(*saved);
    ASSERT_TRUE(static_cast<bool>(reloaded)) << (reloaded.error() ? reloaded.error()->message : "");
    EXPECT_EQ(reloaded->extent(), glm::uvec3(kSize, kSize, 1));
    EXPECT_EQ(reloaded->mipLevels(), kMips);
    EXPECT_EQ(reloaded->arrayLayers(), 6u);
    EXPECT_EQ(reloaded->format(), Image::Format::eRGBA8_UNORM);
}

// The async twin, run to completion the way the headless engine runs a job.
TEST_F(GpuTest, ExportWritesAWholeImageAsKtx2Async) {
    auto image = mippedImage(8, 4);

    auto task = kimg::SaveImageSetAsync(outDir(), "async", image);
    while (!task.done()) {
        kor::Context::DrainMainThread();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    auto result = task.take();
    ASSERT_TRUE(result.has_value()) << result.error();
    ASSERT_TRUE(result->has_value()) << (*result).error().message;
    EXPECT_TRUE(std::filesystem::exists(**result));

    auto reloaded = kimg::LoadImage(**result);
    ASSERT_TRUE(static_cast<bool>(reloaded));
    EXPECT_EQ(reloaded->mipLevels(), 4u);
}

// A block-compressed image can only go into KTX2 — and it goes in as the blocks it already is, which
// is what makes this the round trip that matters for a compressed texture.
TEST_F(GpuTest, ExportWritesCompressedBlocksIntoKtx2) {
    constexpr std::uint32_t kSize = 16;
    const auto format = Image::Format::eBC7_UNORM;
    const auto byteCount = Image::sizeOfRegion(format, { kSize, kSize, 1 });

    std::vector<std::uint8_t> blocks(byteCount);
    for (std::size_t i = 0; i < blocks.size(); ++i)
        blocks[i] = static_cast<std::uint8_t>((i / Image::blockSize(format)) + 1);

    auto image = Image::Builder{}
        .setType(Image::Type::e2D)
        .setFormat(format)
        .setExtent(glm::uvec2{ kSize, kSize })
        .setUsage(Image::Usage::eTransferDst | Image::Usage::eTransferSrc)
        .build();
    ASSERT_TRUE(static_cast<bool>(image));

    const auto staging = Buffer::Builder<std::uint8_t>()
        .setDataView(std::span<const std::uint8_t>(blocks))
        .setUsage(Buffer::Usage::eTransferSrc)
        .setType(Buffer::Type::eStaging)
        .build();
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyBufferToImage(staging, image, kor::Copy{
            .imageOffset = { 0, 0, 0 }, .imageExtent = { kSize, kSize, 1 } });
    }, CommandBuffer::Usage::eTransfer);

    const auto saved = kimg::SaveImageSet(outDir(), "bc7", image);
    ASSERT_TRUE(saved.has_value()) << saved.error().message;

    auto reloaded = kimg::LoadImage(*saved);
    ASSERT_TRUE(static_cast<bool>(reloaded)) << (reloaded.error() ? reloaded.error()->message : "");
    EXPECT_EQ(reloaded->format(), format);
    EXPECT_EQ(reloaded->extent(), glm::uvec3(kSize, kSize, 1));

    // The blocks themselves, byte for byte: a compressed texture that survives a round trip is one
    // nothing decoded and re-encoded behind your back.
    Buffer::RawBuilder rb;
    rb.setRawSize(static_cast<glm::i64>(byteCount))
      .setUsage(Buffer::Usage::eTransferDst)
      .setType(Buffer::Type::eReadback);
    auto readback = rb.build();
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(reloaded, readback, kor::Copy{
            .imageOffset = { 0, 0, 0 }, .imageExtent = { kSize, kSize, 1 } });
    }, CommandBuffer::Usage::eTransfer);
    EXPECT_EQ(readback->Read<std::uint8_t>(), blocks);
}

// ...and it is refused, by name, from a container that cannot hold blocks.
TEST_F(GpuTest, ExportRefusesCompressedIntoAnOrdinaryContainer) {
    auto image = Image::Builder{}
        .setType(Image::Type::e2D)
        .setFormat(Image::Format::eBC7_UNORM)
        .setExtent(glm::uvec2{ 8, 8 })
        .setUsage(Image::Usage::eTransferDst | Image::Usage::eTransferSrc)
        .build();

    const auto saved = kimg::SaveImage(outDir(), "bc7", kimg::FileFormat::ePNG, image);
    ASSERT_FALSE(saved.has_value());
    EXPECT_NE(saved.error().message.find("only be written as KTX2"), std::string::npos);
}

} // namespace
