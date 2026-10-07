// Integration coverage for the image *import* module (modules/image-import): decoding and uploading a
// file, and the two ways into a cubemap — six face files assembled into one image, and an
// equirectangular panorama projected onto one by the module's compute shader. It writes files too, to
// have something to read, which is what the export module is linked here for; the writers themselves
// are covered by test_image_export.cpp.
//
// The cubemap tests build their own inputs rather than shipping fixtures: six single-colour images
// saved to a temp directory for the assembly path, and a procedurally generated panorama whose
// texels encode the direction they look along for the projection path. That second one is what
// makes the projection *checkable* — the face a texel lands on and where on that face it lands are
// both implied by the colour that arrives, so a mapping that is upside down, mirrored, or rotated
// a quarter turn fails rather than merely looking plausible.

#include "gpu_fixture.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <numbers>
#include <thread>
#include <vector>

#include "buffer.h"
#include "commandBuffer.h"
#include "error.h"
#include "context.h"
#include "image.h"
#include "imageView.h"

#include <koralImageExport.h>
#include <koralImageImport.h>

using kor::Buffer;
using kor::CommandBuffer;
using kor::Image;
using kor::ResourceRef;

namespace {

constexpr std::uint32_t kFaceCount = 6;

// The direction the centre of each face looks along, in the order the layers are stored:
// +X, -X, +Y, -Y, +Z, -Z.
constexpr std::array<kor::Vec3, kFaceCount> kFaceDirections{
    kor::Vec3{ 1.f, 0.f, 0.f }, kor::Vec3{ -1.f, 0.f, 0.f },
    kor::Vec3{ 0.f, 1.f, 0.f }, kor::Vec3{ 0.f, -1.f, 0.f },
    kor::Vec3{ 0.f, 0.f, 1.f }, kor::Vec3{ 0.f, 0.f, -1.f },
};

std::filesystem::path tempDir() {
    return std::filesystem::temp_directory_path();
}

/**
 * @brief Runs a task to completion, the way the headless engine runs a job.
 *
 * The async loaders hand work between the main thread and the background pool, so something has to
 * pump the main-thread executor — in a real application that is the frame loop.
 */
template<typename T>
T runToCompletion(kor::Task<T> task) {
    while (!task.Done()) {
        kor::Context::DrainMainThread();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    auto result = task.Take();
    EXPECT_TRUE(result.has_value()) << (result.has_value() ? std::string{} : result.error());
    return result.has_value() ? std::move(*result) : T{};
}

/** @brief An 8x8 image of one colour, on the GPU, ready to be saved or read. */
kor::Resource<Image> solidImage(const kor::Vec4 color, const std::uint32_t size = 8) {
    auto image = Image::Builder{}
        .SetType(Image::Type::e2D)
        .SetFormat(Image::Format::eRGBA8_UNORM)
        .SetExtent(kor::UVec2{ size, size })
        .SetUsage(Image::Usage::eTransferDst | Image::Usage::eTransferSrc | Image::Usage::eSampled)
        .Build();
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.ClearColorImage(image, color);
    }, CommandBuffer::Usage::eGraphics).Wait();
    return image;
}

/** @brief Reads one mip-0 layer of an image back as floats, whatever it is stored as. */
std::vector<kor::Vec4> readLayerAsFloat(const ResourceRef<const Image>& image, const std::uint32_t layer) {
    const auto extent = image->Extent();
    const auto texels = static_cast<std::size_t>(extent.x) * extent.y;

    Buffer::RawBuilder rb;
    rb.SetRawSize(static_cast<kor::i64>(texels * sizeof(kor::Vec4)))
      .SetUsage(Buffer::Usage::eTransferDst)
      .SetType(Buffer::Type::eReadback);
    auto readback = rb.Build();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(image, readback, kor::Copy{
            .imageOffset = { 0, 0, 0 },
            .imageExtent = { extent.x, extent.y, 1 },
            .imageBaseArrayLayer = layer,
            .imageLayerCount = 1,
            .imageMipLevel = 0,
        });
    }, CommandBuffer::Usage::eTransfer).Wait();

    return readback->Read<kor::Vec4>();
}

