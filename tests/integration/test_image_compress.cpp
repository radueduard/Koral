// Integration coverage for the image *compression* module (modules/image-compress): encoding an
// ordinary image into a compressed KTX2, and loading the result back — which for a universal codec
// means transcoding it into whatever block format this device supports.
//
// The pair is the point. A file that was written proves the encoder ran; a file that loads back as a
// compressed texture of the right size proves the whole chain — encode, container, transcode, block
// upload — works end to end. And the file being *smaller* is, after all, the reason to do any of it.

#include "gpu_fixture.h"

#include <chrono>
#include <cstdint>
#include <span>
#include <filesystem>
#include <thread>
#include <string>
#include <vector>

#include "buffer.h"
#include "commandBuffer.h"
#include "context.h"
#include "image.h"

#include <koralImageCompress.h>
#include <koralImageExport.h>
#include <koralImageImport.h>

using kor::Buffer;
using kor::CommandBuffer;
using kor::Image;
using kor::ResourceRef;

namespace {

std::filesystem::path outDir() {
    return std::filesystem::temp_directory_path() / "koral_image_compress";
}

/**
 * @brief A source image with structure in it: smooth gradients plus a hard edge.
 *
 * Flat colour compresses to nothing and would hide a broken encoder; a gradient with an edge is what
 * a block codec actually has to work at, and it also makes the file size meaningful.
 */
kimg::CpuImage gradientSource(const std::uint32_t size) {
    kimg::CpuImage image;
    image.format = Image::Format::eRGBA8_UNORM;
    image.extent = { size, size, 1 };
    image.pixels.resize(static_cast<std::size_t>(size) * size * 4);

    for (std::uint32_t y = 0; y < size; ++y) {
        for (std::uint32_t x = 0; x < size; ++x) {
            auto* texel = image.pixels.data() + (static_cast<std::size_t>(y) * size + x) * 4;
            texel[0] = static_cast<unsigned char>(x * 255 / (size - 1));
            texel[1] = static_cast<unsigned char>(y * 255 / (size - 1));
            texel[2] = (x / 8 + y / 8) % 2 ? 220 : 40;     // a checker, for a hard edge to hold
            texel[3] = 255;
        }
    }
    return image;
}

/**
 * @brief Writes a small source image to disk, and hands back its path.
 *
 * Small on purpose: encoding is seconds of CPU per megapixel, and what these tests check is the path
 * through the module, not the encoder's throughput. A real asset pipeline runs this over 4K textures
 * and takes the time.
 */
std::filesystem::path writeSourceFile(const std::string& name, const std::uint32_t size = 64) {
    const auto source = gradientSource(size);

    auto image = Image::Builder{}
        .setType(Image::Type::e2D)
        .setFormat(Image::Format::eRGBA8_UNORM)
        .setExtent(glm::uvec2{ size, size })
        .addUsage(Image::Usage::eTransferDst)
        .addUsage(Image::Usage::eTransferSrc)
        .build();

    const auto staging = Buffer::Builder<unsigned char>()
        .setDataView(std::span<const unsigned char>(source.pixels))
        .setUsage(Buffer::Usage::eTransferSrc)
        .setType(Buffer::Type::eStaging)
        .build();
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyBufferToImage(staging, ResourceRef<const Image>(image), kor::Copy{
            .imageOffset = { 0, 0, 0 }, .imageExtent = { size, size, 1 } });
    }, CommandBuffer::Usage::eTransfer);

    const auto written = kimg::SaveImage(outDir(), name, kimg::FileFormat::ePNG, ResourceRef<const Image>(image));
    EXPECT_TRUE(written.has_value()) << (written.has_value() ? std::string{} : written.error().message);
    return written.has_value() ? *written : std::filesystem::path{};
}

/** @brief Whether the device has any block-compressed format at all. */
bool deviceHasBlockCompression() {
    return Image::IsFormatSupported(Image::Format::eBC7_UNORM)
        || Image::IsFormatSupported(Image::Format::eBC3_UNORM)
        || Image::IsFormatSupported(Image::Format::eASTC_4x4_UNORM)
        || Image::IsFormatSupported(Image::Format::eETC2_RGBA8_UNORM);
}

// ---- encoding ----------------------------------------------------------------------------------

