//
// Created by radue on 2/18/2026.
//

#include "image.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <magic_enum/magic_enum.hpp>

#include "buffer.h"
#include "ogl_err_handling.h"

namespace kor::ogl
{
    GLenum GetTargetFromImageType(const kor::Image::Type type, const kor::SampleCount msaa, const glm::u32 arrayLayers)
    {
        switch (type) {
        case kor::Image::Type::e1D:
            return arrayLayers == 1 ? GL_TEXTURE_1D : GL_TEXTURE_1D_ARRAY;
        case kor::Image::Type::e2D:
            if (arrayLayers == 1) {
                return msaa == kor::SampleCount::e1 ? GL_TEXTURE_2D : GL_TEXTURE_2D_MULTISAMPLE;
            } else {
                return msaa == kor::SampleCount::e1 ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D_MULTISAMPLE_ARRAY;
            }
        case kor::Image::Type::e3D:
            return GL_TEXTURE_3D;
        default:
            throw std::runtime_error("Unsupported image type!");
        }
    }

    Image::Image(const kor::Image::Builder& createInfo) : kor::Image(createInfo)
    {
        if (createInfo.sampleCount != SampleCount::e1 && createInfo.type != Type::e2D) {
            std::cerr << "Error: Multisampled images are only supported for 2D images! Attempting to create a multisampled image with type " << magic_enum::enum_name(createInfo.type) << std::endl;
        }

        if (createInfo.arrayLayers > 1 && createInfo.type == Type::e3D) {
            std::cerr << "Error: Multisampled images are not supported!" << std::endl;
        }

        if (isDepthStencilFormat(createInfo.format) && createInfo.type != Type::e2D) {
            std::cerr << "Error: Depth/stencil formats are only supported for 2D images! Attempting to create a depth/stencil image with type " << magic_enum::enum_name(createInfo.type) << std::endl;
        }


        // Determine the appropriate OpenGL texture target based on the image type and SampleCount settings
        GLenum target;
        switch (createInfo.type) {
        case Type::e1D:
            target = createInfo.arrayLayers == 1 ? GL_TEXTURE_1D : GL_TEXTURE_1D_ARRAY;
            break;
        case Type::e2D:
            target = createInfo.arrayLayers == 1
                ? (createInfo.sampleCount == SampleCount::e1 ? GL_TEXTURE_2D : GL_TEXTURE_2D_MULTISAMPLE)
                : (createInfo.sampleCount == SampleCount::e1 ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D_MULTISAMPLE_ARRAY);
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

        if (createInfo.type == kor::Image::Type::e1D && createInfo.arrayLayers == 1) {
            glTexStorage1D(target, _mipLevels, internalFormat, createInfo.extent.x);
        } else if (createInfo.type == kor::Image::Type::e1D && createInfo.arrayLayers > 1) {
            glTexStorage2D(target, _mipLevels, internalFormat, createInfo.extent.x, createInfo.arrayLayers);
        } else if (createInfo.type == kor::Image::Type::e2D && createInfo.arrayLayers == 1) {
            if (createInfo.sampleCount == kor::SampleCount::e1) {
                glTexStorage2D(target, _mipLevels, internalFormat, createInfo.extent.x, createInfo.extent.y);
            } else {
                glTexStorage2DMultisample(target, static_cast<GLsizei>(createInfo.sampleCount), internalFormat, createInfo.extent.x, createInfo.extent.y, GL_TRUE);
            }
        } else if (createInfo.type == kor::Image::Type::e2D && createInfo.arrayLayers > 1) {
            if (createInfo.sampleCount == kor::SampleCount::e1) {
                glTexStorage3D(target, _mipLevels, internalFormat, createInfo.extent.x, createInfo.extent.y, createInfo.arrayLayers);
            } else {
                glTexStorage3DMultisample(target, static_cast<GLsizei>(createInfo.sampleCount), internalFormat, createInfo.extent.x, createInfo.extent.y, createInfo.arrayLayers, GL_TRUE);
            }
        } else if (createInfo.type == kor::Image::Type::e3D) {
            glTexStorage3D(target, _mipLevels, internalFormat, createInfo.extent.x, createInfo.extent.y, createInfo.extent.z);
        }

        // check for errors
        if (glCheckError()) {
            glDeleteTextures(1, &_id);
        }
    }

    Image::~Image()
    {
        glDeleteTextures(1, &_id);
        glCheckError();
    }

    void Image::doResize(const glm::uvec3 &extent) {

        // glTexStorage* storage is immutable, so a resize must recreate the texture.
        // Image views forward to the image's current id (see ImageView::operator*),
        // so they keep working across the swap — but the generation is still bumped, because the
        // contract is shared with Vulkan and something other than a view may be watching it.
        const GLenum target = GetTargetFromImageType(_type, _sampleCount, _arrayLayers);
        glDeleteTextures(1, &_id);
        glCreateTextures(target, 1, &_id);
        glBindTexture(target, _id);
        glCheckError();

        _extent = extent;
        const GLenum internalFormat = InternalFormatFromImageFormat(_format);

        if (_type == Type::e1D && _arrayLayers == 1) {
            glTexStorage1D(target, _mipLevels, internalFormat, extent.x);
        } else if (_type == Type::e1D && _arrayLayers > 1) {
            glTexStorage2D(target, _mipLevels, internalFormat, extent.x, _arrayLayers);
        } else if (_type == Type::e2D && _arrayLayers == 1) {
            if (_sampleCount == SampleCount::e1) {
                glTexStorage2D(target, _mipLevels, internalFormat, extent.x, extent.y);
            } else {
                glTexStorage2DMultisample(target, static_cast<GLsizei>(_sampleCount), internalFormat, extent.x, extent.y, GL_TRUE);
            }
        } else if (_type == Type::e2D && _arrayLayers > 1) {
            if (_sampleCount == SampleCount::e1) {
                glTexStorage3D(target, _mipLevels, internalFormat, extent.x, extent.y, _arrayLayers);
            } else {
                glTexStorage3DMultisample(target, static_cast<GLsizei>(_sampleCount), internalFormat, extent.x, extent.y, _arrayLayers, GL_TRUE);
            }
        } else if (_type == Type::e3D) {
            glTexStorage3D(target, _mipLevels, internalFormat, extent.x, extent.y, extent.z);
        }

        // check for errors
        if (glCheckError()) {
            throw std::runtime_error("Failed to resize image!");
        }
    }

    bool Image::isFormatSupported(const kor::Image::Format format, const Flags<kor::Image::Usage> usage)
    {
        // A format the conversion table has no entry for is one this engine cannot make an image of,
        // whatever the driver has.
        GLenum internalFormat;
        try {
            internalFormat = InternalFormatFromImageFormat(format);
        } catch (const std::exception&) {
            return false;
        }

        const auto supported = [internalFormat](const GLenum target, const GLenum property) {
            GLint answer = GL_NONE;
            glGetInternalformativ(target, internalFormat, property, 1, &answer);
            glCheckError();
            // GL_FULL_SUPPORT is the only answer worth acting on; GL_CAVEAT_SUPPORT means the driver
            // will emulate it slowly, which for a texture format is not support at all.
            return answer == GL_FULL_SUPPORT;
        };

        if (!supported(GL_TEXTURE_2D, GL_INTERNALFORMAT_SUPPORTED)) return false;
        if (usage & kor::Image::Usage::eStorage && !supported(GL_TEXTURE_2D, GL_SHADER_IMAGE_STORE)) return false;
        if (usage & (Flags(kor::Image::Usage::eColorAttachment) | kor::Image::Usage::eDepthStencilAttachment)
            && !supported(GL_TEXTURE_2D, GL_FRAMEBUFFER_RENDERABLE)) return false;

        return true;
    }

    GLenum Image::InternalFormatFromImageFormat(const kor::Image::Format format)
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

        // ---- Block-compressed ------------------------------------------------------------------
        //
        // These names are the same formats Vulkan calls BC/ASTC/ETC2; GL just spells them after the
        // extensions they arrived in. Whether the driver *has* the extension is another matter — an
        // unsupported one fails at glTexStorage, which is where it belongs.
        case Format::eBC1_RGB_UNORM: return GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
        case Format::eBC1_RGB_SRGB: return GL_COMPRESSED_SRGB_S3TC_DXT1_EXT;
        case Format::eBC1_RGBA_UNORM: return GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
        case Format::eBC1_RGBA_SRGB: return GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT;
        case Format::eBC2_UNORM: return GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
        case Format::eBC2_SRGB: return GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT;
        case Format::eBC3_UNORM: return GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
        case Format::eBC3_SRGB: return GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT;
        case Format::eBC4_UNORM: return GL_COMPRESSED_RED_RGTC1;
        case Format::eBC4_SNORM: return GL_COMPRESSED_SIGNED_RED_RGTC1;
        case Format::eBC5_UNORM: return GL_COMPRESSED_RG_RGTC2;
        case Format::eBC5_SNORM: return GL_COMPRESSED_SIGNED_RG_RGTC2;
        case Format::eBC6H_UFLOAT: return GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT_ARB;
        case Format::eBC6H_SFLOAT: return GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT_ARB;
        case Format::eBC7_UNORM: return GL_COMPRESSED_RGBA_BPTC_UNORM_ARB;
        case Format::eBC7_SRGB: return GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM_ARB;

        case Format::eASTC_4x4_UNORM: return GL_COMPRESSED_RGBA_ASTC_4x4_KHR;
        case Format::eASTC_4x4_SRGB: return GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR;
        case Format::eASTC_6x6_UNORM: return GL_COMPRESSED_RGBA_ASTC_6x6_KHR;
        case Format::eASTC_6x6_SRGB: return GL_COMPRESSED_SRGB8_ALPHA8_ASTC_6x6_KHR;
        case Format::eASTC_8x8_UNORM: return GL_COMPRESSED_RGBA_ASTC_8x8_KHR;
        case Format::eASTC_8x8_SRGB: return GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x8_KHR;

        case Format::eETC2_RGB8_UNORM: return GL_COMPRESSED_RGB8_ETC2;
        case Format::eETC2_RGB8_SRGB: return GL_COMPRESSED_SRGB8_ETC2;
        case Format::eETC2_RGBA8_UNORM: return GL_COMPRESSED_RGBA8_ETC2_EAC;
        case Format::eETC2_RGBA8_SRGB: return GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC;
        case Format::eEAC_R11_UNORM: return GL_COMPRESSED_R11_EAC;
        case Format::eEAC_R11_SNORM: return GL_COMPRESSED_SIGNED_R11_EAC;
        case Format::eEAC_RG11_UNORM: return GL_COMPRESSED_RG11_EAC;
        case Format::eEAC_RG11_SNORM: return GL_COMPRESSED_SIGNED_RG11_EAC;

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

    GLenum Image::DataTypeFromImageFormat(kor::Image::Format format)
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