/** @brief Reads one mip-0 layer of an 8-bit image back. */
std::vector<kor::U8Vec4> readLayerAsBytes(const ResourceRef<const Image>& image, const std::uint32_t layer) {
    const auto extent = image->Extent();
    const auto texels = static_cast<std::size_t>(extent.x) * extent.y;

    Buffer::RawBuilder rb;
    rb.SetRawSize(static_cast<kor::i64>(texels * sizeof(kor::U8Vec4)))
      .SetUsage(Buffer::Usage::eTransferDst)
      .SetType(Buffer::Type::eReadback);
    auto readback = rb.Build();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(image, readback, kor::Copy{
            .imageOffset = { 0, 0, 0 },
            .imageExtent = { extent.x, extent.y, 1 },
            .imageBaseArrayLayer = layer,
            .imageLayerCount = 1,
            .imageMipLevel = 0,
        });
    }, CommandBuffer::Usage::eTransfer).Wait();

    return readback->Read<kor::U8Vec4>();
}

// ---- files ------------------------------------------------------------------------------------

// Decode one of the helmet's textures off disk and upload it, with and without a generated mip
// chain — exercises kimg::LoadImage end to end.
TEST_F(GpuTest, ImageModuleLoadsFromDisk) {
    const auto path = kor::AssetPath("DamagedHelmet/Default_albedo.jpg");

    auto image = kimg::LoadImage(path);
    ASSERT_TRUE(static_cast<bool>(image));
    const kor::UVec3 extent = image->Extent();
    EXPECT_GT(extent.x, 0u);
    EXPECT_GT(extent.y, 0u);

    auto mipped = kimg::LoadImage(path, /*generateMipmaps=*/true);
    ASSERT_TRUE(static_cast<bool>(mipped));
    EXPECT_EQ(mipped->Extent(), extent);
    EXPECT_GT(mipped->MipLevels(), 1u);
}

// The same file through the coroutine path. Run to completion here rather than across frames:
// what is being checked is that it arrives, not that it interleaves.
TEST_F(GpuTest, ImageModuleLoadsFromDiskAsync) {
    const auto path = kor::AssetPath("DamagedHelmet/Default_albedo.jpg");

    auto image = runToCompletion(kimg::LoadImageAsync(path));
    ASSERT_TRUE(static_cast<bool>(image));
    EXPECT_GT(image->Extent().x, 0u);
}

// A file that is not there must poison its own resource and say so, not throw and not come back
// looking valid.
TEST_F(GpuTest, ImageModuleReportsAMissingFile) {
    auto image = kimg::LoadImage("no/such/texture.png");
    EXPECT_FALSE(static_cast<bool>(image));
    ASSERT_TRUE(image.Failure());
    EXPECT_NE(image.Failure()->message.find("texture.png"), std::string::npos);
}

// Save a GPU image to disk and read it back, verifying the pixels survive the round-trip. Drives
// kimg::SaveImage (CopyImageToBuffer + OIIO encode) and the decode path again on a file this test
// produced.
TEST_F(GpuTest, ImageModuleSaveRoundTrips) {
    constexpr std::uint32_t kSize = 8;
    // 0.25/0.5/0.75 -> 64/128/191 in UNORM8.
    auto image = solidImage({ 0.25f, 0.5f, 0.75f, 1.f }, kSize);

    // <directory>/<name><extension for the container>: all three arguments matter.
    const auto outPath = tempDir() / "koral_image_roundtrip.png";
    std::error_code ec;
    std::filesystem::remove(outPath, ec);
    ASSERT_TRUE(kimg::SaveImage(tempDir(), "koral_image_roundtrip", kimg::FileFormat::ePNG,
                               image).has_value());
    ASSERT_TRUE(std::filesystem::exists(outPath));

    auto reloaded = kimg::LoadImage(outPath);
    ASSERT_TRUE(static_cast<bool>(reloaded));
    EXPECT_EQ(reloaded->Extent(), kor::UVec3(kSize, kSize, 1));

    const auto texels = readLayerAsBytes(reloaded, 0);
    ASSERT_EQ(texels.size(), static_cast<std::size_t>(kSize) * kSize);
    for (const auto& t : texels) {
        EXPECT_NEAR(t.x, 64, 2);
        EXPECT_NEAR(t.y, 128, 2);
        EXPECT_NEAR(t.z, 191, 2);
    }
    std::filesystem::remove(outPath, ec);
}