// UASTC is the default and the interesting case: the file is an intermediate that becomes a real
// block format on load, so this test covers the encoder *and* the transcoder.
TEST_F(GpuTest, CompressUastcRoundTripsThroughTheLoader) {
    constexpr std::uint32_t kSize = 64;
    const auto source = gradientSource(kSize);

    const auto written = kimg::CompressToKtx(source, outDir(), "uastc", { .codec = kimg::Codec::eUASTC });
    ASSERT_TRUE(written.has_value()) << written.error().message;
    ASSERT_TRUE(std::filesystem::exists(*written));
    EXPECT_EQ(written->extension(), ".ktx2");

    // Smaller than the raw texels it came from — which is the entire reason for the module.
    const auto rawBytes = static_cast<std::uintmax_t>(kSize) * kSize * 4;
    EXPECT_LT(std::filesystem::file_size(*written), rawBytes);

    auto reloaded = kimg::LoadImage(*written);
    ASSERT_TRUE(static_cast<bool>(reloaded)) << (reloaded.error() ? reloaded.error()->message : "");
    EXPECT_EQ(reloaded->getExtent(), glm::uvec3(kSize, kSize, 1));
    // A mip chain, because the encoder was asked for one and a compressed texture cannot be given
    // one afterwards. 64 -> 1 is seven levels.
    EXPECT_EQ(reloaded->getMipLevels(), 7u);

    // On any device with block compression the loader must have transcoded into one of them rather
    // than falling back to uncompressed.
    if (deviceHasBlockCompression()) {
        EXPECT_TRUE(Image::IsBlockCompressed(reloaded->getFormat()))
            << "transcoded to format " << static_cast<int>(reloaded->getFormat());
    }
}

// ETC1S trades quality for size, and the size is the observable part.
TEST_F(GpuTest, CompressEtc1sIsSmallerThanUastc) {
    constexpr std::uint32_t kSize = 64;
    const auto source = gradientSource(kSize);

    const auto uastc = kimg::CompressToKtx(source, outDir(), "cmp-uastc", { .codec = kimg::Codec::eUASTC });
    const auto etc1s = kimg::CompressToKtx(source, outDir(), "cmp-etc1s", { .codec = kimg::Codec::eETC1S });
    ASSERT_TRUE(uastc.has_value()) << uastc.error().message;
    ASSERT_TRUE(etc1s.has_value()) << etc1s.error().message;

    EXPECT_LT(std::filesystem::file_size(*etc1s), std::filesystem::file_size(*uastc));

    auto reloaded = kimg::LoadImage(*etc1s);
    ASSERT_TRUE(static_cast<bool>(reloaded)) << (reloaded.error() ? reloaded.error()->message : "");
    EXPECT_EQ(reloaded->getExtent(), glm::uvec3(kSize, kSize, 1));
}

// ASTC is encoded directly rather than transcoded, so the file names a real GPU format — and can only
// be loaded where the device has it.
TEST_F(GpuTest, CompressAstcWritesARealGpuFormat) {
    const auto source = gradientSource(32);

    const auto written = kimg::CompressToKtx(source, outDir(), "astc", { .codec = kimg::Codec::eASTC });
    ASSERT_TRUE(written.has_value()) << written.error().message;
    ASSERT_TRUE(std::filesystem::exists(*written));

    auto reloaded = kimg::LoadImage(*written);
    if (Image::IsFormatSupported(Image::Format::eASTC_4x4_SRGB)
        || Image::IsFormatSupported(Image::Format::eASTC_4x4_UNORM)) {
        ASSERT_TRUE(static_cast<bool>(reloaded)) << (reloaded.error() ? reloaded.error()->message : "");
        EXPECT_EQ(reloaded->getExtent(), glm::uvec3(32, 32, 1));
        EXPECT_TRUE(Image::IsBlockCompressed(reloaded->getFormat()));
    } else {
        // No ASTC here — a desktop GPU, most likely. The file is still valid; this device simply
        // cannot hold it, and the loader has to say so rather than crash.
        EXPECT_FALSE(static_cast<bool>(reloaded));
    }
}

