// Integration tests for kor::Image against a real Vulkan device. These exercise
// the image upload path (staging buffer -> vkCmdCopyBufferToImage during build),
// image layout transitions (the automatic barrier inside CopyImageToBuffer),
// vkCmdClearColorImage, and image->buffer readback.
//
// There is no public Image::Read(); to inspect an image's contents we copy it
// into a host-visible readback buffer with CommandBuffer::CopyImageToBuffer and
// read that buffer back — the same round-trip a real app would use for a
// screenshot or GPU-computed texture.

#include "gpu_fixture.h"

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "buffer.h"
#include "commandBuffer.h"
#include "context.h"
#include "error.h"
#include "image.h"

using kor::Buffer;
using kor::CommandBuffer;
using kor::Image;
using kor::ResourceRef;

namespace {

using Pixel = glm::u8vec4; // RGBA8

constexpr std::uint32_t kW = 8;
constexpr std::uint32_t kH = 8;

// Copies a whole RGBA8 image into a fresh readback buffer and returns its pixels.
std::vector<Pixel> readbackPixels(const kor::Resource<Image>& image, std::uint32_t w, std::uint32_t h) {
    Buffer::RawBuilder rb;
    rb.setRawSize(static_cast<glm::i64>(w) * h * sizeof(Pixel))
      .setUsage(Buffer::Usage::eTransferDst)
      .setType(Buffer::Type::eReadback);
    auto readback = rb.build();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        // CopyImageToBuffer inserts its own image barrier to TransferSrc, so we
        // don't need to transition the image explicitly first.
        cb.CopyImageToBuffer(image, readback);
    }, CommandBuffer::Usage::eTransfer);

    return readback->Read<Pixel>();
}

// Upload a known pattern to mip 0, copy it back, and check every texel survived
// the CPU -> staging -> device-local image -> staging -> CPU round-trip.
TEST_F(GpuTest, UploadReadbackRGBA8) {
    std::vector<Pixel> src(kW * kH);
    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            src[y * kW + x] = Pixel(
                static_cast<std::uint8_t>(x * 32),
                static_cast<std::uint8_t>(y * 32),
                static_cast<std::uint8_t>((x + y) * 16),
                255);
        }
    }

    Image::Builder ib;
    ib.setType(Image::Type::e2D)
      .setFormat(Image::Format::eRGBA8_UNORM)
      .setExtent(glm::uvec2{kW, kH})
      .setUsage(Image::Usage::eTransferSrc | Image::Usage::eTransferDst)   // needed for the upload
      .setData(std::span<const Pixel>(src));
    auto image = ib.build();

    ASSERT_TRUE(static_cast<bool>(image));
    EXPECT_EQ(image->extent(), glm::uvec3(kW, kH, 1));
    EXPECT_EQ(image->format(), Image::Format::eRGBA8_UNORM);

    const std::vector<Pixel> out = readbackPixels(image, kW, kH);
    ASSERT_EQ(out.size(), src.size());
    for (std::size_t i = 0; i < src.size(); ++i) {
        EXPECT_EQ(out[i].r, src[i].r) << "pixel " << i << " channel r";
        EXPECT_EQ(out[i].g, src[i].g) << "pixel " << i << " channel g";
        EXPECT_EQ(out[i].b, src[i].b) << "pixel " << i << " channel b";
        EXPECT_EQ(out[i].a, src[i].a) << "pixel " << i << " channel a";
    }
}

// ClearColorImage should fill every texel with the given color. UNORM maps
// [0,1] float -> [0,255], so {1,0,0,1} becomes (255,0,0,255).
TEST_F(GpuTest, ClearColorImageThenReadback) {
    Image::Builder ib;
    ib.setType(Image::Type::e2D)
      .setFormat(Image::Format::eRGBA8_UNORM)
      .setExtent(glm::uvec2{kW, kH})
      .setUsage(Image::Usage::eTransferSrc | Image::Usage::eTransferDst);
    auto image = ib.build();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.ClearColorImage(image, glm::vec4{1.f, 0.f, 0.f, 1.f});
    }, CommandBuffer::Usage::eGraphics);

    // Separate submit: SingleTimeCommand waits idle between the two, and the copy's
    // internal transition (transfer-dst -> transfer-src) provides the memory
    // dependency from the clear's write to the readback read.
    const std::vector<Pixel> out = readbackPixels(image, kW, kH);
    ASSERT_EQ(out.size(), static_cast<std::size_t>(kW) * kH);
    for (std::size_t i = 0; i < out.size(); ++i) {
        EXPECT_EQ(out[i].r, 255) << "pixel " << i;
        EXPECT_EQ(out[i].g, 0)   << "pixel " << i;
        EXPECT_EQ(out[i].b, 0)   << "pixel " << i;
        EXPECT_EQ(out[i].a, 255) << "pixel " << i;
    }
}

