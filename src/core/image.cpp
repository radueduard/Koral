//
// Created by radue on 2/18/2026.
//

module;

#include <stdexcept>

module gfx.image;
import vk.image;
import ogl.image;
import gfx.context;
import gfx.window;

namespace gfx
{
    gfx::Resource<Image> Image::Builder::build() const
    {
        switch (Context::Window().getAPI()) {
            case API::eOpenGL:
            return gfx::MakeResource<ogl::Image>(*this);
            case API::eVulkan:
            return gfx::MakeResource<vk::Image>(*this);
        default:
            throw std::runtime_error("Unknown graphics API!");
        }
    }

    glm::u32 Image::ChannelSizeFromImageFormat(const gfx::Image::Format format)
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

    glm::u32 Image::ChannelCountFromImageFormat(const gfx::Image::Format format)
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
        _sampleCount(createInfo.sampleCount),
        _usage(createInfo.usage) {
        if (_mipLevels == 0) {
            _mipLevels = 1 + static_cast<glm::u32>(std::floor(std::log2(std::max(_extent.x, std::max(_extent.y, _extent.z)))));
        }
    }

    void Image::ReadPixel(const glm::uvec3 &coord, void *outData, const glm::u32 dataSize) const {
        const auto stagingBuffer = gfx::Buffer::RawBuilder()
            .setRawSize(dataSize)
            .setUsage(Buffer::Usage::eTransferDst)
            .setType(Buffer::Type::eStaging)
            .build();
        CommandBuffer::SingleTimeCommand([&](gfx::CommandBuffer& commandBuffer) {
            commandBuffer.CopyImageToBuffer(ResourceRef<const gfx::Image>(this), stagingBuffer, Copy {
                .bufferOffset = 0,
                .bufferRowLength = 0,
                .bufferImageHeight = 0,
                .imageOffset = { static_cast<int32_t>(coord.x), static_cast<int32_t>(coord.y), static_cast<int32_t>(coord.z) },
                .imageExtent = { 1, 1, 1 },
                .imageBaseArrayLayer = 0,
                .imageLayerCount = 1,
                .imageMipLevel = 0
            });
        });
        const auto bytes = stagingBuffer->Read<std::byte>(dataSize, 0);
        std::memcpy(outData, bytes.data(), dataSize);
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
} // gfx