// Mips are the reason to compress ahead of time at all: they cannot be generated for a block format
// on the GPU, so the encoder is where they come from — and asking for none must give none.
TEST_F(GpuTest, CompressCanBeAskedForNoMipmaps) {
    const auto source = gradientSource(32);

    const auto written = kimg::CompressToKtx(source, outDir(), "flat",
                                             { .codec = kimg::Codec::eUASTC, .generateMipmaps = false });
    ASSERT_TRUE(written.has_value()) << written.error().message;

    auto reloaded = kimg::LoadImage(*written);
    ASSERT_TRUE(static_cast<bool>(reloaded)) << (reloaded.error() ? reloaded.error()->message : "");
    EXPECT_EQ(reloaded->getMipLevels(), 1u);
}

// Supercompression shrinks the file without changing what the GPU gets.
TEST_F(GpuTest, CompressSupercompressionShrinksTheFile) {
    const auto source = gradientSource(64);

    const auto plain = kimg::CompressToKtx(source, outDir(), "plain",
                                           { .codec = kimg::Codec::eUASTC, .supercompress = false });
    const auto zstd = kimg::CompressToKtx(source, outDir(), "zstd",
                                          { .codec = kimg::Codec::eUASTC, .supercompress = true });
    ASSERT_TRUE(plain.has_value()) << plain.error().message;
    ASSERT_TRUE(zstd.has_value()) << zstd.error().message;

    EXPECT_LT(std::filesystem::file_size(*zstd), std::filesystem::file_size(*plain));

    // ...and both still load, to the same size.
    auto a = kimg::LoadImage(*plain);
    auto b = kimg::LoadImage(*zstd);
    ASSERT_TRUE(static_cast<bool>(a)) << (a.error() ? a.error()->message : "");
    ASSERT_TRUE(static_cast<bool>(b)) << (b.error() ? b.error()->message : "");
    EXPECT_EQ(a->getExtent(), b->getExtent());
}

// ---- from a file, which is how a build step uses it ---------------------------------------------

// The whole intended workflow: an image on disk becomes a compressed texture on disk, named after it.
TEST_F(GpuTest, CompressReadsAnImageFileAndNamesTheOutputAfterIt) {
    const auto source = writeSourceFile("wood");
    ASSERT_TRUE(std::filesystem::exists(source));

    const auto written = kimg::CompressToKtx(source, outDir());
    ASSERT_TRUE(written.has_value()) << written.error().message;
    EXPECT_EQ(written->stem(), "wood");
    EXPECT_EQ(written->extension(), ".ktx2");

    // Compressed, and far smaller than the same texture uncompressed on the GPU would be.
    auto reloaded = kimg::LoadImage(*written);
    ASSERT_TRUE(static_cast<bool>(reloaded)) << (reloaded.error() ? reloaded.error()->message : "");
    if (deviceHasBlockCompression()) {
        EXPECT_TRUE(Image::IsBlockCompressed(reloaded->getFormat()));
    }
}

// The async form, run to completion the way the headless engine runs a job.
TEST_F(GpuTest, CompressRunsAsynchronously) {
    const auto source = writeSourceFile("async-source", 32);

    auto task = kimg::CompressToKtxAsync(source, outDir(), { .codec = kimg::Codec::eETC1S, .quality = 1 });
    while (!task.done()) {
        kor::Context::DrainMainThread();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    auto result = task.take();
    ASSERT_TRUE(result.has_value()) << result.error();
    ASSERT_TRUE(result->has_value()) << (*result).error().message;
    EXPECT_TRUE(std::filesystem::exists(**result));
}

// ---- refusals ----------------------------------------------------------------------------------

// An HDR source has no business in an 8-bit encoder, and saying so beats truncating it silently.
TEST_F(GpuTest, CompressRefusesAFloatSource) {
    kimg::CpuImage floats;
    floats.format = Image::Format::eRGBA32_SFLOAT;
    floats.extent = { 4, 4, 1 };
    floats.pixels.resize(4 * 4 * 16);

    const auto written = kimg::CompressToKtx(floats, outDir(), "hdr");
    ASSERT_FALSE(written.has_value());
    EXPECT_NE(written.error().message.find("8-bit"), std::string::npos);
}

TEST_F(GpuTest, CompressReportsAMissingSource) {
    const auto written = kimg::CompressToKtx("no/such/texture.png", outDir());
    ASSERT_FALSE(written.has_value());
    EXPECT_NE(written.error().message.find("texture.png"), std::string::npos);
}

} // namespace
