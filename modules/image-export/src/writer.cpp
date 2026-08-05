//
// Created by radue on 30.07.2026.
//

// Reading a subimage back off the GPU, and writing it as an ordinary image file. The KTX2 half lives
// in ktxWriter.cpp; what they share is in writer.h.

#include "writer.h"

#include <algorithm>
#include <cstring>
#include <format>

#include <OpenImageIO/imageio.h>

#include <buffer.h>
#include <commandBuffer.h>
#include <context.h>
#include <log.h>

namespace kimg
{
    namespace detail
    {
        kor::Error fileError(const std::filesystem::path& path, std::string what)
        {
            return kor::Error{ .code = kor::ErrorCode::eInvalidArgument,
                               .message = std::format("{}: {}", path.string(), std::move(what)) };
        }

        glm::uvec3 mipExtent(const kor::Image& image, const glm::u32 mipLevel)
        {
            const auto base = image.getExtent();
            return { std::max(1u, base.x >> mipLevel),
                     std::max(1u, base.y >> mipLevel),
                     std::max(1u, base.z >> mipLevel) };
        }

        std::expected<Subimage, kor::Error> resolveSubimage(const kor::Image& image, Subimage subimage)
        {
            const auto complaint = [](std::string what) {
                return std::unexpected(kor::Error{ .code = kor::ErrorCode::eImageSubresourceOutOfRange,
                                                   .message = std::move(what) });
            };

            if (subimage.mipLevel >= image.getMipLevels())
                return complaint(std::format("mip level {} does not exist; the image has {}",
                                             subimage.mipLevel, image.getMipLevels()));
            if (subimage.arrayLayer >= image.getArrayLayers())
                return complaint(std::format("array layer {} does not exist; the image has {}",
                                             subimage.arrayLayer, image.getArrayLayers()));

            const auto level = mipExtent(image, subimage.mipLevel);

            // Zero means "the rest of the level", so the default Subimage is the whole picture.
            if (subimage.extent.x == 0) subimage.extent.x = level.x - std::min(subimage.offset.x, level.x);
            if (subimage.extent.y == 0) subimage.extent.y = level.y - std::min(subimage.offset.y, level.y);
            if (subimage.extent.z == 0) subimage.extent.z = level.z - std::min(subimage.offset.z, level.z);

            if (subimage.offset.x + subimage.extent.x > level.x ||
                subimage.offset.y + subimage.extent.y > level.y ||
                subimage.offset.z + subimage.extent.z > level.z) {
                return complaint(std::format(
                    "the region {}x{}x{} at ({}, {}, {}) does not fit mip level {}, which is {}x{}x{}",
                    subimage.extent.x, subimage.extent.y, subimage.extent.z,
                    subimage.offset.x, subimage.offset.y, subimage.offset.z,
                    subimage.mipLevel, level.x, level.y, level.z));
            }

            // A compressed image is addressed in blocks. An offset inside one, or an extent that ends
            // inside one, is not something a copy can express — except at the edge of the level, where
            // a partial block is the level itself.
            if (kor::Image::IsBlockCompressed(image.getFormat())) {
                const auto block = kor::Image::BlockExtentFromImageFormat(image.getFormat());
                if (subimage.offset.x % block.x != 0 || subimage.offset.y % block.y != 0) {
                    return complaint(std::format(
                        "a {}x{}-block format can only be read from a block boundary; ({}, {}) is not one",
                        block.x, block.y, subimage.offset.x, subimage.offset.y));
                }
                const bool endsAtEdge = subimage.offset.x + subimage.extent.x == level.x
                                     && subimage.offset.y + subimage.extent.y == level.y;
                if (!endsAtEdge && (subimage.extent.x % block.x != 0 || subimage.extent.y % block.y != 0)) {
                    return complaint(std::format(
                        "a {}x{}-block format can only be read in whole blocks; {}x{} is not a whole number of them",
                        block.x, block.y, subimage.extent.x, subimage.extent.y));
                }
            }

            return subimage;
        }

