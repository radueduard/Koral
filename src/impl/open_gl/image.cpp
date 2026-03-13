//
// Created by radue on 2/18/2026.
//

#include "image.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <magic_enum/magic_enum.hpp>

#include "buffer.h"
#include "utils/ogl_err_handling.h"


namespace gfx::ogl
{
    GLenum GetTargetFromImageType(const gfx::Image::Type type, const gfx::MSAA msaa, const glm::u32 arrayLayers)
    {
        switch (type) {
        case gfx::Image::Type::e1D:
            return arrayLayers == 1 ? GL_TEXTURE_1D : GL_TEXTURE_1D_ARRAY;
        case gfx::Image::Type::e2D:
            if (arrayLayers == 1) {
                return msaa == gfx::MSAA::eNone ? GL_TEXTURE_2D : GL_TEXTURE_2D_MULTISAMPLE;
            } else {
                return msaa == gfx::MSAA::eNone ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D_MULTISAMPLE_ARRAY;
            }
        case gfx::Image::Type::e3D:
            return GL_TEXTURE_3D;
        default:
            throw std::runtime_error("Unsupported image type!");
        }
    }

    Image::Image(const gfx::Image::Builder& createInfo) : gfx::Image(createInfo)
    {
        if (createInfo.msaa != MSAA::eNone && createInfo.type != Type::e2D) {
            std::cerr << "Error: Multisampled images are only supported for 2D images! Attempting to create a multisampled image with type " << magic_enum::enum_name(createInfo.type) << std::endl;
        }

        if (createInfo.arrayLayers > 1 && createInfo.type == Type::e3D) {
            std::cerr << "Error: Multisampled images are not supported!" << std::endl;
        }

        if (IsDepthStencilFormat(createInfo.format) && createInfo.type != Type::e2D) {
            std::cerr << "Error: Depth/stencil formats are only supported for 2D images! Attempting to create a depth/stencil image with type " << magic_enum::enum_name(createInfo.type) << std::endl;
        }


        // Determine the appropriate OpenGL texture target based on the image type and MSAA settings
        GLenum target;
        switch (createInfo.type) {
        case Type::e1D:
            target = createInfo.arrayLayers == 1 ? GL_TEXTURE_1D : GL_TEXTURE_1D_ARRAY;
            break;
        case Type::e2D:
            target = createInfo.arrayLayers == 1
                ? (createInfo.msaa == MSAA::eNone ? GL_TEXTURE_2D : GL_TEXTURE_2D_MULTISAMPLE)
                : (createInfo.msaa == MSAA::eNone ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D_MULTISAMPLE_ARRAY);
            break;
        case Type::e3D:
            target = GL_TEXTURE_3D;
            break;
        default:
            throw std::runtime_error("Unsupported image type!");
        }
        glCreateTextures(target, 1, &_id);
        glCheckError();

        // Bind the texture to the appropriate target
        glBindTexture(target, _id);

        // check for errors
        if (glCheckError()) {
            glDeleteTextures(1, &_id);
        }

        // Set image format
        const GLenum internalFormat = InternalFormatFromImageFormat(createInfo.format);

        if (createInfo.type == gfx::Image::Type::e1D && createInfo.arrayLayers == 1) {
            glTexStorage1D(target, createInfo.mipLevels, internalFormat, createInfo.extent.x);
        } else if (createInfo.type == gfx::Image::Type::e1D && createInfo.arrayLayers > 1) {
            glTexStorage2D(target, createInfo.mipLevels, internalFormat, createInfo.extent.x, createInfo.arrayLayers);
        } else if (createInfo.type == gfx::Image::Type::e2D && createInfo.arrayLayers == 1) {
            if (createInfo.msaa == gfx::MSAA::eNone) {
                glTexStorage2D(target, createInfo.mipLevels, internalFormat, createInfo.extent.x, createInfo.extent.y);
            } else {
                glTexStorage2DMultisample(target, static_cast<GLsizei>(createInfo.msaa), internalFormat, createInfo.extent.x, createInfo.extent.y, GL_TRUE);
            }
        } else if (createInfo.type == gfx::Image::Type::e2D && createInfo.arrayLayers > 1) {
            if (createInfo.msaa == gfx::MSAA::eNone) {
                glTexStorage3D(target, createInfo.mipLevels, internalFormat, createInfo.extent.x, createInfo.extent.y, createInfo.arrayLayers);
            } else {
                glTexStorage3DMultisample(target, static_cast<GLsizei>(createInfo.msaa), internalFormat, createInfo.extent.x, createInfo.extent.y, createInfo.arrayLayers, GL_TRUE);
            }
        } else if (createInfo.type == gfx::Image::Type::e3D) {
            glTexStorage3D(target, createInfo.mipLevels, internalFormat, createInfo.extent.x, createInfo.extent.y, createInfo.extent.z);
        }

        // check for errors
        if (glCheckError()) {
            glDeleteTextures(1, &_id);
        }
    }

