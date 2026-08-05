// Unit tests for the arithmetic every image path depends on: how big a texel is, how big a *block*
// is, and how many bytes a region of either occupies. No GPU needed — this is the maths that decides
// staging-buffer sizes and guards copies, and getting it wrong for a compressed format is how you
// upload three quarters of a texture and see nothing.

#include <gtest/gtest.h>

#include <image.h>

using kor::Image;
using Format = kor::Image::Format;

namespace {

// -----------------------------------------------------------------------------
// Which formats are blocks, and which are texels.
// -----------------------------------------------------------------------------
TEST(ImageFormat, UncompressedFormatsAreNotBlockCompressed) {
    EXPECT_FALSE(Image::IsBlockCompressed(Format::eRGBA8_UNORM));
    EXPECT_FALSE(Image::IsBlockCompressed(Format::eRGBA32_SFLOAT));
    EXPECT_FALSE(Image::IsBlockCompressed(Format::eD32_SFLOAT));
}

TEST(ImageFormat, EveryCompressedFamilyIsRecognised) {
    for (const auto format : { Format::eBC1_RGB_UNORM, Format::eBC1_RGBA_SRGB, Format::eBC2_UNORM,
                               Format::eBC3_SRGB, Format::eBC4_UNORM, Format::eBC5_SNORM,
                               Format::eBC6H_UFLOAT, Format::eBC7_SRGB,
                               Format::eASTC_4x4_UNORM, Format::eASTC_6x6_SRGB, Format::eASTC_8x8_UNORM,
                               Format::eETC2_RGB8_UNORM, Format::eETC2_RGBA8_SRGB,
                               Format::eEAC_R11_UNORM, Format::eEAC_RG11_SNORM }) {
        EXPECT_TRUE(Image::IsBlockCompressed(format)) << "format " << static_cast<int>(format);
    }
}

// -----------------------------------------------------------------------------
// Block extent: 1x1 for a texel format, so the same arithmetic serves both.
// -----------------------------------------------------------------------------
TEST(ImageFormat, UncompressedBlockIsOneTexel) {
    EXPECT_EQ(Image::BlockExtentFromImageFormat(Format::eRGBA8_UNORM), glm::uvec2(1, 1));
    EXPECT_EQ(Image::BlockSizeFromImageFormat(Format::eRGBA8_UNORM), 4u);
    EXPECT_EQ(Image::BlockSizeFromImageFormat(Format::eRGBA32_SFLOAT), 16u);
    EXPECT_EQ(Image::BlockSizeFromImageFormat(Format::eR8_UNORM), 1u);
}

TEST(ImageFormat, BlockExtentsFollowTheFamily) {
    EXPECT_EQ(Image::BlockExtentFromImageFormat(Format::eBC7_UNORM), glm::uvec2(4, 4));
    EXPECT_EQ(Image::BlockExtentFromImageFormat(Format::eBC1_RGB_UNORM), glm::uvec2(4, 4));
    EXPECT_EQ(Image::BlockExtentFromImageFormat(Format::eETC2_RGBA8_UNORM), glm::uvec2(4, 4));
    EXPECT_EQ(Image::BlockExtentFromImageFormat(Format::eEAC_R11_UNORM), glm::uvec2(4, 4));
    // ASTC is the family that names its block size, and it is the only one with a choice.
    EXPECT_EQ(Image::BlockExtentFromImageFormat(Format::eASTC_4x4_SRGB), glm::uvec2(4, 4));
    EXPECT_EQ(Image::BlockExtentFromImageFormat(Format::eASTC_6x6_UNORM), glm::uvec2(6, 6));
    EXPECT_EQ(Image::BlockExtentFromImageFormat(Format::eASTC_8x8_SRGB), glm::uvec2(8, 8));
}

TEST(ImageFormat, BlockSizesAreEightOrSixteenBytes) {
    // The 8-byte half: no independent alpha, or a single channel.
    for (const auto format : { Format::eBC1_RGB_UNORM, Format::eBC1_RGBA_UNORM, Format::eBC4_UNORM,
                               Format::eETC2_RGB8_UNORM, Format::eEAC_R11_UNORM }) {
        EXPECT_EQ(Image::BlockSizeFromImageFormat(format), 8u) << "format " << static_cast<int>(format);
    }
    // Everything else compressed, whatever its block covers.
    for (const auto format : { Format::eBC2_UNORM, Format::eBC3_UNORM, Format::eBC5_UNORM,
                               Format::eBC6H_SFLOAT, Format::eBC7_SRGB, Format::eASTC_4x4_UNORM,
                               Format::eASTC_8x8_UNORM, Format::eETC2_RGBA8_UNORM, Format::eEAC_RG11_UNORM }) {
        EXPECT_EQ(Image::BlockSizeFromImageFormat(format), 16u) << "format " << static_cast<int>(format);
    }
}

// -----------------------------------------------------------------------------
// SizeOfRegion: the number a staging buffer is sized with.
// -----------------------------------------------------------------------------
TEST(ImageFormat, RegionSizeOfUncompressedIsTexelsTimesTexelSize) {
    EXPECT_EQ(Image::SizeOfRegion(Format::eRGBA8_UNORM, { 8, 8, 1 }), 8u * 8u * 4u);
    EXPECT_EQ(Image::SizeOfRegion(Format::eRGBA32_SFLOAT, { 4, 4, 1 }), 4u * 4u * 16u);
    EXPECT_EQ(Image::SizeOfRegion(Format::eR8_UNORM, { 3, 5, 1 }), 15u);
}

TEST(ImageFormat, RegionSizeCountsLayersAndDepth) {
    EXPECT_EQ(Image::SizeOfRegion(Format::eRGBA8_UNORM, { 8, 8, 1 }, 6), 8u * 8u * 4u * 6u);
    EXPECT_EQ(Image::SizeOfRegion(Format::eRGBA8_UNORM, { 8, 8, 4 }), 8u * 8u * 4u * 4u);
    // A zero in either is read as one rather than as "nothing", so an unset depth is harmless.
    EXPECT_EQ(Image::SizeOfRegion(Format::eRGBA8_UNORM, { 8, 8, 0 }, 0), 8u * 8u * 4u);
}

TEST(ImageFormat, RegionSizeOfCompressedCountsBlocks) {
    // 8x8 BC7: 2x2 blocks of 16 bytes = 64, a quarter of the 256 bytes it would take uncompressed.
    EXPECT_EQ(Image::SizeOfRegion(Format::eBC7_UNORM, { 8, 8, 1 }), 64u);
    EXPECT_EQ(Image::SizeOfRegion(Format::eRGBA8_UNORM, { 8, 8, 1 }), 256u);
    // BC1 halves it again.
    EXPECT_EQ(Image::SizeOfRegion(Format::eBC1_RGB_UNORM, { 8, 8, 1 }), 32u);
    // Six faces of a compressed cube.
    EXPECT_EQ(Image::SizeOfRegion(Format::eBC7_UNORM, { 8, 8, 1 }, 6), 64u * 6u);
}

TEST(ImageFormat, APartialBlockStillCostsAWholeBlock) {
    // The case that silently truncates an upload if it is got wrong: a 5x5 BC7 region is 2x2 blocks,
    // not 1.25x1.25 — and the last mip levels of any texture are exactly this shape.
    EXPECT_EQ(Image::SizeOfRegion(Format::eBC7_UNORM, { 5, 5, 1 }), 4u * 16u);
    EXPECT_EQ(Image::SizeOfRegion(Format::eBC7_UNORM, { 1, 1, 1 }), 16u);
    EXPECT_EQ(Image::SizeOfRegion(Format::eBC1_RGB_UNORM, { 2, 2, 1 }), 8u);
    // ASTC 8x8: anything up to 8 texels across is one block.
    EXPECT_EQ(Image::SizeOfRegion(Format::eASTC_8x8_UNORM, { 8, 8, 1 }), 16u);
    EXPECT_EQ(Image::SizeOfRegion(Format::eASTC_8x8_UNORM, { 9, 8, 1 }), 32u);
}

TEST(ImageFormat, AMipChainOfACompressedTextureNeverGoesBelowOneBlock) {
    // What a KTX loader adds up when it sizes its upload: every level of a 16x16 BC7 chain.
    glm::u64 total = 0;
    for (glm::u32 mip = 0; mip < 5; ++mip) {
        const glm::u32 size = std::max(1u, 16u >> mip);
        total += Image::SizeOfRegion(Format::eBC7_UNORM, { size, size, 1 });
    }
    // 16x16 -> 16 blocks, 8x8 -> 4, and 4x4/2x2/1x1 -> one block each.
    EXPECT_EQ(total, (16u + 4u + 1u + 1u + 1u) * 16u);
}

// A per-channel size is meaningless for a block format, and saying so is better than returning a
// number a caller would then multiply by.
TEST(ImageFormat, ChannelSizeRefusesCompressedFormats) {
    EXPECT_THROW((void)Image::ChannelSizeFromImageFormat(Format::eBC7_UNORM), std::runtime_error);
    EXPECT_THROW((void)Image::ChannelCountFromImageFormat(Format::eASTC_4x4_UNORM), std::runtime_error);
}

} // namespace
