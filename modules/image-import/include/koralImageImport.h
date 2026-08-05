//
// Created by radue on 29.07.2026.
//

/**
 * @file koralImageImport.h
 * @brief The image *import* module: everything that turns a file into a kor::Image.
 *
 * PNG, JPEG, EXR, HDR and the rest through OpenImageIO; KTX and KTX2 with the mip chain, array
 * layers and block compression already in them. Writing images back out is the export module's
 * business (`koral-image-export`, `<koralImageExport.h>`), and producing a compressed KTX2 is the
 * compression module's (`koral-image-compress`).
 *
 * @code
 * // CMakeLists.txt:  target_link_libraries(MyScene PRIVATE Koral::Koral Koral::koral-image-import)
 *
 * auto albedo = kimg::LoadImage("textures/wood.png", true);   // true: build the mip chain
 * auto sky    = kimg::LoadCubemapFromEquirectangular("skies/sunset.hdr");
 * @endcode
 *
 * Every loader comes in two forms. The plain one blocks until the texture is on the GPU, which is
 * what you want while a scene is initialising. The `...Async` one returns a kor::Task: decoding
 * runs on background threads and the GPU upload is chunked, so a frame can keep rendering while it
 * happens.
 *
 * @section image_errors When a file cannot be read
 *
 * Nothing here throws. A missing or unreadable file comes back as a *poisoned* kor::Resource that
 * carries the reason — test it with `if (image)` and read `image.error()` — so one bad texture path
 * does not take down the frame that asked for it.
 *
 * @section image_cubemaps Cubemaps
 *
 * Two ways in, and both produce the same thing: a six-layer image, cube-compatible, ready for an
 * ImageView of type kor::ImageView::Type::eCube.
 *
 * @code
 * auto sky = kimg::LoadCubemap({ .right = "px.png", .left = "nx.png",
 *                                .top   = "py.png", .bottom = "ny.png",
 *                                .front = "pz.png", .back   = "nz.png" });
 *
 * auto view = kor::ImageView::Builder(kor::ResourceRef<const kor::Image>(sky))
 *     .setViewType(kor::ImageView::Type::eCube)
 *     .setArrayLayerCount(6)
 *     .build();
 * @endcode
 *
 * @note The equirectangular projection runs as a compute shader on the GPU. It is verified on
 *       Vulkan; on OpenGL, cube *views* are a gap in the backend rather than in this module.
 */

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include <error.h>
#include <image.h>
#include <resource.h>
#include <task.h>

/**
 * @brief Marks what crosses out of the import module's library.
 *
 * Koral and its modules build with hidden visibility, so a function a consumer *calls* has to say
 * so. Exported here, imported everywhere else; the module's own build defines
 * KORAL_IMAGE_IMPORT_EXPORTS to pick the first branch.
 */
#if defined(_WIN32)
#  if defined(KORAL_IMAGE_IMPORT_EXPORTS)
#    define KIMG_IMPORT_API __declspec(dllexport)
#  else
#    define KIMG_IMPORT_API __declspec(dllimport)
#  endif
#else
#  define KIMG_IMPORT_API __attribute__((visibility("default")))
#endif

namespace kimg
{
    /** @brief This module's identity, for another module that declares a dependency on it. */
    inline constexpr std::string_view kImportModuleId = "koral.image.import";
    inline constexpr std::uint32_t    kImportModuleVersion = 1;

    /**
     * @brief One decoded image, in CPU memory, before it is anything a GPU knows about.
     *
     * What a decoder produces and an uploader consumes. Public because it is useful on its own: the
     * compression module encodes one of these, and a project that wants to *look* at pixels rather
     * than sample them wants one too.
     */
    struct KIMG_IMPORT_API CpuImage
    {
        std::vector<unsigned char> pixels;      ///< Tightly packed, in `format`'s own layout.
        glm::uvec3 extent { 1, 1, 1 };          ///< Width, height, depth in texels.
        kor::Image::Format format = kor::Image::Format::eRGBA8_UNORM;  ///< What one texel — or block — holds.

        /** @brief How many bytes `pixels` holds for this extent and format. */
        [[nodiscard]] glm::u64 byteCount() const { return kor::Image::SizeOfRegion(format, extent); }
    };

    /**
     * @brief Decodes a file into CPU memory, without touching the GPU.
     * @param relativePath The file, resolved against the asset search roots like LoadImage.
     * @return The pixels, or why they could not be read. Three-channel sources are widened to four
     *         with an opaque alpha, because no GPU format has three channels.
     *
     * Safe to run on a background thread once the path is resolved — which is what the async loaders
     * do, and what the compression module does before it encodes.
     */
    [[nodiscard]] KIMG_IMPORT_API kor::Result<CpuImage> DecodeImageFile(const std::filesystem::path& relativePath);