    Image::~Image()
    {
        glDeleteTextures(1, &_id);
    }

    std::vector<std::byte> Image::ReadData(glm::u32 mipLevel, glm::u32 arrayLayer) const
    {
        if (!(_usage & Usage::eTransferSrc)) {
            throw std::runtime_error("Attempting to read data from an image that does not have the TransferSrc usage flag set!");
        }

        // Determine the appropriate OpenGL texture target based on the image type and MSAA settings
        const GLenum target = GetTargetFromImageType(_type, _msaa, _arrayLayers);
        glBindTexture(target, _id);

        // Create a PBO
        GLuint pbo;
        glGenBuffers(1, &pbo);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);

        const GLenum baseFormat = BaseFormatFromImageFormat(_format);
        const GLenum dataType = DataTypeFromImageFormat(_format);
        const GLsizei width = static_cast<GLsizei>(_extent.x >> mipLevel);
        const GLsizei height = static_cast<GLsizei>(_extent.y >> mipLevel);
        const GLsizei depth = static_cast<GLsizei>(_extent.z >> mipLevel);
        const GLsizei layerCount = _arrayLayers > 1 ? static_cast<GLsizei>(_arrayLayers) : 1;
        const GLsizei pixelSize = PixelSizeFromImageFormat(_format);
        const GLsizei channelCount = ChannelCountFromImageFormat(_format);
        const GLsizei imageSize = width * height * depth * layerCount * pixelSize * channelCount;

        glBufferStorage(GL_PIXEL_PACK_BUFFER, imageSize, nullptr, GL_MAP_READ_BIT);
        glCheckError();

        glGetTexImage(target, mipLevel, baseFormat, dataType, nullptr);
        glCheckError();

        // Map the PBO and read the data
        const void* mappedData = glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
        glCheckError();
        if (!mappedData)
        {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            glDeleteBuffers(1, &pbo);
            throw std::runtime_error("Failed to map pixel buffer object for reading image data!");
        }

        std::vector<std::byte> data(imageSize);
        std::memcpy(data.data(), mappedData, imageSize);

        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        glCheckError();