// A partial copy: read back only a sub-rectangle of the image using explicit
// imageOffset + imageExtent (rather than the whole-image defaults). This is the
// path where a default imageExtent of {-1,-1,-1} used to leak straight through
// to vkCmdCopyImageToBuffer; here we prove the explicit-region variant lands the
// right texels in a tightly-packed readback buffer.
TEST_F(GpuTest, SubRegionReadback) {
    constexpr std::uint32_t kOffX = 2, kOffY = 2;
    constexpr std::uint32_t kSubW = 4, kSubH = 4;

    std::vector<Pixel> src(kW * kH);
    for (std::uint32_t y = 0; y < kH; ++y)
        for (std::uint32_t x = 0; x < kW; ++x)
            src[y * kW + x] = Pixel(
                static_cast<std::uint8_t>(x * 32),
                static_cast<std::uint8_t>(y * 32),
                static_cast<std::uint8_t>((x + y) * 16),
                255);

    Image::Builder ib;
    ib.setType(Image::Type::e2D)
      .setFormat(Image::Format::eRGBA8_UNORM)
      .setExtent(glm::uvec2{kW, kH})
      .setUsage(Image::Usage::eTransferSrc | Image::Usage::eTransferDst)
      .setData(std::span<const Pixel>(src));
    auto image = ib.build();

    Buffer::RawBuilder rb;
    rb.setRawSize(static_cast<glm::i64>(kSubW) * kSubH * sizeof(Pixel))
      .setUsage(Buffer::Usage::eTransferDst)
      .setType(Buffer::Type::eReadback);
    auto readback = rb.build();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(image, readback, kor::Copy{
            .imageOffset = { static_cast<int>(kOffX), static_cast<int>(kOffY), 0 },
            .imageExtent = { static_cast<int>(kSubW), static_cast<int>(kSubH), 1 },
        });
    }, CommandBuffer::Usage::eTransfer);

    const std::vector<Pixel> out = readback->Read<Pixel>();
    ASSERT_EQ(out.size(), static_cast<std::size_t>(kSubW) * kSubH);
    for (std::uint32_t j = 0; j < kSubH; ++j) {
        for (std::uint32_t i = 0; i < kSubW; ++i) {
            const Pixel& got = out[j * kSubW + i];
            const Pixel& want = src[(kOffY + j) * kW + (kOffX + i)];
            EXPECT_EQ(got.r, want.r) << "sub texel (" << i << "," << j << ") r";
            EXPECT_EQ(got.g, want.g) << "sub texel (" << i << "," << j << ") g";
            EXPECT_EQ(got.b, want.b) << "sub texel (" << i << "," << j << ") b";
            EXPECT_EQ(got.a, want.a) << "sub texel (" << i << "," << j << ") a";
        }
    }
}

