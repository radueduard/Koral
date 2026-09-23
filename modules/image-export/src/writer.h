//
// Created by radue on 30.07.2026.
//

// The parts both writers need: getting bytes off the GPU, and saying what a kor::Image::Format is in
// the vocabularies the two file libraries speak.

#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include <error.h>
#include <image.h>
#include <resource.h>

#include <koralImageExport.h>

namespace kimg::detail
{
    /** @brief One subimage, read off the GPU, with the shape it turned out to have. */
    struct ReadBack
    {
        std::vector<unsigned char> bytes;
        glm::uvec3 extent { 1, 1, 1 };      ///< The region actually read, in texels.
    };

    /** @brief Shorthand for the errors this module reports, all of which name the file. */
    kor::Error fileError(const std::filesystem::path& path, std::string what);

    /** @brief The extent of one mip level of @p image. */
    glm::uvec3 mipExtent(const kor::Image& image, glm::u32 mipLevel);

    /**
     * @brief Checks a subimage against the image it names part of.
     * @return The region it resolves to, or why it does not name one.
     *
     * Zero extents become "the rest of the level", a mip or layer past the end is refused by name,
     * and a compressed image is held to its block grid — a copy that starts mid-block is a copy the
     * driver would either reject or silently misplace.
     */
    std::expected<Subimage, kor::Error> resolveSubimage(const kor::Image& image, Subimage subimage);

    /**
     * @brief Copies one subimage out of an image into CPU memory.
     *
     * Main thread: it submits and waits on a single-time transfer. The size comes from
     * kor::Image::sizeOfRegion, so a block-compressed image is measured in blocks.
     */
    std::expected<ReadBack, kor::Error> readBack(const kor::ResourceRef<const kor::Image>& image,
                                                 const Subimage& subimage);

    /** @brief Writes bytes as an ordinary image file, through OpenImageIO. Background-safe. */
    std::expected<void, kor::Error> writeWithOiio(const std::filesystem::path& target,
                                                  const ReadBack& data, kor::Image::Format format);

    /** @brief Writes an entire image, mips and layers and all, as a KTX2. Background-safe. */
    std::expected<void, kor::Error> writeKtx2(const std::filesystem::path& target,
                                              const kor::Image& image,
                                              const std::vector<ReadBack>& slices);
}