    /**
     * @brief Loads an image from disk into a GPU texture.
     * @param relativePath The file. A relative path is resolved against the asset search roots —
     *        the directories listed under "assetDirectories" in koral.json, then the ones that ship
     *        with the engine — so a scene can say "textures/wood.png" without knowing where the
     *        project ended up on disk. An absolute path is opened as given.
     * @param generateMipmaps Whether to build the mip chain after uploading. A file that already
     *        carries mips (KTX) keeps its own, and a block-compressed one cannot have them generated
     *        at all — it arrives with the chain it was encoded with.
     * @return The texture, or a poisoned resource naming what went wrong.
     *
     * Blocks until the image is on the GPU. Use LoadImageAsync to keep the frame moving.
     *
     * @see kor::assetPath, kor::ProjectConfig::assetDirectories
     */
    [[nodiscard]] KIMG_IMPORT_API kor::Resource<kor::Image> LoadImage(
        const std::filesystem::path& relativePath, bool generateMipmaps = false);

    /**
     * @brief Loads an image without blocking the frame.
     * @param relativePath The file, resolved like LoadImage.
     * @param generateMipmaps Whether to build the mip chain after uploading.
     * @return A task yielding the texture. Decoding runs on background threads and the GPU upload
     *         is chunked, so the render thread is never held for long.
     */
    [[nodiscard]] KIMG_IMPORT_API kor::Task<kor::Resource<kor::Image>> LoadImageAsync(
        const std::filesystem::path& relativePath, bool generateMipmaps = false);

    /**
     * @brief The six files a cubemap is assembled from, named rather than numbered.
     *
     * Which file is which face is the one thing everybody gets wrong, so they are named for what
     * you see rather than for an axis. The order they are stored in is the standard one — +X, -X,
     * +Y, -Y, +Z, -Z — which is the layer order a cube view expects.
     */
    struct KIMG_IMPORT_API CubeFaces
    {
        std::filesystem::path right;    ///< +X
        std::filesystem::path left;     ///< -X
        std::filesystem::path top;      ///< +Y
        std::filesystem::path bottom;   ///< -Y
        std::filesystem::path front;    ///< +Z
        std::filesystem::path back;     ///< -Z

        /** @brief The faces in layer order: +X, -X, +Y, -Y, +Z, -Z. */
        [[nodiscard]] std::array<std::filesystem::path, 6> inLayerOrder() const
        {
            return { right, left, top, bottom, front, back };
        }
    };

    /**
     * @brief Assembles a cubemap from six face images.
     * @param faces The six files, resolved like LoadImage.
     * @param generateMipmaps Whether to build the mip chain once all six faces are uploaded.
     * @return A six-layer, cube-compatible image, or a poisoned resource naming the first face that
     *         could not be read — or the first one whose size or format disagrees with face 0,
     *         since a cube whose faces disagree is not a cube.
     */
    [[nodiscard]] KIMG_IMPORT_API kor::Resource<kor::Image> LoadCubemap(
        const CubeFaces& faces, bool generateMipmaps = false);

    /** @brief Assembles a cubemap from six face images without blocking the frame. @see LoadCubemap */
    [[nodiscard]] KIMG_IMPORT_API kor::Task<kor::Resource<kor::Image>> LoadCubemapAsync(
        CubeFaces faces, bool generateMipmaps = false);

    /**
     * @brief Loads an equirectangular (lat-long) image — typically an .hdr — and projects it onto a cubemap.
     * @param relativePath The panorama, resolved like LoadImage.
     * @param faceSize Pixels along one edge of a face. Zero picks a quarter of the source width,
     *        which is the size at which the projection neither invents nor discards detail.
     * @param generateMipmaps Whether to build the mip chain after projecting.
     * @return A six-layer, cube-compatible image in 32-bit float, whatever the source's precision
     *         was — a panorama is normally HDR, and that is what keeps its range.
     *
     * The projection is a compute dispatch, not a CPU loop: the panorama is uploaded once and the
     * six faces are written as the layers of a storage image.
     */
    [[nodiscard]] KIMG_IMPORT_API kor::Resource<kor::Image> LoadCubemapFromEquirectangular(
        const std::filesystem::path& relativePath, glm::u32 faceSize = 0, bool generateMipmaps = false);

    /** @brief Loads and projects an equirectangular image without blocking the frame. @see LoadCubemapFromEquirectangular */
    [[nodiscard]] KIMG_IMPORT_API kor::Task<kor::Resource<kor::Image>> LoadCubemapFromEquirectangularAsync(
        std::filesystem::path relativePath, glm::u32 faceSize = 0, bool generateMipmaps = false);

    /**
     * @brief Projects an equirectangular image already on the GPU onto a cubemap.
     * @param equirect The panorama. Must be sampleable.
     * @param faceSize Pixels along one edge of a face; zero takes a quarter of the panorama's width.
     * @param generateMipmaps Whether to build the mip chain after projecting.
     * @return A six-layer, cube-compatible image.
     *
     * What the two functions above do once the file is decoded, exposed because a panorama does not
     * have to come from a file — one rendered or generated in the same frame projects the same way.
     */
    [[nodiscard]] KIMG_IMPORT_API kor::Resource<kor::Image> EquirectangularToCubemap(
        kor::ResourceRef<const kor::Image> equirect, glm::u32 faceSize = 0, bool generateMipmaps = false);
}