// ---- cubemap from six files -------------------------------------------------------------------

// Writes six single-colour faces to disk and gives them back their layer index as a colour, so
// assembling them wrong — a swapped pair, an off-by-one — shows up as the wrong layer.
struct SixFaces {
    kimg::CubeFaces faces;
    std::array<kor::U8Vec4, kFaceCount> colors;

    void remove() const {
        std::error_code ec;
        for (const auto& path : faces.InLayerOrder()) std::filesystem::remove(path, ec);
    }
};

SixFaces writeSixFaces(const std::string& prefix, const std::uint32_t size = 8) {
    static constexpr std::array<const char*, kFaceCount> kNames{ "px", "nx", "py", "ny", "pz", "nz" };

    SixFaces written{};
    std::array<std::filesystem::path, kFaceCount> paths{};
    for (std::uint32_t face = 0; face < kFaceCount; ++face) {
        // One channel per face index, well apart in value so a mix-up cannot pass.
        const float level = static_cast<float>(face + 1) / 8.f;
        auto image = solidImage({ level, 1.f - level, 0.5f, 1.f }, size);

        const auto stem = prefix + "_" + kNames[face];
        paths[face] = tempDir() / (stem + ".png");
        std::error_code ec;
        std::filesystem::remove(paths[face], ec);
        const auto saved = kimg::SaveImage(tempDir(), stem, kimg::FileFormat::ePNG,
                                           image);
        EXPECT_TRUE(saved.has_value()) << (saved.has_value() ? std::string{} : saved.error().message);

        written.colors[face] = kor::U8Vec4{
            static_cast<std::uint8_t>(std::lround(level * 255.f)),
            static_cast<std::uint8_t>(std::lround((1.f - level) * 255.f)),
            128, 255 };
    }

    written.faces = kimg::CubeFaces{
        .right = paths[0], .left = paths[1], .top = paths[2],
        .bottom = paths[3], .front = paths[4], .back = paths[5] };
    return written;
}

void expectFacesInOrder(const kor::Resource<Image>& cube, const SixFaces& source) {
    ASSERT_TRUE(static_cast<bool>(cube));
    EXPECT_EQ(cube->ArrayLayers(), kFaceCount);
    EXPECT_EQ(cube->Extent(), kor::UVec3(8, 8, 1));

    for (std::uint32_t face = 0; face < kFaceCount; ++face) {
        const auto texels = readLayerAsBytes(cube, face);
        ASSERT_FALSE(texels.empty()) << "face " << face;
        EXPECT_NEAR(texels.front().x, source.colors[face].x, 2) << "face " << face;
        EXPECT_NEAR(texels.front().y, source.colors[face].y, 2) << "face " << face;
    }
}

TEST_F(GpuTest, CubemapFromSixFiles) {
    const auto source = writeSixFaces("koral_cube_sync");
    auto cube = kimg::LoadCubemap(source.faces);
    expectFacesInOrder(cube, source);
    source.remove();
}

TEST_F(GpuTest, CubemapFromSixFilesAsync) {
    const auto source = writeSixFaces("koral_cube_async");
    auto cube = runToCompletion(kimg::LoadCubemapAsync(source.faces));
    expectFacesInOrder(cube, source);
    source.remove();
}

// A cube is six *matching* faces. One that is a different size has to be refused by name rather
// than uploaded into a layer it does not fit.
TEST_F(GpuTest, CubemapRejectsMismatchedFaces) {
    auto source = writeSixFaces("koral_cube_bad");

    const auto oddPath = tempDir() / "koral_cube_bad_odd.png";
    auto odd = solidImage({ 1.f, 1.f, 1.f, 1.f }, 16);
    ASSERT_TRUE(kimg::SaveImage(tempDir(), "koral_cube_bad_odd", kimg::FileFormat::ePNG,
                               odd).has_value());
    source.faces.top = oddPath;

    auto cube = kimg::LoadCubemap(source.faces);
    EXPECT_FALSE(static_cast<bool>(cube));
    ASSERT_TRUE(cube.Failure());
    EXPECT_NE(cube.Failure()->message.find("every face must match"), std::string::npos);

    std::error_code ec;
    std::filesystem::remove(oddPath, ec);
    source.remove();
}

// ---- cubemap from an equirectangular panorama --------------------------------------------------

