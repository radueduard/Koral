//
// Created by radue on 30.07.2026.
//

/**
 * @file koralImageExport.h
 * @brief The image *export* module: writing a kor::Image, or any part of one, to disk.
 *
 * The other half of the import module. Two ways out, because there are two questions:
 *
 * @code
 * // CMakeLists.txt:  target_link_libraries(MyScene PRIVATE Koral::Koral Koral::koral-image-export)
 *
 * // one subimage, in any container: a screenshot, a debug view of mip 3, one cube face
 * kimg::SaveImage("out", "albedo", kimg::FileFormat::ePNG, image);
 * kimg::SaveImage("out", "face-py", kimg::FileFormat::ePNG, sky, { .arrayLayer = 2 });
 *
 * // the whole thing — every mip of every layer — in the one container that can hold it
 * kimg::SaveImageSet("out", "sky", sky);            // -> out/sky.ktx2
 * @endcode
 *
 * Both come in an `...Async` form as well: the GPU read-back and the encode happen off the render
 * thread, which for a 4K image is the difference between a hitch and none.
 *
 * @section export_what What can be written
 *
 * Anything the GPU will hand back. Depth formats, float formats, array layers, 3D slices, and
 * *block-compressed* images — those last only into KTX2, since a PNG has nowhere to put a BC7 block
 * and this module deliberately does not decode one to invent something to write.
 *
 * A container takes what it can: PNG holds 8- or 16-bit, `.hdr` holds three float channels, EXR and
 * TIFF hold whatever you give them. OpenImageIO converts on write, and the alpha is dropped when the
 * container has none rather than the write being refused.
 *
 * @section export_errors When a write fails
 *
 * Nothing here throws. Every function returns a kor::VoidResult (or a kor::Result with the path it
 * wrote), so a failed save is a value you can look at — and it names the file.
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include <glm/glm.hpp>

#include <error.h>
#include <image.h>
#include <resource.h>
#include <task.h>

/**
 * @brief Marks what crosses out of the export module's library.
 *
 * Koral and its modules build with hidden visibility, so a function a consumer *calls* has to say
 * so. The module's own build defines KORAL_IMAGE_EXPORT_EXPORTS to pick the first branch.
 */
#if defined(_WIN32)
#  if defined(KORAL_IMAGE_EXPORT_EXPORTS)
#    define KIMG_EXPORT_API __declspec(dllexport)
#  else
#    define KIMG_EXPORT_API __declspec(dllimport)
#  endif
#else
#  define KIMG_EXPORT_API __attribute__((visibility("default")))
#endif

namespace kimg
{
    /** @brief This module's identity, for another module that declares a dependency on it. */
    inline constexpr std::string_view ExportModuleId = "koral.image.export";
    inline constexpr std::uint32_t    ExportModuleVersion = 1;

    /** @brief Which container to write. The file's extension follows from it. */
    enum class FileFormat : std::uint8_t {
        ePNG,   ///< 8- or 16-bit, lossless, alpha. The default for a screenshot.
        eJPG,   ///< 8-bit, lossy, no alpha.
        eBMP,
        eTGA,
        eHDR,   ///< Radiance RGBE: three channels, high dynamic range.
        eDDS,
        ePPM,
        eTIF,   ///< Any bit depth including float, and every channel count.
        eEXR,   ///< OpenEXR: float, high dynamic range, arbitrary channels.
        eKTX2,  ///< The only one that holds mips, array layers and block compression. @see SaveImageSet
    };

    /** @brief The extension @p format is written with, including the dot. */
    [[nodiscard]] KIMG_EXPORT_API std::string_view extensionFor(FileFormat format);

    /**
     * @brief Which part of an image to write.
     *
     * Defaults to the whole of mip 0 of layer 0 — the top-level picture, which is what a screenshot
     * or a texture dump means. Every field narrows it: one mip, one layer, one rectangle.
     */
    struct KIMG_EXPORT_API Subimage
    {
        glm::u32 mipLevel = 0;      ///< Which mip level. Its extent is the image's, halved that many times.
        glm::u32 arrayLayer = 0;    ///< Which array layer, or which cube face: +X, -X, +Y, -Y, +Z, -Z.
        glm::uvec3 offset { 0, 0, 0 };   ///< Where in the level to start. Must land on a block boundary for a compressed format.
        /// How much of it to take, in texels. Zero in any component means "the rest of the level",
        /// so the default is the whole thing.
        glm::uvec3 extent { 0, 0, 0 };
    };

    /**
     * @brief Writes one subimage of an image to disk.
     * @param directory Where to write. Created if it does not exist. An output path is not a lookup,
     *        so it is never resolved against the asset search roots.
     * @param name File name, without the extension.
     * @param format Which container to write; it decides the extension.
     * @param image The image to read from. Must have been created with kor::Image::Usage::eTransferSrc.
     * @param subimage Which part of it. @see Subimage
     * @return The path written, or why it could not be.
     *
     * Blocks until the image has been read back off the GPU and encoded.
     */
    [[nodiscard]] KIMG_EXPORT_API kor::Result<std::filesystem::path> SaveImage(
        const std::filesystem::path& directory, const std::string& name, FileFormat format,
        kor::ResourceRef<const kor::Image> image, const Subimage& subimage = {});

    /** @brief Writes one subimage without blocking the frame. @see SaveImage */
    [[nodiscard]] KIMG_EXPORT_API kor::Task<kor::Result<std::filesystem::path>> SaveImageAsync(
        std::filesystem::path directory, std::string name, FileFormat format,
        kor::ResourceRef<const kor::Image> image, Subimage subimage = {});

    /**
     * @brief Writes an entire image — every mip of every layer — as a KTX2.
     * @param directory Where to write. Created if it does not exist.
     * @param name File name, without the extension; `.ktx2` is added.
     * @param image The image to read from. Must have been created with kor::Image::Usage::eTransferSrc.
     * @return The path written, or why it could not be.
     *
     * KTX2 is the only container here that can hold all of it: a cubemap's six faces, a mip chain, an
     * array, and block-compressed data as the blocks it already is. This is how a texture makes the
     * round trip out of the engine and back in through kimg::LoadImage unchanged.
     */
    [[nodiscard]] KIMG_EXPORT_API kor::Result<std::filesystem::path> SaveImageSet(
        const std::filesystem::path& directory, const std::string& name,
        kor::ResourceRef<const kor::Image> image);

    /** @brief Writes an entire image as a KTX2 without blocking the frame. @see SaveImageSet */
    [[nodiscard]] KIMG_EXPORT_API kor::Task<kor::Result<std::filesystem::path>> SaveImageSetAsync(
        std::filesystem::path directory, std::string name, kor::ResourceRef<const kor::Image> image);
}
