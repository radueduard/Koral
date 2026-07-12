//
// Created by radue on 2/18/2026.
//

#include <image.h>
#include <buffer.h>
#include <commandBuffer.h>
#include <framebuffer.h>
#include <surface.h>

#include "../backends/open_gl/image.h"
#include "../backends/vulkan/image.h"
#include "../../include/window.h"

#include "context.h"

namespace kor
{
    kor::Result<std::unique_ptr<Image>> Image::Builder::create() const
    {
        beginAttempt();

        if (auto v = validate(); !v) return std::unexpected(v.error());

        const auto api = Context::activeAPI();
        if (api != API::eOpenGL && api != API::eVulkan)
            return fail(ErrorCode::eUnknownApi, "Unknown graphics API!");

        // Construct and (optionally) upload inside guard(): any backend exception becomes a
        // kor::Error, and a staging-buffer failure is re-thrown with its own cause attached.
        return guard(ErrorCode::eBackend, [&]() -> std::unique_ptr<Image> {
        // The object, not a Resource: materialize() builds the owning Resource around it. The
        // upload below needs a ResourceRef, so it takes an unsafe (untracked) one — sound here
        // because the image cannot outlive this scope before we hand it over.
        std::unique_ptr<Image> image = (api == API::eVulkan)
            ? kor::MakeBackendPtr<Image, vk::Image>(*this)
            : kor::MakeBackendPtr<Image, ogl::Image>(*this);

        const auto imageRef = ResourceRef<const Image>(image.get());

        // Upload initial pixel data, if any was supplied via setData(). Uses the same
        // staging-buffer + copy path as the importer, so it works on both backends.
        if (!data.empty()) {
            const auto staging = kor::Buffer::Builder<std::byte>()
                .setDataView(std::span<const std::byte>(data))
                .setUsage(kor::Buffer::Usage::eTransferSrc)
                .setType(kor::Buffer::Type::eStaging)
                .build();

            // The staging buffer is an internal detail of the upload, so its failure is *our*
            // failure — rethrow it so guard() turns it back into our error, with the allocation
            // failure kept as the cause the user actually needs to see.
            if (!staging.valid()) {
                throw BackendException(causedBy(
                    Error{ .code = ErrorCode::eBackend, .message = "Could not stage the image's initial pixel data." },
                    staging.errorPtr()));
            }

            CommandBuffer::SingleTimeCommand([&](CommandBuffer& commandBuffer) {
                commandBuffer.CopyBufferToImage(staging, imageRef, kor::Copy {
                    .imageBaseArrayLayer = 0,
                    .imageLayerCount = image->getArrayLayers(),
                    .imageMipLevel = 0,
                });
            });

            if (image->getMipLevels() > 1) {
                CommandBuffer::SingleTimeCommand([&](CommandBuffer& commandBuffer) {
                    commandBuffer.GenerateMipmaps(imageRef);
                });
            }

            // Leave the image shader-readable: the copy/mip commands leave it in a
            // transfer-destination state, but descriptors bind sampled images as read-only.
            CommandBuffer::SingleTimeCommand([&](CommandBuffer& commandBuffer) {
                commandBuffer.Barrier({}, {{ imageRef, ResourceAccess::AllShaderRead }});
            });
        }

        return image;
        });
    }


    kor::Resource<Image> Image::Builder::build() const
    {
        return materialize<Image>(*this, "Image");
    }

    glm::u32 Image::ChannelSizeFromImageFormat(const kor::Image::Format format)
    {
        switch (format)
        {
        case Format::eR8_UNORM:
        case Format::eR8_SNORM:
        case Format::eR8_UINT:
        case Format::eR8_SINT:
            return 1;
        case Format::eRG8_UNORM:
        case Format::eRG8_SNORM:
        case Format::eRG8_UINT:
        case Format::eRG8_SINT:
            return 1;
        case Format::eRGB8_UNORM:
        case Format::eRGB8_SNORM:
        case Format::eRGB8_UINT:
        case Format::eRGB8_SINT:
        case Format::eRGB8_SRGB:
            return 1;
        case Format::eRGBA8_UNORM:
        case Format::eRGBA8_SNORM:
        case Format::eRGBA8_UINT:
        case Format::eRGBA8_SINT:
        case Format::eRGBA8_SRGB:
            return 1;
        case Format::eR16_UNORM:
        case Format::eR16_SNORM:
        case Format::eR16_UINT:
        case Format::eR16_SINT:
        case Format::eR16_SFLOAT:
            return 2;
        case Format::eRG16_UNORM:
        case Format::eRG16_SNORM:
        case Format::eRG16_UINT:
        case Format::eRG16_SINT:
        case Format::eRG16_SFLOAT:
            return 2;
        case Format::eRGB16_UNORM:
        case Format::eRGB16_SNORM:
        case Format::eRGB16_UINT:
        case Format::eRGB16_SINT:
        case Format::eRGB16_SFLOAT:
            return 2;
        case Format::eRGBA16_UNORM:
        case Format::eRGBA16_SNORM:
        case Format::eRGBA16_UINT:
        case Format::eRGBA16_SINT:
        case Format::eRGBA16_SFLOAT:
            return 2;
        case Format::eR32_UINT:
        case Format::eR32_SINT:
        case Format::eR32_SFLOAT:
            return 4;
        case Format::eRG32_UINT:
        case Format::eRG32_SINT:
        case Format::eRG32_SFLOAT:
            return 4;
        case Format::eRGB32_UINT:
        case Format::eRGB32_SINT:
        case Format::eRGB32_SFLOAT:
            return 4;
        case Format::eRGBA32_UINT:
        case Format::eRGBA32_SINT:
        case Format::eRGBA32_SFLOAT:
            return 4;
        case Format::eD16_UNORM:
            return 2;
        case Format::eD24_UNORM_S8_UINT:
            return 4;
        case Format::eD32_SFLOAT:
            return 4;
        case Format::eD32_SFLOAT_S8_UINT:
            return 4;
        default: throw std::runtime_error("Unsupported image format for pixel size!");
        }
    }