/**
 * @brief A panorama whose every texel holds the direction it looks along, encoded as a colour.
 *
 * The inverse of what the projection shader does, which is what makes the round-trip checkable:
 * project this, and the colour that lands on a face's centre texel must be that face's own
 * direction. Anything the mapping gets wrong — a flip, a mirror, a quarter turn, the wrong face
 * order — moves a colour somewhere it does not belong.
 */
kor::Resource<Image> directionPanorama(const std::uint32_t width, const std::uint32_t height) {
    std::vector<kor::Vec4> texels(static_cast<std::size_t>(width) * height);
    for (std::uint32_t y = 0; y < height; ++y) {
        // Latitude runs from +Y at row 0 down to -Y at the last row, matching the shader.
        const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(height);
        const float latitude = v * std::numbers::pi_v<float>;
        for (std::uint32_t x = 0; x < width; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(width);
            const float longitude = (u - 0.5f) * 2.f * std::numbers::pi_v<float>;

            const kor::Vec3 direction{
                std::sin(latitude) * std::cos(longitude),
                std::cos(latitude),
                std::sin(latitude) * std::sin(longitude),
            };
            texels[static_cast<std::size_t>(y) * width + x] = kor::Vec4(direction, 1.f);
        }
    }

    auto image = Image::Builder{}
        .SetType(Image::Type::e2D)
        .SetFormat(Image::Format::eRGBA32_SFLOAT)
        .SetExtent(kor::UVec2{ width, height })
        .SetUsage(Image::Usage::eTransferDst | Image::Usage::eTransferSrc | Image::Usage::eSampled)
        .Build();

    const auto staging = Buffer::Builder<kor::Vec4>()
        .SetDataView(std::span<const kor::Vec4>(texels))
        .SetUsage(Buffer::Usage::eTransferSrc)
        .SetType(Buffer::Type::eStaging)
        .Build();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyBufferToImage(staging, image, kor::Copy{
            .imageOffset = { 0, 0, 0 },
            .imageExtent = { width, height, 1 },
        });
        cb.Barrier({}, {{ ResourceRef<const Image>(image), kor::ResourceAccess::eAllShaderRead }});
    }).Wait();
    return image;
}

TEST_F(GpuTest, EquirectangularProjectsOntoTheRightFaces) {
    auto panorama = directionPanorama(256, 128);
    ASSERT_TRUE(static_cast<bool>(panorama));

    constexpr std::uint32_t kFaceSize = 32;
    auto cube = kimg::EquirectangularToCubemap(panorama, kFaceSize);
    ASSERT_TRUE(static_cast<bool>(cube)) << (cube.Failure() ? cube.Failure()->message : "");
    EXPECT_EQ(cube->ArrayLayers(), kFaceCount);
    EXPECT_EQ(cube->Extent(), kor::UVec3(kFaceSize, kFaceSize, 1));
    // 32-bit float whatever the panorama was, so an HDR source keeps its range.
    EXPECT_EQ(cube->PixelFormat(), Image::Format::eRGBA32_SFLOAT);

    for (std::uint32_t face = 0; face < kFaceCount; ++face) {
        const auto texels = readLayerAsFloat(cube, face);
        ASSERT_EQ(texels.size(), static_cast<std::size_t>(kFaceSize) * kFaceSize) << "face " << face;

        // The four texels around the face centre; a face's centre falls between texels for an
        // even face size, and any of the four is within a texel of the axis.
        const std::size_t mid = kFaceSize / 2;
        const auto& centre = texels[mid * kFaceSize + mid];
        const kor::Vec3 direction{ centre.x, centre.y, centre.z };

        // Within a texel of the axis the face points along: a mapping that is flipped or rotated
        // lands a neighbouring direction here instead, which is far outside this.
        EXPECT_GT(kor::Dot(kor::Normalize(direction), kFaceDirections[face]), 0.97f)
            << "face " << face << " centre looks along ("
            << direction.x << ", " << direction.y << ", " << direction.z << ")";
    }
}