// The write-side counterpart to SubRegionReadback: clear the whole image to a
// base color, then CopyBufferToImage a small patch into an explicit sub-region.
// A full readback must show the patch exactly where we put it and the base color
// everywhere else — proving CopyBufferToImage honours imageOffset/imageExtent.
TEST_F(GpuTest, PartialUploadIntoSubRegion) {
    constexpr std::uint32_t kOffX = 2, kOffY = 2;
    constexpr std::uint32_t kSubW = 4, kSubH = 4;
    const Pixel kBase{0, 0, 0, 255};
    const Pixel kPatch{200, 100, 50, 255};

    Image::Builder ib;
    ib.setType(Image::Type::e2D)
      .setFormat(Image::Format::eRGBA8_UNORM)
      .setExtent(glm::uvec2{kW, kH})
      .setUsage(Image::Usage::eTransferSrc | Image::Usage::eTransferDst);
    auto image = ib.build();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.ClearColorImage(image, glm::vec4{0.f, 0.f, 0.f, 1.f});
    }, CommandBuffer::Usage::eGraphics);

    const std::vector<Pixel> patch(kSubW * kSubH, kPatch);
    Buffer::Builder<Pixel> sb;
    sb.setData(patch);
    sb.setUsage(Buffer::Usage::eTransferSrc);
    sb.setType(Buffer::Type::eStaging);
    auto staging = sb.build();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyBufferToImage(staging, image, kor::Copy{
            .imageOffset = { static_cast<int>(kOffX), static_cast<int>(kOffY), 0 },
            .imageExtent = { static_cast<int>(kSubW), static_cast<int>(kSubH), 1 },
        });
    });

    const std::vector<Pixel> out = readbackPixels(image, kW, kH);
    ASSERT_EQ(out.size(), static_cast<std::size_t>(kW) * kH);
    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            const bool inPatch = x >= kOffX && x < kOffX + kSubW && y >= kOffY && y < kOffY + kSubH;
            const Pixel& want = inPatch ? kPatch : kBase;
            const Pixel& got = out[y * kW + x];
            EXPECT_EQ(got.r, want.r) << "texel (" << x << "," << y << ") r";
            EXPECT_EQ(got.g, want.g) << "texel (" << x << "," << y << ") g";
            EXPECT_EQ(got.b, want.b) << "texel (" << x << "," << y << ") b";
            EXPECT_EQ(got.a, want.a) << "texel (" << x << "," << y << ") a";
        }
    }
}

// Regression guard for the whole-system GPU hang: a copy whose requested extent
// overruns the destination buffer must be rejected on the CPU (as a kor::Error)
// and must NEVER be handed to vkCmdCopyImageToBuffer. We record the copy on a
// command buffer and inspect ok()/errors() without ever submitting it, so even a
// still-broken build fails as a normal assertion instead of freezing the machine.
TEST_F(GpuTest, OversizeCopyRejectedNotSubmitted) {
    Image::Builder ib;
    ib.setType(Image::Type::e2D)
      .setFormat(Image::Format::eRGBA8_UNORM)
      .setExtent(glm::uvec2{kW, kH})
      .setUsage(Image::Usage::eTransferSrc | Image::Usage::eTransferDst);
    auto image = ib.build();

    // Deliberately tiny readback buffer — far too small for the requested region.
    Buffer::RawBuilder rb;
    rb.setRawSize(static_cast<glm::i64>(sizeof(Pixel)))
      .setUsage(Buffer::Usage::eTransferDst)
      .setType(Buffer::Type::eReadback);
    auto readback = rb.build();

    auto cb = CommandBuffer::Create(CommandBuffer::Usage::eTransfer);
    cb->Begin();
    cb->CopyImageToBuffer(image, readback, kor::Copy{
        .imageExtent = { 100000, 100000, 1 }, // gigantic on purpose
    });
    cb->End();

    EXPECT_FALSE(cb->ok()) << "an oversize copy must be recorded as a failure";
    ASSERT_FALSE(cb->errors().empty());
    EXPECT_EQ(cb->errors().front().code, kor::ErrorCode::eCopySizeExceedsBuffer);
    // Intentionally never Submit()/WaitForFence(): the bad copy was rejected at
    // record time, so there is nothing safe or useful to send to the GPU.
}

// The copy-size guard must count *bytes*, not texels. This copies an 8x8
// RGBA32_SFLOAT image (16 bytes/texel = 1024 bytes) into a 256-byte buffer.
// A texel-count check (64 texels <= 256) would have waved this through and let
// the GPU stomp 768 bytes past the buffer; the byte-accurate check rejects it.
TEST_F(GpuTest, ByteSizedGuardRejectsLargeFormatOverflow) {
    Image::Builder ib;
    ib.setType(Image::Type::e2D)
      .setFormat(Image::Format::eRGBA32_SFLOAT)
      .setExtent(glm::uvec2{kW, kH})
      .setUsage(Image::Usage::eTransferSrc | Image::Usage::eTransferDst);
    auto image = ib.build();

    Buffer::RawBuilder rb;
    rb.setRawSize(256) // holds 64 texels' worth of *bytes* only if 4 bytes/texel — but this format is 16
      .setUsage(Buffer::Usage::eTransferDst)
      .setType(Buffer::Type::eReadback);
    auto readback = rb.build();

    auto cb = CommandBuffer::Create(CommandBuffer::Usage::eTransfer);
    cb->Begin();
    cb->CopyImageToBuffer(image, readback); // default = whole 8x8
    cb->End();

    EXPECT_FALSE(cb->ok()) << "a byte-overflowing copy must be recorded as a failure";
    ASSERT_FALSE(cb->errors().empty());
    EXPECT_EQ(cb->errors().front().code, kor::ErrorCode::eCopySizeExceedsBuffer);
}

