//
// Created by radue on 30.07.2026.
//

/**
 * @file koralImageCompress.h
 * @brief The image *compression* module: turning an ordinary image file into a GPU-ready KTX2.
 *
 * A PNG or JPEG is a *file* format — the GPU cannot sample it, so loading one means decoding it and
 * uploading four bytes a texel. A compressed KTX2 is a *texture* format: the blocks in the file are
 * the blocks in video memory, four to eight times smaller, and there is nothing to decode.
 *
 * @code
 * // CMakeLists.txt:  target_link_libraries(MyTool PRIVATE Koral::Koral Koral::koral-image-compress)
 *
 * auto written = kimg::CompressToKtx("textures/wood.png", "cooked");
 * // -> cooked/wood.ktx2, with a mip chain, ready to load anywhere
 * @endcode
 *
 * This is a *build-time* tool by nature: compressing a 4K texture takes seconds, not milliseconds.
 * Run it over your assets once and ship the result — which then loads through kimg::LoadImage with no
 * mention of this module at all.
 *
 * @section compress_universal Why the output is "universal"
 *
 * The default codec does not encode BC7 or ASTC directly; it encodes an intermediate — UASTC — that
 * *transcodes* to any of them in milliseconds when the file is loaded. One file therefore works on a
 * desktop that wants BC7, a phone that wants ASTC, and an old GL device that wants ETC2, without
 * shipping three. The import module does that transcode on the way in and picks the target from what
 * the device actually supports.
 *
 * Choose Codec::eASTC instead when you know the target hardware and want the file to be exactly what
 * it will be sampled as.
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include <glm/glm.hpp>

#include <error.h>
#include <resource.h>
#include <task.h>

#include <koralImageImport.h>

/**
 * @brief Marks what crosses out of the compression module's library.
 *
 * The module's own build defines KORAL_IMAGE_COMPRESS_EXPORTS to pick the export branch.
 */
#if defined(_WIN32)
#  if defined(KORAL_IMAGE_COMPRESS_EXPORTS)
#    define KIMG_COMPRESS_API __declspec(dllexport)
#  else
#    define KIMG_COMPRESS_API __declspec(dllimport)
#  endif
#else
#  define KIMG_COMPRESS_API __attribute__((visibility("default")))
#endif

namespace kimg
{
    /** @brief This module's identity, for another module that declares a dependency on it. */
    inline constexpr std::string_view kCompressModuleId = "koral.image.compress";
    inline constexpr std::uint32_t    kCompressModuleVersion = 1;

    /** @brief What to encode into. */
    enum class Codec
    {
        /**
         * @brief UASTC: high quality, transcodes to any block format. The default.
         *
         * 8 bits a texel in the file, and it becomes BC7 or ASTC 4x4 at full quality on load. Pair
         * with CompressOptions::supercompress, which shrinks the *file* without touching what the GPU
         * ends up with.
         */
        eUASTC,
        /**
         * @brief ETC1S: much smaller, visibly lossier, transcodes to any block format.
         *
         * A quarter to an eighth of UASTC's file size. For textures where size matters more than
         * fidelity — detail maps, UI atlases, anything seen small.
         */
        eETC1S,
        /**
         * @brief ASTC 4x4, encoded directly rather than transcoded on load.
         *
         * Choose it when the target hardware is known to support ASTC: the file is then exactly what
         * the GPU samples, with no transcode step at all. Desktop GPUs commonly do *not*.
         */
        eASTC,
    };

    /** @brief How to encode. The defaults are what you want for a colour texture. */
    struct KIMG_COMPRESS_API CompressOptions
    {
        Codec codec = Codec::eUASTC;    ///< What to encode into.

        /**
         * @brief How hard to work, 1..255. Higher is slower and better.
         *
         * Read by ETC1S as its quality level and by ASTC as its effort; UASTC has five discrete
         * levels and this is mapped onto them.
         */
        glm::u32 quality = 128;

        /**
         * @brief Whether to build a mip chain before encoding.
         *
         * Almost always yes: a compressed texture cannot have mips generated for it on the GPU (they
         * would have to be decoded, filtered and re-encoded), so this is where they come from. Each
         * level is a box filter of the one above.
         */
        bool generateMipmaps = true;

        /**
         * @brief Whether the source is sRGB-encoded, as an ordinary colour texture is.
         *
         * Decides the format recorded in the file, and so whether the hardware converts to linear on
         * every sample. Set false for data that is not colour: normal maps, masks, heightfields.
         */
        bool srgb = true;

        /**
         * @brief Whether this is a normal map.
         *
         * Turns off the perceptual weighting that assumes the channels are colour, which on a normal
         * map produces visible faceting.
         */
        bool normalMap = false;

        /**
         * @brief Whether to Zstd-compress the encoded blocks in the file.
         *
         * Shrinks the file — often by a third for UASTC — and costs a moment of decompression on
         * load. It does not change what reaches the GPU. Ignored by ETC1S, which has its own
         * entropy coding.
         */
        bool supercompress = true;
    };

    /**
     * @brief Compresses an image file into a KTX2.
     * @param source The image to read, resolved against the asset search roots like kimg::LoadImage.
     * @param outputDirectory Where to write. Created if it does not exist.
     * @param options How to encode. @see CompressOptions
     * @return The path written — the source's own name with a `.ktx2` extension — or why it could not be.
     *
     * Blocking, and deliberately: this is a tool, and the work is seconds of CPU. Use the async form
     * to run it from inside a running application.
     */
    [[nodiscard]] KIMG_COMPRESS_API kor::Result<std::filesystem::path> CompressToKtx(
        const std::filesystem::path& source, const std::filesystem::path& outputDirectory,
        const CompressOptions& options = {});

    /** @brief Compresses an image file into a KTX2 off the render thread. @see CompressToKtx */
    [[nodiscard]] KIMG_COMPRESS_API kor::Task<kor::Result<std::filesystem::path>> CompressToKtxAsync(
        std::filesystem::path source, std::filesystem::path outputDirectory,
        CompressOptions options = {});

    /**
     * @brief Compresses pixels already in memory into a KTX2.
     * @param image The pixels. Must be an 8-bit format — the encoders take colour, not HDR.
     * @param outputDirectory Where to write. Created if it does not exist.
     * @param name File name, without the extension.
     * @param options How to encode.
     * @return The path written, or why it could not be.
     *
     * What the two above do once the source is decoded, exposed because pixels do not have to come
     * from a file — one rendered, generated, or read back off the GPU compresses the same way.
     */
    [[nodiscard]] KIMG_COMPRESS_API kor::Result<std::filesystem::path> CompressToKtx(
        const CpuImage& image, const std::filesystem::path& outputDirectory,
        const std::string& name, const CompressOptions& options = {});
}