// The corners are where a projection that is subtly wrong shows up: each corner of a face must
// look along a diagonal, and the three faces meeting at a corner must agree about it.
TEST_F(GpuTest, EquirectangularKeepsFaceCornersConsistent) {
    auto panorama = directionPanorama(256, 128);
    constexpr std::uint32_t kFaceSize = 32;
    auto cube = kimg::EquirectangularToCubemap(panorama, kFaceSize);
    ASSERT_TRUE(static_cast<bool>(cube)) << (cube.Failure() ? cube.Failure()->message : "");

    for (std::uint32_t face = 0; face < kFaceCount; ++face) {
        const auto texels = readLayerAsFloat(cube, face);
        for (const std::size_t y : { std::size_t{0}, static_cast<std::size_t>(kFaceSize - 1) }) {
            for (const std::size_t x : { std::size_t{0}, static_cast<std::size_t>(kFaceSize - 1) }) {
                const auto& texel = texels[y * kFaceSize + x];
                const kor::Vec3 direction{ texel.x, texel.y, texel.z };
                ASSERT_GT(kor::Length(direction), 0.5f) << "face " << face << " corner is empty";

                // A corner texel of a cube face looks along a direction whose three components are
                // all about equal in magnitude — the cube's diagonal.
                const kor::Vec3 unit = kor::Abs(kor::Normalize(direction));
                EXPECT_NEAR(unit.x, unit.y, 0.12f) << "face " << face;
                EXPECT_NEAR(unit.y, unit.z, 0.12f) << "face " << face;
            }
        }
    }
}

// The file path, end to end and in the format it exists for: write the panorama out as a real .hdr,
// then load and project it in one call. Also the only test of the module resolving its own shipped
// shader by name, and of SaveImage writing something that is not 8-bit.
TEST_F(GpuTest, EquirectangularFromFile) {
    auto panorama = directionPanorama(128, 64);

    const auto outPath = tempDir() / "koral_panorama.hdr";
    std::error_code ec;
    std::filesystem::remove(outPath, ec);
    ASSERT_TRUE(kimg::SaveImage(tempDir(), "koral_panorama", kimg::FileFormat::eHDR,
                               panorama).has_value());
    ASSERT_TRUE(std::filesystem::exists(outPath));

    // The file has to carry the panorama, not merely exist: an .hdr decodes back to floats, and a
    // texel in the middle of the top row looks nearly straight up.
    auto reloaded = kimg::LoadImage(outPath);
    ASSERT_TRUE(static_cast<bool>(reloaded)) << (reloaded.Failure() ? reloaded.Failure()->message : "");
    EXPECT_EQ(reloaded->Extent(), kor::UVec3(128, 64, 1));
    const auto rows = readLayerAsFloat(reloaded, 0);
    ASSERT_EQ(rows.size(), static_cast<std::size_t>(128) * 64);
    EXPECT_GT(rows[64].y, 0.9f) << "the top row of the panorama should look up";

    auto cube = kimg::LoadCubemapFromEquirectangular(outPath, 16);
    ASSERT_TRUE(static_cast<bool>(cube)) << (cube.Failure() ? cube.Failure()->message : "");
    EXPECT_EQ(cube->ArrayLayers(), kFaceCount);
    EXPECT_EQ(cube->Extent(), kor::UVec3(16, 16, 1));

    auto async = runToCompletion(kimg::LoadCubemapFromEquirectangularAsync(outPath, 16));
    ASSERT_TRUE(static_cast<bool>(async)) << (async.Failure() ? async.Failure()->message : "");
    EXPECT_EQ(async->ArrayLayers(), kFaceCount);

    std::filesystem::remove(outPath, ec);
}

// Face size defaults to a quarter of the panorama's width — the size at which a face's texels are
// about as dense as the source's.
TEST_F(GpuTest, EquirectangularDefaultsFaceSizeToAQuarterOfTheWidth) {
    auto panorama = directionPanorama(128, 64);
    auto cube = kimg::EquirectangularToCubemap(panorama);
    ASSERT_TRUE(static_cast<bool>(cube)) << (cube.Failure() ? cube.Failure()->message : "");
    EXPECT_EQ(cube->Extent(), kor::UVec3(32, 32, 1));
}

// A cubemap is only useful if it can be *sampled* as one, which needs the image to be
// cube-compatible — six 2D layers, which is what both loaders produce.
TEST_F(GpuTest, CubemapCanBeViewedAsACube) {
    const auto source = writeSixFaces("koral_cube_view");
    auto cube = kimg::LoadCubemap(source.faces);
    ASSERT_TRUE(static_cast<bool>(cube));

    auto view = kor::ImageView::Builder(cube)
        .SetViewType(kor::ImageView::Type::eCube)
        .SetArrayLayerCount(kFaceCount)
        .Build();
    EXPECT_TRUE(static_cast<bool>(view)) << (view.Failure() ? view.Failure()->message : "");

    source.remove();
}