        std::expected<ReadBack, kor::Error> readBack(const kor::ResourceRef<const kor::Image>& image,
                                                    const Subimage& subimage)
        {
            const auto byteCount = kor::Image::SizeOfRegion(image->getFormat(), subimage.extent);

            kor::Buffer::RawBuilder builder;
            builder.setRawSize(static_cast<glm::i64>(byteCount))
                .addUsage(kor::Buffer::Usage::eTransferDst)
                .setType(kor::Buffer::Type::eReadback);
            auto staging = builder.build();
            if (!staging) {
                return std::unexpected(staging.error()
                    ? *staging.error()
                    : kor::Error{ .code = kor::ErrorCode::eBackend, .message = "read-back buffer could not be allocated" });
            }

            // SingleTimeCommand begins, submits and fences the copy, unlike a bare Run() — which
            // would leave the buffer empty and the file full of zeroes.
            kor::CommandBuffer::SingleTimeCommand([&](kor::CommandBuffer& commandBuffer) {
                commandBuffer.CopyImageToBuffer(image, kor::ResourceRef<const kor::Buffer>(staging), kor::Copy {
                    .imageOffset = subimage.offset,
                    .imageExtent = subimage.extent,
                    .imageBaseArrayLayer = subimage.arrayLayer,
                    .imageLayerCount = 1,
                    .imageMipLevel = subimage.mipLevel,
                });
            }, kor::CommandBuffer::Usage::eTransfer);

            ReadBack data;
            data.extent = subimage.extent;
            data.bytes = staging->Read<unsigned char>(staging->getSize());
            if (data.bytes.size() < byteCount) {
                return std::unexpected(kor::Error{ .code = kor::ErrorCode::eBackend,
                    .message = std::format("the GPU returned {} bytes for a {}-byte region",
                                           data.bytes.size(), byteCount) });
            }
            return data;
        }

        namespace
        {
            /**
             * @brief How OpenImageIO should be told to interpret an image's texels.
             *
             * Taken from the format rather than assumed: a float image written as 8-bit would lose
             * three quarters of every texel, and the read-back that fed it would be sized for the
             * wrong type.
             */
            OIIO::TypeDesc oiioTypeFor(const kor::Image::Format format)
            {
                using F = kor::Image::Format;
                switch (format) {
                case F::eR16_SFLOAT: case F::eRG16_SFLOAT: case F::eRGB16_SFLOAT: case F::eRGBA16_SFLOAT:
                    return OIIO::TypeDesc::HALF;
                case F::eR32_SFLOAT: case F::eRG32_SFLOAT: case F::eRGB32_SFLOAT: case F::eRGBA32_SFLOAT:
                case F::eD32_SFLOAT:
                    return OIIO::TypeDesc::FLOAT;
                case F::eR32_UINT: case F::eRG32_UINT: case F::eRGB32_UINT: case F::eRGBA32_UINT:
                    return OIIO::TypeDesc::UINT32;
                case F::eR32_SINT: case F::eRG32_SINT: case F::eRGB32_SINT: case F::eRGBA32_SINT:
                    return OIIO::TypeDesc::INT32;
                case F::eR16_UNORM: case F::eRG16_UNORM: case F::eRGB16_UNORM: case F::eRGBA16_UNORM:
                case F::eD16_UNORM:
                    return OIIO::TypeDesc::UINT16;
                case F::eR16_SINT: case F::eRG16_SINT: case F::eRGB16_SINT: case F::eRGBA16_SINT:
                    return OIIO::TypeDesc::INT16;
                case F::eR8_SINT: case F::eRG8_SINT: case F::eRGB8_SINT: case F::eRGBA8_SINT:
                case F::eR8_SNORM: case F::eRG8_SNORM: case F::eRGB8_SNORM: case F::eRGBA8_SNORM:
                    return OIIO::TypeDesc::INT8;
                default:
                    // Every 8-bit unsigned format.
                    return OIIO::TypeDesc::UINT8;
                }
            }
        }

        std::expected<void, kor::Error> writeWithOiio(const std::filesystem::path& target,
                                                     const ReadBack& data, const kor::Image::Format format)
        {
            const auto output = OIIO::ImageOutput::create(target.string());
            if (!output) return std::unexpected(fileError(target, "no writer for this container (" + OIIO::geterror() + ")"));

            const auto type = oiioTypeFor(format);
            const auto sourceChannels = static_cast<int>(kor::Image::ChannelCountFromImageFormat(format));
            const auto channelSize = static_cast<std::size_t>(kor::Image::ChannelSizeFromImageFormat(format));

            // Not every container has an alpha channel — Radiance .hdr is RGBE and holds exactly
            // three — and one that has not will refuse a four-channel spec outright rather than drop
            // it. So ask, and drop the alpha here when the answer is no.
            const bool keepAlpha = sourceChannels < 4 || output->supports("alpha");
            const int channels = keepAlpha ? sourceChannels : 3;

            const OIIO::ImageSpec spec(static_cast<int>(data.extent.x), static_cast<int>(data.extent.y),
                                       channels, type);
            if (!output->open(target.string(), spec))
                return std::unexpected(fileError(target, "could not be opened for writing (" + OIIO::geterror() + ")"));

            std::vector<unsigned char> bytes = data.bytes;
            if (!keepAlpha) {
                // Compact RGBA down to RGB in place, one texel at a time — the destination always
                // trails the source, so there is nothing to copy around.
                const auto texels = static_cast<std::size_t>(data.extent.x) * data.extent.y * data.extent.z;
                for (std::size_t t = 0; t < texels; ++t) {
                    std::memmove(bytes.data() + t * 3 * channelSize,
                                 bytes.data() + t * static_cast<std::size_t>(sourceChannels) * channelSize,
                                 3 * channelSize);
                }
                bytes.resize(texels * 3 * channelSize);
            }

            if (!output->write_image(type, bytes.data()))
                return std::unexpected(fileError(target, "could not be written (" + OIIO::geterror() + ")"));

            output->close();
            return {};
        }
    }

