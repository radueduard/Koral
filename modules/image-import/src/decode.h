//
// Created by radue on 29.07.2026.
//

// The file-format half of the module, kept apart from the GPU half: what OpenImageIO and KTX say a
// file holds, translated into what kor::Image understands. Decoding touches no device, so all of it
// is safe to run on a background thread; the upload helpers at the bottom are the main thread's.

#pragma once

#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

#include <glm/glm.hpp>

#include <OpenImageIO/typedesc.h>

#include <error.h>
#include <image.h>

#include <koralImageImport.h>

// OIIO is a *versioned* namespace behind an alias, so its types cannot be forward-declared —
// declaring `namespace OIIO` conflicts with the alias. Hence the include; both users of this
// header pull OpenImageIO in anyway.

namespace kimg::detail
{
    /**
     * @brief Decodes the first subimage of an already-resolved path into CPU memory.
     * @return The pixels, or why they could not be read. Three-channel sources are widened to four
     *         with an opaque alpha, because no GPU format has three channels.
     */
    std::expected<CpuImage, kor::Error> decodeFile(const std::filesystem::path& path);

    /** @brief The kor::Image::Format an OpenImageIO type and channel count map onto. */
    std::expected<kor::Image::Format, kor::Error> formatFrom(const OIIO::TypeDesc& type, int channels);

    /** @brief Shorthand for the errors this module reports, all of which name the file. */
    kor::Error fileError(const std::filesystem::path& path, std::string what);

    /** @brief A texture nobody can use, carrying the reason, logged on the way out. */
    kor::Resource<kor::Image> poisoned(const std::filesystem::path& path, std::string what);

    // ---- upload helpers, shared by every loader in the module -----------------------------------

    /** @brief Uploads one CPU-side buffer into one mip level of one array layer. */
    void uploadSlice(const kor::ResourceRef<const kor::Image>& image, std::span<const unsigned char> bytes,
                     glm::uvec3 extent, glm::u32 layer, glm::u32 mip);

    /**
     * @brief Finishes a freshly uploaded texture: optional mips, then a shader-readable layout.
     *
     * The copies leave it in eTransferDstOptimal, but descriptors bind sampled images as
     * eShaderReadOnlyOptimal. The single-time submit completes before any frame renders, so no later
     * barrier is needed. Mip generation is skipped for a block-compressed image, which cannot be
     * blitted into — it carries the chain it was encoded with.
     */
    void finishUpload(const kor::ResourceRef<const kor::Image>& image, bool generateMipmaps);

    // ---- KTX ------------------------------------------------------------------------------------

    /**
     * @brief A KTX/KTX2 file read into memory, and the plan for getting it onto the GPU.
     *
     * Split out of the loaders so the blocking and the coroutine paths share one implementation
     * rather than two that drift: reading is background-safe, and everything the GPU needs is worked
     * out before the main thread is asked for anything.
     */
    struct KtxImage
    {
        /** @brief One mip of one layer, as a range of the file's pixel data. */
        struct Slice { glm::u32 mip; glm::u32 layer; std::size_t offset; std::size_t size; };

        std::shared_ptr<void> texture;          ///< The ktxTexture, kept opaque so this header stays free of ktx.h.
        const unsigned char* data = nullptr;    ///< Its pixel data, owned by `texture`.
        kor::Image::Format format = kor::Image::Format::eRGBA8_UNORM;
        kor::Image::Type type = kor::Image::Type::e2D;
        glm::uvec3 extent { 1, 1, 1 };
        glm::u32 arrayLayers = 1;
        glm::u32 fileMipLevels = 1;             ///< How many levels the file carries; 1 means it has none.
        std::vector<Slice> slices;              ///< One per mip per layer, in upload order.
    };

    /** @brief Whether this path names a KTX container, and so wants the loader below. */
    bool isKtx(const std::filesystem::path& path);

    /**
     * @brief Reads a KTX/KTX2 file, transcoding it when it holds a universal texture.
     *
     * A Basis-compressed KTX2 carries no GPU format of its own; it is transcoded here into the best
     * block format the device supports.
     */
    std::expected<KtxImage, kor::Error> readKtx(const std::filesystem::path& path);

    /** @brief Creates the GPU image a KtxImage describes. Main thread. */
    kor::Resource<kor::Image> makeKtxImage(const KtxImage& ktx, bool generateMipmaps);
}