// Blit::filtering has to reach the GPU. The Vulkan backend used to hardcode eNearest in both
// doBlit overloads and ignore the field entirely, so a downscale that asked to be averaged was
// point-sampled instead — silently, and differently from OpenGL, which had always honoured it.
// GenerateMipmaps asks for eLinear, so every Vulkan mip chain was built the wrong way.
//
// Shrinking a 2x2 checker of black and white to 1x1 separates the two unambiguously: linear
// weights all four texels equally and lands mid-grey, nearest can only ever return one of the
// source texels, so it lands on an extreme. No assumption about *which* texel nearest picks.
TEST_F(GpuTest, BlitFilteringIsHonoured) {
    const std::vector<Pixel> checker = {
        Pixel(0, 0, 0, 255),       Pixel(255, 255, 255, 255),
        Pixel(255, 255, 255, 255), Pixel(0, 0, 0, 255),
    };

    const auto makeSource = [&] {
        Image::Builder ib;
        ib.setType(Image::Type::e2D)
          .setFormat(Image::Format::eRGBA8_UNORM)
          .setExtent(glm::uvec2{2, 2})
          .setUsage(Image::Usage::eTransferSrc | Image::Usage::eTransferDst)
          .setData(std::span<const Pixel>(checker));
        return ib.build();
    };

    const auto blitTo1x1 = [&](const kor::Filter filter) {
        auto source = makeSource();
        Image::Builder ib;
        ib.setType(Image::Type::e2D)
          .setFormat(Image::Format::eRGBA8_UNORM)
          .setExtent(glm::uvec2{1, 1})
          .setUsage(Image::Usage::eTransferSrc | Image::Usage::eTransferDst);
        auto destination = ib.build();

        CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
            cb.Blit(source, destination,
                    kor::Blit{ .filtering = filter });
        }, CommandBuffer::Usage::eGraphics);

        const std::vector<Pixel> out = readbackPixels(destination, 1, 1);
        EXPECT_EQ(out.size(), 1u);
        return out.front().r;
    };

    const int linear = blitTo1x1(kor::Filter::eLinear);
    const int nearest = blitTo1x1(kor::Filter::eNearest);

    // 127.5 in exact arithmetic; the rounding of the last bit is the driver's business.
    EXPECT_NEAR(linear, 128, 8) << "eLinear did not average the four source texels";
    EXPECT_TRUE(nearest == 0 || nearest == 255)
        << "eNearest returned " << nearest << ", which is not one of the source texels";
}

// A partial-range barrier: a base mip level with no count, meaning "levels 2..3". The count used
// to be resolved against the image's *total* mip count rather than the levels left above the
// base, so this emitted barriers for levels 4 and 5 of a 4-level image — two spec violations
// (VUID-VkImageMemoryBarrier-subresourceRange-01486 and -01724) per submission.
//
// Like the coverage tests, this asserts nothing beyond running clean: the validation layer
// reports through the log rather than failing the process, so read the output when it changes.
TEST_F(GpuTest, PartialRangeImageBarrierStaysInBounds) {
    Image::Builder ib;
    ib.setType(Image::Type::e2D)
      .setFormat(Image::Format::eRGBA8_UNORM)
      .setExtent(glm::uvec2{8, 8})
      .setMipLevels(4) // 8 -> 4 -> 2 -> 1
      .setUsage(Image::Usage::eTransferSrc | Image::Usage::eTransferDst);
    auto image = ib.build();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.ImageBarrier(kor::ImageBarrier(image,
                                          kor::ResourceAccess::eTransferDst, 2u));
    }, CommandBuffer::Usage::eGraphics);
    SUCCEED();
}

} // namespace