    std::string_view extensionFor(const FileFormat format)
    {
        switch (format) {
        case FileFormat::ePNG: return ".png";
        case FileFormat::eJPG: return ".jpg";
        case FileFormat::eBMP: return ".bmp";
        case FileFormat::eTGA: return ".tga";
        case FileFormat::eHDR: return ".hdr";
        case FileFormat::eDDS: return ".dds";
        case FileFormat::ePPM: return ".ppm";
        case FileFormat::eTIF: return ".tif";
        case FileFormat::eEXR: return ".exr";
        case FileFormat::eKTX2: return ".ktx2";
        }
        return ".png";
    }

    namespace
    {
        /** @brief What every save does first: check the arguments and settle on a target path. */
        struct Prepared
        {
            std::filesystem::path target;
            Subimage subimage;
        };

        kor::Result<Prepared> prepare(const std::filesystem::path& directory, const std::string& name,
                                      const FileFormat format, const kor::ResourceRef<const kor::Image>& image,
                                      const Subimage& subimage)
        {
            const auto target = directory / (name + std::string(extensionFor(format)));

            if (!image) {
                return std::unexpected(detail::fileError(target, "cannot be written from an unusable image"));
            }
            if (kor::Image::IsBlockCompressed(image->getFormat()) && format != FileFormat::eKTX2) {
                // Decoding a block to invent something a PNG could hold is a decision this module has
                // no business making silently. KTX2 takes the blocks as they are.
                return std::unexpected(detail::fileError(target,
                    "a block-compressed image can only be written as KTX2; decode it first, or use SaveImageSet"));
            }

            auto resolved = detail::resolveSubimage(*image, subimage);
            if (!resolved) return std::unexpected(detail::fileError(target, std::string(resolved.error().message)));

            std::error_code ec;
            std::filesystem::create_directories(directory, ec);

            return Prepared{ target, *resolved };
        }
    }

    kor::Result<std::filesystem::path> SaveImage(
        const std::filesystem::path& directory, const std::string& name, const FileFormat format,
        kor::ResourceRef<const kor::Image> image, const Subimage& subimage)
    {
        auto prepared = prepare(directory, name, format, image, subimage);
        if (!prepared) return std::unexpected(prepared.error());

        // KTX2 of a single subimage is still a KTX2 — one level, one layer — and goes through the
        // writer that can express that.
        if (format == FileFormat::eKTX2) {
            auto data = detail::readBack(image, prepared->subimage);
            if (!data) return std::unexpected(detail::fileError(prepared->target, std::string(data.error().message)));

            std::vector<detail::ReadBack> slices;
            slices.push_back(std::move(*data));
            if (auto written = detail::writeKtx2(prepared->target, *image, slices); !written)
                return std::unexpected(written.error());
            return prepared->target;
        }

        auto data = detail::readBack(image, prepared->subimage);
        if (!data) return std::unexpected(detail::fileError(prepared->target, std::string(data.error().message)));

        if (auto written = detail::writeWithOiio(prepared->target, *data, image->getFormat()); !written)
            return std::unexpected(written.error());
        return prepared->target;
    }

    kor::Task<kor::Result<std::filesystem::path>> SaveImageAsync(
        std::filesystem::path directory, std::string name, const FileFormat format,
        kor::ResourceRef<const kor::Image> image, Subimage subimage)
    {
        // The read-back is the main thread's — it submits GPU work — and the encode is not, which is
        // the half worth moving: a 4K PNG costs far more to deflate than to copy.
        auto prepared = prepare(directory, name, format, image, subimage);
        if (!prepared) co_return std::unexpected(prepared.error());

        auto data = detail::readBack(image, prepared->subimage);
        if (!data) co_return std::unexpected(detail::fileError(prepared->target, std::string(data.error().message)));

        const auto imageFormat = image->getFormat();
        const auto target = prepared->target;

        co_await kor::Context::SwitchToBackgroundThread();
        std::expected<void, kor::Error> written;
        if (format == FileFormat::eKTX2) {
            std::vector<detail::ReadBack> slices;
            slices.push_back(std::move(*data));
            written = detail::writeKtx2(target, *image, slices);
        } else {
            written = detail::writeWithOiio(target, *data, imageFormat);
        }
        co_await kor::Context::SwitchToMainThread();

        if (!written) co_return std::unexpected(written.error());
        co_return target;
    }
}