// ---- block-compressed images -------------------------------------------------------------------

// The whole compressed path in one test: create a BC7 image, upload blocks into it, read them back
// and compare byte for byte. Content is deliberately arbitrary — what is being checked is that the
// engine counts *blocks* rather than texels everywhere along the way (staging size, the copy guard,
// the backend's compressed entry points). BC7 and BC1 are required of every desktop Vulkan device.
TEST_F(GpuTest, CompressedImageUploadRoundTrips) {
    for (const auto format : { Image::Format::eBC7_UNORM, Image::Format::eBC1_RGB_UNORM }) {
        constexpr std::uint32_t kSize = 16;   // 4x4 blocks
        const auto byteCount = Image::SizeOfRegion(format, { kSize, kSize, 1 });
        EXPECT_EQ(byteCount, format == Image::Format::eBC7_UNORM ? 256u : 128u);

        // Recognisable bytes: block n is filled with n, so a misplaced block is visible.
        std::vector<std::uint8_t> blocks(byteCount);
        for (std::size_t i = 0; i < blocks.size(); ++i)
            blocks[i] = static_cast<std::uint8_t>((i / Image::BlockSize(format)) + 1);

        auto image = Image::Builder{}
            .SetType(Image::Type::e2D)
            .SetFormat(format)
            .SetExtent(kor::UVec2{ kSize, kSize })
            .SetUsage(Image::Usage::eTransferDst | Image::Usage::eTransferSrc | Image::Usage::eSampled)
            .Build();
        ASSERT_TRUE(static_cast<bool>(image)) << (image.Failure() ? image.Failure()->message : "");

        const auto staging = Buffer::Builder<std::uint8_t>()
            .SetDataView(std::span<const std::uint8_t>(blocks))
            .SetUsage(Buffer::Usage::eTransferSrc)
            .SetType(Buffer::Type::eStaging)
            .Build();

        Buffer::RawBuilder rb;
        rb.SetRawSize(static_cast<kor::i64>(byteCount))
          .SetUsage(Buffer::Usage::eTransferDst)
          .SetType(Buffer::Type::eReadback);
        auto readback = rb.Build();

        CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
            cb.CopyBufferToImage(staging, image, kor::Copy{
                .imageOffset = { 0, 0, 0 },
                .imageExtent = { kSize, kSize, 1 },
            });
            cb.CopyImageToBuffer(image, readback, kor::Copy{
                .imageOffset = { 0, 0, 0 },
                .imageExtent = { kSize, kSize, 1 },
            });
        }, CommandBuffer::Usage::eTransfer).Wait();

        const auto out = readback->Read<std::uint8_t>();
        ASSERT_EQ(out.size(), blocks.size()) << "format " << static_cast<int>(format);
        EXPECT_EQ(out, blocks) << "format " << static_cast<int>(format);
    }
}

// Mips are made by blitting, which a compressed format cannot be the destination of. The engine has
// to say so rather than let the backend produce a wall of validation errors.
TEST_F(GpuTest, CompressedImageRefusesGeneratedMipmaps) {
    auto image = Image::Builder{}
        .SetType(Image::Type::e2D)
        .SetFormat(Image::Format::eBC7_UNORM)
        .SetExtent(kor::UVec2{ 16, 16 })
        .SetMipLevels(5)
        .SetUsage(Image::Usage::eTransferDst | Image::Usage::eTransferSrc)
        .Build();
    ASSERT_TRUE(static_cast<bool>(image));

    auto cb = CommandBuffer::Create(CommandBuffer::Usage::eGraphics);
    cb->Begin();
    cb->GenerateMipmaps(image);
    cb->End();

    EXPECT_FALSE(cb->Ok());
    ASSERT_FALSE(cb->Errors().empty());
    EXPECT_EQ(cb->Errors().front().code, kor::ErrorCode::eInvalidArgument);
    EXPECT_NE(cb->Errors().front().message.find("block-compressed"), std::string::npos);
}

} // namespace