        // Clean up
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        glDeleteBuffers(1, &pbo);
        return data;
    }

    void Image::CopyFrom(const gfx::Buffer& buffer, const uint32_t mipLevel, const uint32_t layer) const
    {
        if (!(_usage & Usage::eTransferDst)) {
            throw std::runtime_error("Attempting to copy data to an image that does not have the TransferDst usage flag set!");
        }
        if (_mipLevels <= mipLevel) {
            throw std::runtime_error("Attempting to copy data to a mip level that does not exist!");
        }
        if (_arrayLayers <= layer) {
            throw std::runtime_error("Attempting to copy data to an array layer that does not exist!");
        }

        const auto& oglBuffer = dynamic_cast<const Buffer&>(buffer);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, *oglBuffer);
        glCheckError();

        const GLenum target = GetTargetFromImageType(_type, _msaa, _arrayLayers);
        const GLenum baseFormat = BaseFormatFromImageFormat(_format);
        const GLenum dataType = DataTypeFromImageFormat(_format);
        const auto width = static_cast<GLsizei>(_extent.x);
        const auto height = static_cast<GLsizei>(_extent.y);
        const auto depth = static_cast<GLsizei>(_extent.z);

        if (_type == Type::e1D && _arrayLayers == 1) {
            glTexSubImage1D(target, static_cast<glm::i32>(mipLevel), 0, width, baseFormat, dataType, nullptr);
        } else if (_type == Type::e1D && _arrayLayers > 1) {
            glTexSubImage2D(target, static_cast<glm::i32>(mipLevel), 0, static_cast<glm::i32>(layer) * width, width, 1, baseFormat, dataType, nullptr);
        } else if (_type == Type::e2D && _arrayLayers == 1) {
            glTexSubImage2D(target, static_cast<glm::i32>(mipLevel), 0, 0, width, height, baseFormat, dataType, nullptr);
        } else if (_type == Type::e2D && _arrayLayers > 1) {
            glTexSubImage3D(target, static_cast<glm::i32>(mipLevel), 0, 0, static_cast<glm::i32>(layer) * width * height, width, height, 1, baseFormat, dataType, nullptr);
        } else if (_type == Type::e3D) {
            glTexSubImage3D(target, static_cast<glm::i32>(mipLevel), 0, 0, 0, width, height, depth, baseFormat, dataType, nullptr);
        } else {
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
            throw std::runtime_error("Unsupported image type or array layer configuration for copying data!");
        }
        glCheckError();
    }

    GLenum Image::InternalFormatFromImageFormat(const gfx::Image::Format format)
    {
        switch (format)
        {
        case Format::eR8_UNORM: return GL_R8;
        case Format::eR8_SNORM: return GL_R8_SNORM;
        case Format::eR8_UINT: return GL_R8UI;
        case Format::eR8_SINT: return GL_R8I;

        case Format::eRG8_UNORM: return GL_RG8;
        case Format::eRG8_SNORM: return GL_RG8_SNORM;
        case Format::eRG8_UINT: return GL_RG8UI;
        case Format::eRG8_SINT: return GL_RG8I;

        case Format::eRGB8_UNORM: return GL_RGB8;
        case Format::eRGB8_SNORM: return GL_RGB8_SNORM;
        case Format::eRGB8_UINT: return GL_RGB8UI;
        case Format::eRGB8_SINT: return GL_RGB8I;
        case Format::eRGB8_SRGB: return GL_SRGB8;

        case Format::eRGBA8_UNORM: return GL_RGBA8;
        case Format::eRGBA8_SNORM: return GL_RGBA8_SNORM;
        case Format::eRGBA8_UINT: return GL_RGBA8UI;
        case Format::eRGBA8_SINT: return GL_RGBA8I;
        case Format::eRGBA8_SRGB: return GL_SRGB8_ALPHA8;

        case Format::eR16_UNORM: return GL_R16;
        case Format::eR16_SNORM: return GL_R16_SNORM;
        case Format::eR16_UINT: return GL_R16UI;
        case Format::eR16_SINT: return GL_R16I;
        case Format::eR16_SFLOAT: return GL_R16F;

        case Format::eRG16_UNORM: return GL_RG16;
        case Format::eRG16_SNORM: return GL_RG16_SNORM;
        case Format::eRG16_UINT: return GL_RG16UI;
        case Format::eRG16_SINT: return GL_RG16I;
        case Format::eRG16_SFLOAT: return GL_RG16F;

        case Format::eRGB16_UNORM: return GL_RGB16;
        case Format::eRGB16_SNORM: return GL_RGB16_SNORM;
        case Format::eRGB16_UINT: return GL_RGB16UI;
        case Format::eRGB16_SINT: return GL_RGB16I;
        case Format::eRGB16_SFLOAT: return GL_RGB16F;

        case Format::eRGBA16_UNORM: return GL_RGBA16;
        case Format::eRGBA16_SNORM: return GL_RGBA16_SNORM;
        case Format::eRGBA16_UINT: return GL_RGBA16UI;
        case Format::eRGBA16_SINT: return GL_RGBA16I;
        case Format::eRGBA16_SFLOAT: return GL_RGBA16F;

        case Format::eR32_UINT: return GL_R32UI;
        case Format::eR32_SINT: return GL_R32I;
        case Format::eR32_SFLOAT: return GL_R32F;

        case Format::eRG32_UINT: return GL_RG32UI;
        case Format::eRG32_SINT: return GL_RG32I;
        case Format::eRG32_SFLOAT: return GL_RG32F;

        case Format::eRGB32_UINT: return GL_RGB32UI;
        case Format::eRGB32_SINT: return GL_RGB32I;
        case Format::eRGB32_SFLOAT: return GL_RGB32F;

        case Format::eRGBA32_UINT: return GL_RGBA32UI;
        case Format::eRGBA32_SINT: return GL_RGBA32I;
        case Format::eRGBA32_SFLOAT: return GL_RGBA32F;

        case Format::eD16_UNORM: return GL_DEPTH_COMPONENT16;
        case Format::eD24_UNORM_S8_UINT: return GL_DEPTH24_STENCIL8;
        case Format::eD32_SFLOAT: return GL_DEPTH_COMPONENT32F;
        case Format::eD32_SFLOAT_S8_UINT: return GL_DEPTH32F_STENCIL8;
        default: throw std::runtime_error("Unsupported image format!");
        }
    }

    GLenum Image::BaseFormatFromImageFormat(const Format format)
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
            return GL_RED;
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
            return GL_RG;
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
            return GL_RGB;
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
            return GL_RGBA;
        case Format::eD16_UNORM:
        case Format::eD32_SFLOAT:
            return GL_DEPTH_COMPONENT;
        case Format::eD24_UNORM_S8_UINT:
        case Format::eD32_SFLOAT_S8_UINT:
            return GL_DEPTH_STENCIL;
        default: throw std::runtime_error("Unsupported internal format!");
        }
    }

    GLenum Image::DataTypeFromImageFormat(gfx::Image::Format format)
    {
        switch (format)
        {
        case Format::eR8_UNORM:
        case Format::eRG8_UNORM:
        case Format::eRGB8_UNORM:
        case Format::eRGBA8_UNORM:
        case Format::eR8_UINT:
        case Format::eRG8_UINT:
        case Format::eRGB8_UINT:
        case Format::eRGBA8_UINT:
            return GL_UNSIGNED_BYTE;
        case Format::eR8_SNORM:
        case Format::eRG8_SNORM:
        case Format::eRGB8_SNORM:
        case Format::eRGBA8_SNORM:
        case Format::eR8_SINT:
        case Format::eRG8_SINT:
        case Format::eRGB8_SINT:
        case Format::eRGBA8_SINT:
            return GL_BYTE;
        case Format::eR16_UNORM:
        case Format::eRG16_UNORM:
        case Format::eRGB16_UNORM:
        case Format::eRGBA16_UNORM:
        case Format::eR16_UINT:
        case Format::eRG16_UINT:
        case Format::eRGB16_UINT:
        case Format::eRGBA16_UINT:
            return GL_UNSIGNED_SHORT;
        case Format::eR16_SNORM:
        case Format::eRG16_SNORM:
        case Format::eRGB16_SNORM:
        case Format::eRGBA16_SNORM:
        case Format::eR16_SINT:
        case Format::eRG16_SINT:
        case Format::eRGB16_SINT:
        case Format::eRGBA16_SINT:
            return GL_SHORT;
        case Format::eR16_SFLOAT:
        case Format::eRG16_SFLOAT:
        case Format::eRGB16_SFLOAT:
        case Format::eRGBA16_SFLOAT:
            return GL_HALF_FLOAT;
        case Format::eR32_SFLOAT:
        case Format::eRG32_SFLOAT:
        case Format::eRGB32_SFLOAT:
        case Format::eRGBA32_SFLOAT:
            return GL_FLOAT;
        case Format::eR32_UINT:
        case Format::eRG32_UINT:
        case Format::eRGB32_UINT:
        case Format::eRGBA32_UINT:
            return GL_UNSIGNED_INT;
        case Format::eR32_SINT:
        case Format::eRG32_SINT:
        case Format::eRGB32_SINT:
        case Format::eRGBA32_SINT:
            return GL_INT;
        case Format::eD16_UNORM:
            return GL_UNSIGNED_SHORT;
        case Format::eD24_UNORM_S8_UINT:
            return GL_UNSIGNED_INT_24_8;
        case Format::eD32_SFLOAT:
            return GL_FLOAT;
        case Format::eD32_SFLOAT_S8_UINT:
            return GL_FLOAT_32_UNSIGNED_INT_24_8_REV;
        default: throw std::runtime_error("Unsupported image format for data type!");
        }
    }
}