    glm::u32 Image::ChannelCountFromImageFormat(const kor::Image::Format format)
    {
        switch (format)
        {
        case Format::eR8_UNORM:
        case Format::eR8_SNORM:
        case Format::eR8_UINT:
        case Format::eR8_SINT:
        case Format::eR16_UNORM:
        case Format::eR16_SNORM:
        case Format::eR16_UINT:
        case Format::eR16_SINT:
        case Format::eR16_SFLOAT:
        case Format::eR32_UINT:
        case Format::eR32_SINT:
        case Format::eR32_SFLOAT:
            return 1;
        case Format::eRG8_UNORM:
        case Format::eRG8_SNORM:
        case Format::eRG8_UINT:
        case Format::eRG8_SINT:
        case Format::eRG16_UNORM:
        case Format::eRG16_SNORM:
        case Format::eRG16_UINT:
        case Format::eRG16_SINT:
        case Format::eRG16_SFLOAT:
        case Format::eRG32_UINT:
        case Format::eRG32_SINT:
        case Format::eRG32_SFLOAT:
            return 2;
        case Format::eRGB8_UNORM:
        case Format::eRGB8_SNORM:
        case Format::eRGB8_UINT:
        case Format::eRGB8_SINT:
        case Format::eRGB8_SRGB:
        case Format::eRGB16_UNORM:
        case Format::eRGB16_SNORM:
        case Format::eRGB16_UINT:
        case Format::eRGB16_SINT:
        case Format::eRGB16_SFLOAT:
        case Format::eRGB32_UINT:
        case Format::eRGB32_SINT:
        case Format::eRGB32_SFLOAT:
            return 3;
        case Format::eRGBA8_UNORM:
        case Format::eRGBA8_SNORM:
        case Format::eRGBA8_UINT:
        case Format::eRGBA8_SINT:
        case Format::eRGBA8_SRGB:
        case Format::eRGBA16_UNORM:
        case Format::eRGBA16_SNORM:
        case Format::eRGBA16_UINT:
        case Format::eRGBA16_SINT:
        case Format::eRGBA16_SFLOAT:
        case Format::eRGBA32_UINT:
        case Format::eRGBA32_SINT:
        case Format::eRGBA32_SFLOAT:
            return 4;
        case Format::eD16_UNORM:
        case Format::eD32_SFLOAT:
            return 1;
        case Format::eD24_UNORM_S8_UINT:
        case Format::eD32_SFLOAT_S8_UINT:
            return 2;
        default: throw std::runtime_error("Unsupported image format for channel count!");
        }
    }

    Image::Image(const Builder& createInfo) :
        _isPerFrame(createInfo.isPerFrame),
        _type(createInfo.type),
        _format(createInfo.format),
        _extent(createInfo.extent),
        _mipLevels(createInfo.mipLevels),
        _arrayLayers(createInfo.arrayLayers),
        _msaa(createInfo.msaa),
        _usage(createInfo.usage) {
        if (_mipLevels == 0) {
            _mipLevels = 1 + static_cast<glm::u32>(std::floor(std::log2(std::max(_extent.x, std::max(_extent.y, _extent.z)))));
        }
    }

    bool IsDepthStencilFormat(const Image::Format format)
    {
        switch (format)
        {
        case Image::Format::eD16_UNORM:
        case Image::Format::eD24_UNORM_S8_UINT:
        case Image::Format::eD32_SFLOAT:
        case Image::Format::eD32_SFLOAT_S8_UINT:
            return true;
        default:
            return false;
        }
    }

    bool IsStencilFormat(Image::Format format)
    {
        switch (format)
        {
        case Image::Format::eD24_UNORM_S8_UINT:
        case Image::Format::eD32_SFLOAT_S8_UINT:
            return true;
        default:
            return false;
        }
    }
} // Koral