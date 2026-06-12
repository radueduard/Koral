//
// Created by radue on 2/20/2026.
//

module;
#include "ogl_err_handling.h"
module ogl.sampler;

namespace gfx::ogl
{
    Sampler::Sampler(const gfx::Sampler::Builder& createInfo) : gfx::Sampler(createInfo)
    {
        glCreateSamplers(1, &_id);
        glCheckError();

        glSamplerParameteri(_id, GL_TEXTURE_MIN_FILTER, GetMinFilterFromFilterMode(createInfo.minFilter, createInfo.mipmapMode));
        glSamplerParameteri(_id, GL_TEXTURE_MAG_FILTER, GetMagFilterFromFilterMode(createInfo.magFilter));
        glSamplerParameteri(_id, GL_TEXTURE_WRAP_S, GetWrapMode(createInfo.addressModeU));
        glSamplerParameteri(_id, GL_TEXTURE_WRAP_T, GetWrapMode(createInfo.addressModeV));
        glSamplerParameteri(_id, GL_TEXTURE_WRAP_R, GetWrapMode(createInfo.addressModeW));
        glSamplerParameterf(_id, GL_TEXTURE_LOD_BIAS, createInfo.mipLodBias);
        if (createInfo.anisotropyEnable) {
            glSamplerParameterf(_id, GL_TEXTURE_MAX_ANISOTROPY, createInfo.maxAnisotropy);
        }
        glSamplerParameterf(_id, GL_TEXTURE_MIN_LOD, createInfo.minLod);
        glSamplerParameterf(_id, GL_TEXTURE_MAX_LOD, createInfo.maxLod);
        if (createInfo.compareEnable) {
            glSamplerParameteri(_id, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
            glSamplerParameteri(_id, GL_TEXTURE_COMPARE_FUNC, GetCompareOpFromCompareMode(createInfo.compareOp));
        }

        glCheckError();
    }

    Sampler::~Sampler()
    {
        glDeleteSamplers(1, &_id);
        glCheckError();
    }

    constexpr GLenum Sampler::GetMinFilterFromFilterMode(const Filter filterMode, const MipmapMode mipmapMode)
    {
        switch (filterMode) {
        case Filter::eLinear:
            switch (mipmapMode) {
            case MipmapMode::eNearest: return GL_LINEAR_MIPMAP_NEAREST;
            case MipmapMode::eLinear: return GL_LINEAR_MIPMAP_LINEAR;
            }
        case Filter::eNearest:
            switch (mipmapMode) {
            case MipmapMode::eNearest: return GL_NEAREST_MIPMAP_NEAREST;
            case MipmapMode::eLinear: return GL_NEAREST_MIPMAP_LINEAR;
            }
        default:
            std::cerr << "Unsupported filter mode! Defaulting to GL_LINEAR." << std::endl;
                return GL_LINEAR;
        }
    }

    constexpr GLenum Sampler::GetMagFilterFromFilterMode(const Filter filterMode) {
        switch (filterMode) {
        case Filter::eLinear: return GL_LINEAR;
        case Filter::eNearest: return GL_NEAREST;
        default:
            std::cerr << "Unsupported filter mode! Defaulting to GL_LINEAR." << std::endl;
                return GL_LINEAR;
        }
    }

    constexpr GLenum Sampler::GetWrapMode(const AddressMode addressMode)
    {
        switch (addressMode) {
        case AddressMode::eRepeat: return GL_REPEAT;
        case AddressMode::eMirroredRepeat: return GL_MIRRORED_REPEAT;
        case AddressMode::eClampToEdge: return GL_CLAMP_TO_EDGE;
        case AddressMode::eClampToBorder: return GL_CLAMP_TO_BORDER;
        default:
            std::cerr << "Unsupported address mode! Defaulting to GL_REPEAT." << std::endl;
                return GL_REPEAT;
        }
    }

    constexpr GLenum Sampler::GetCompareOpFromCompareMode(const CompareOp compareOp)
    {
        switch (compareOp) {
        case CompareOp::eNever: return GL_NEVER;
        case CompareOp::eLess: return GL_LESS;
        case CompareOp::eEqual: return GL_EQUAL;
        case CompareOp::eLessOrEqual: return GL_LEQUAL;
        case CompareOp::eGreater: return GL_GREATER;
        case CompareOp::eNotEqual: return GL_NOTEQUAL;
        case CompareOp::eGreaterOrEqual: return GL_GEQUAL;
        case CompareOp::eAlways: return GL_ALWAYS;
        default:
            std::cerr << "Unsupported compare op! Defaulting to GL_ALWAYS." << std::endl;
                return GL_ALWAYS;
        }
    }

}
