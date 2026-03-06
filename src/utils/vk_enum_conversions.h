//
// Created by radue on 2/28/2026.
//

#pragma once
#include "core/image.h"
#include "core/imageView.h"
#include <vulkan/vulkan.hpp>

namespace gfx
{
    inline ::vk::Format getVkFormat(const Image::Format format)
    {
        switch (format)
        {
        case Image::Format::eR8_UNORM: return ::vk::Format::eR8Unorm;
        case Image::Format::eR8_SNORM: return ::vk::Format::eR8Snorm;
        case Image::Format::eR8_UINT: return ::vk::Format::eR8Uint;
        case Image::Format::eR8_SINT: return ::vk::Format::eR8Sint;
        case Image::Format::eRG8_UNORM: return ::vk::Format::eR8G8Unorm;
        case Image::Format::eRG8_SNORM: return ::vk::Format::eR8G8Snorm;
        case Image::Format::eRG8_UINT: return ::vk::Format::eR8G8Uint;
        case Image::Format::eRG8_SINT: return ::vk::Format::eR8G8Sint;
        case Image::Format::eRGB8_UNORM: return ::vk::Format::eR8G8B8Unorm;
        case Image::Format::eRGB8_SNORM: return ::vk::Format::eR8G8B8Snorm;
        case Image::Format::eRGB8_UINT: return ::vk::Format::eR8G8B8Uint;
        case Image::Format::eRGB8_SINT: return ::vk::Format::eR8G8B8Sint;
        case Image::Format::eRGB8_SRGB: return ::vk::Format::eR8G8B8Srgb;
        case Image::Format::eRGBA8_UNORM: return ::vk::Format::eR8G8B8A8Unorm;
        case Image::Format::eRGBA8_SNORM: return ::vk::Format::eR8G8B8A8Snorm;
        case Image::Format::eRGBA8_UINT: return ::vk::Format::eR8G8B8A8Uint;
        case Image::Format::eRGBA8_SINT: return ::vk::Format::eR8G8B8A8Sint;
        case Image::Format::eRGBA8_SRGB: return ::vk::Format::eR8G8B8A8Srgb;
        case Image::Format::eR16_UNORM: return ::vk::Format::eR16Unorm;
        case Image::Format::eR16_SNORM: return ::vk::Format::eR16Snorm;
        case Image::Format::eR16_UINT: return ::vk::Format::eR16Uint;
        case Image::Format::eR16_SINT: return ::vk::Format::eR16Sint;
        case Image::Format::eR16_SFLOAT: return ::vk::Format::eR16Sfloat;
        case Image::Format::eRG16_UNORM: return ::vk::Format::eR16G16Unorm;
        case Image::Format::eRG16_SNORM: return ::vk::Format::eR16G16Snorm;
        case Image::Format::eRG16_UINT: return ::vk::Format::eR16G16Uint;
        case Image::Format::eRG16_SINT: return ::vk::Format::eR16G16Sint;
        case Image::Format::eRG16_SFLOAT: return ::vk::Format::eR16G16Sfloat;
        case Image::Format::eRGB16_UNORM: return ::vk::Format::eR16G16B16Unorm;
        case Image::Format::eRGB16_SNORM: return ::vk::Format::eR16G16B16Snorm;
        case Image::Format::eRGB16_UINT: return ::vk::Format::eR16G16B16Uint;
        case Image::Format::eRGB16_SINT: return ::vk::Format::eR16G16B16Sint;
        case Image::Format::eRGB16_SFLOAT: return ::vk::Format::eR16G16B16Sfloat;
        case Image::Format::eRGBA16_UNORM: return ::vk::Format::eR16G16B16A16Unorm;
        case Image::Format::eRGBA16_SNORM: return ::vk::Format::eR16G16B16A16Snorm;
        case Image::Format::eRGBA16_UINT: return ::vk::Format::eR16G16B16A16Uint;
        case Image::Format::eRGBA16_SINT: return ::vk::Format::eR16G16B16A16Sint;
        case Image::Format::eRGBA16_SFLOAT: return ::vk::Format::eR16G16B16A16Sfloat;
        case Image::Format::eR32_UINT: return ::vk::Format::eR32Uint;
        case Image::Format::eR32_SINT: return ::vk::Format::eR32Sint;
        case Image::Format::eR32_SFLOAT: return ::vk::Format::eR32Sfloat;
        case Image::Format::eRG32_UINT: return ::vk::Format::eR32G32Uint;
        case Image::Format::eRG32_SINT: return ::vk::Format::eR32G32Sint;
        case Image::Format::eRG32_SFLOAT: return ::vk::Format::eR32G32Sfloat;
        case Image::Format::eRGB32_UINT: return ::vk::Format::eR32G32B32Uint;
        case Image::Format::eRGB32_SINT: return ::vk::Format::eR32G32B32Sint;
        case Image::Format::eRGB32_SFLOAT: return ::vk::Format::eR32G32B32Sfloat;
        case Image::Format::eRGBA32_UINT: return ::vk::Format::eR32G32B32A32Uint;
        case Image::Format::eRGBA32_SINT: return ::vk::Format::eR32G32B32A32Sint;
        case Image::Format::eRGBA32_SFLOAT: return ::vk::Format::eR32G32B32A32Sfloat;
        case Image::Format::eD16_UNORM: return ::vk::Format::eD16Unorm;
        case Image::Format::eD24_UNORM_S8_UINT: return ::vk::Format::eD24UnormS8Uint;
        case Image::Format::eD32_SFLOAT: return ::vk::Format::eD32Sfloat;
        case Image::Format::eD32_SFLOAT_S8_UINT: return ::vk::Format::eD32SfloatS8Uint;
        default: throw std::runtime_error("Unsupported image format!");
        }
    }

    inline gfx::Image::Format getFormat(const ::vk::Format format)
    {
        switch (format)
        {
        case ::vk::Format::eR8Unorm: return Image::Format::eR8_UNORM;
        case ::vk::Format::eR8Snorm: return Image::Format::eR8_SNORM;
        case ::vk::Format::eR8Uint: return Image::Format::eR8_UINT;
        case ::vk::Format::eR8Sint: return Image::Format::eR8_SINT;
        case ::vk::Format::eR8G8Unorm: return Image::Format::eRG8_UNORM;
        case ::vk::Format::eR8G8Snorm: return Image::Format::eRG8_SNORM;
        case ::vk::Format::eR8G8Uint: return Image::Format::eRG8_UINT;
        case ::vk::Format::eR8G8Sint: return Image::Format::eRG8_SINT;
        case ::vk::Format::eR8G8B8Unorm: return Image::Format::eRGB8_UNORM;
        case ::vk::Format::eR8G8B8Snorm: return Image::Format::eRGB8_SNORM;
        case ::vk::Format::eR8G8B8Uint: return Image::Format::eRGB8_UINT;
        case ::vk::Format::eR8G8B8Sint: return Image::Format::eRGB8_SINT;
        case ::vk::Format::eR8G8B8Srgb: return Image::Format::eRGB8_SRGB;
        case ::vk::Format::eR8G8B8A8Unorm: return Image::Format::eRGBA8_UNORM;
        case ::vk::Format::eR8G8B8A8Snorm: return Image::Format::eRGBA8_SNORM;
        case ::vk::Format::eR8G8B8A8Uint: return Image::Format::eRGBA8_UINT;
        case ::vk::Format::eR8G8B8A8Sint: return Image::Format::eRGBA8_SINT;
        case ::vk::Format::eR8G8B8A8Srgb: return Image::Format::eRGBA8_SRGB;
        case ::vk::Format::eR16Unorm: return Image::Format::eR16_UNORM;
        case ::vk::Format::eR16Snorm: return Image::Format::eR16_SNORM;
        case ::vk::Format::eR16Uint: return Image::Format::eR16_UINT;
        case ::vk::Format::eR16Sint: return Image::Format::eR16_SINT;
        case ::vk::Format::eR16Sfloat: return Image::Format::eR16_SFLOAT;
        case ::vk::Format::eR16G16Unorm: return Image::Format::eRG16_UNORM;
        case ::vk::Format::eR16G16Snorm: return Image::Format::eRG16_SNORM;
        case ::vk::Format::eR16G16Uint: return Image::Format::eRG16_UINT;
        case ::vk::Format::eR16G16Sint: return Image::Format::eRG16_SINT;
        case ::vk::Format::eR16G16Sfloat: return Image::Format::eRG16_SFLOAT;
        case ::vk::Format::eR16G16B16Unorm: return Image::Format::eRGB16_UNORM;
        case ::vk::Format::eR16G16B16Snorm: return Image::Format::eRGB16_SNORM;
        case ::vk::Format::eR16G16B16Uint: return Image::Format::eRGB16_UINT;
        case ::vk::Format::eR16G16B16Sint: return Image::Format::eRGB16_SINT;
        case ::vk::Format::eR16G16B16Sfloat: return Image::Format::eRGB16_SFLOAT;
        case ::vk::Format::eR16G16B16A16Unorm: return Image::Format::eRGBA16_UNORM;
        case ::vk::Format::eR16G16B16A16Snorm: return Image::Format::eRGBA16_SNORM;
        case ::vk::Format::eR16G16B16A16Uint: return Image::Format::eRGBA16_UINT;
        case ::vk::Format::eR16G16B16A16Sint: return Image::Format::eRGBA16_SINT;
        case ::vk::Format::eR16G16B16A16Sfloat: return Image::Format::eRGBA16_SFLOAT;
        case ::vk::Format::eR32Uint: return Image::Format::eR32_UINT;
        case ::vk::Format::eR32Sint: return Image::Format::eR32_SINT;
        case ::vk::Format::eR32Sfloat: return Image::Format::eR32_SFLOAT;
        case ::vk::Format::eR32G32Uint: return Image::Format::eRG32_UINT;
        case ::vk::Format::eR32G32Sint: return Image::Format::eRG32_SINT;
        case ::vk::Format::eR32G32Sfloat: return Image::Format::eRG32_SFLOAT;
        case ::vk::Format::eR32G32B32Uint: return Image::Format::eRGB32_UINT;
        case ::vk::Format::eR32G32B32Sint: return Image::Format::eRGB32_SINT;
        case ::vk::Format::eR32G32B32Sfloat: return Image::Format::eRGB32_SFLOAT;
        case ::vk::Format::eR32G32B32A32Uint: return Image::Format::eRGBA32_UINT;
        case ::vk::Format::eR32G32B32A32Sint: return Image::Format::eRGBA32_SINT;
        case ::vk::Format::eR32G32B32A32Sfloat: return Image::Format::eRGBA32_SFLOAT;
        case ::vk::Format::eD16Unorm: return Image::Format::eD16_UNORM;
        case ::vk::Format::eD24UnormS8Uint: return Image::Format::eD24_UNORM_S8_UINT;
        case ::vk::Format::eD32Sfloat: return Image::Format::eD32_SFLOAT;
        case ::vk::Format::eD32SfloatS8Uint: return Image::Format::eD32_SFLOAT_S8_UINT;
        default: throw std::runtime_error("Unsupported image format!");
        }
    }

    inline ::vk::ImageType getVkImageType(const gfx::Image::Type type)
    {
        switch (type) {
        case gfx::Image::Type::e1D: return ::vk::ImageType::e1D;
        case gfx::Image::Type::e2D: return ::vk::ImageType::e2D;
        case gfx::Image::Type::e3D: return ::vk::ImageType::e3D;
        default: throw std::runtime_error("Unknown image type");
        }
    }

    inline ::vk::ImageViewType getVkImageViewType(const gfx::ImageView::Type type)
    {
        switch (type) {
        case gfx::ImageView::Type::e1D: return ::vk::ImageViewType::e1D;
        case gfx::ImageView::Type::e2D: return ::vk::ImageViewType::e2D;
        case gfx::ImageView::Type::e3D: return ::vk::ImageViewType::e3D;
        case gfx::ImageView::Type::eCube: return ::vk::ImageViewType::eCube;
        case gfx::ImageView::Type::e1DArray: return ::vk::ImageViewType::e1DArray;
        case gfx::ImageView::Type::e2DArray: return ::vk::ImageViewType::e2DArray;
        case gfx::ImageView::Type::eCubeArray: return ::vk::ImageViewType::eCubeArray;
        default: throw std::runtime_error("Unknown image type");
        }
    }

    inline ::vk::ImageUsageFlags getVkUsage(const Flags<gfx::Image::Usage> usage)
    {
        ::vk::ImageUsageFlags usageFlags;
        if (usage & gfx::Image::Usage::eTransferSrc) usageFlags |= ::vk::ImageUsageFlagBits::eTransferSrc;
        if (usage & gfx::Image::Usage::eTransferDst) usageFlags |= ::vk::ImageUsageFlagBits::eTransferDst;
        if (usage & gfx::Image::Usage::eSampled) usageFlags |= ::vk::ImageUsageFlagBits::eSampled;
        if (usage & gfx::Image::Usage::eStorage) usageFlags |= ::vk::ImageUsageFlagBits::eStorage;
        if (usage & gfx::Image::Usage::eColorAttachment) usageFlags |= ::vk::ImageUsageFlagBits::eColorAttachment;
        if (usage & gfx::Image::Usage::eDepthStencilAttachment) usageFlags |= ::vk::ImageUsageFlagBits::eDepthStencilAttachment;
        return usageFlags;
    }

    inline ::vk::SampleCountFlagBits getVkSampleCount(MSAA msaa)
    {
        switch (msaa) {
        case MSAA::eNone: return ::vk::SampleCountFlagBits::e1;
        case MSAA::e2x: return ::vk::SampleCountFlagBits::e2;
        case MSAA::e4x: return ::vk::SampleCountFlagBits::e4;
        case MSAA::e8x: return ::vk::SampleCountFlagBits::e8;
        case MSAA::e16x: return ::vk::SampleCountFlagBits::e16;
        default: throw std::runtime_error("Unknown MSAA level");
        }
    }

    inline ::vk::Filter getVkFilter(const gfx::Sampler::Filter filter)
    {
        switch (filter) {
        case gfx::Sampler::Filter::eNearest: return ::vk::Filter::eNearest;
        case gfx::Sampler::Filter::eLinear: return ::vk::Filter::eLinear;
        default: throw std::runtime_error("Unknown filter type");
        }
    }

    inline ::vk::SamplerMipmapMode getVkMipmapMode(const gfx::Sampler::MipmapMode mode)
    {
        switch (mode) {
        case gfx::Sampler::MipmapMode::eNearest: return ::vk::SamplerMipmapMode::eNearest;
        case gfx::Sampler::MipmapMode::eLinear: return ::vk::SamplerMipmapMode::eLinear;
        default: throw std::runtime_error("Unknown mipmap mode");
        }
    }

    inline ::vk::SamplerAddressMode getVkSamplerAddressMode(const gfx::Sampler::AddressMode mode)
    {
        switch (mode) {
        case gfx::Sampler::AddressMode::eRepeat: return ::vk::SamplerAddressMode::eRepeat;
        case gfx::Sampler::AddressMode::eMirroredRepeat: return ::vk::SamplerAddressMode::eMirroredRepeat;
        case gfx::Sampler::AddressMode::eClampToEdge: return ::vk::SamplerAddressMode::eClampToEdge;
        case gfx::Sampler::AddressMode::eClampToBorder: return ::vk::SamplerAddressMode::eClampToBorder;
        default: throw std::runtime_error("Unknown address mode");
        }
    }

    inline ::vk::CompareOp getVkCompareOp(const gfx::Sampler::CompareOp op)
    {
        switch (op) {
        case gfx::Sampler::CompareOp::eNever: return ::vk::CompareOp::eNever;
        case gfx::Sampler::CompareOp::eLess: return ::vk::CompareOp::eLess;
        case gfx::Sampler::CompareOp::eEqual: return ::vk::CompareOp::eEqual;
        case gfx::Sampler::CompareOp::eLessOrEqual: return ::vk::CompareOp::eLessOrEqual;
        case gfx::Sampler::CompareOp::eGreater: return ::vk::CompareOp::eGreater;
        case gfx::Sampler::CompareOp::eNotEqual: return ::vk::CompareOp::eNotEqual;
        case gfx::Sampler::CompareOp::eGreaterOrEqual: return ::vk::CompareOp::eGreaterOrEqual;
        case gfx::Sampler::CompareOp::eAlways: return ::vk::CompareOp::eAlways;
        default: throw std::runtime_error("Unknown compare operation");
        }
    }

    inline ::vk::SamplerMipmapMode getVkSamplerMipmapMode(const gfx::Sampler::MipmapMode mode)
    {
        switch (mode) {
        case gfx::Sampler::MipmapMode::eNearest: return ::vk::SamplerMipmapMode::eNearest;
        case gfx::Sampler::MipmapMode::eLinear: return ::vk::SamplerMipmapMode::eLinear;
        default: throw std::runtime_error("Unknown mipmap mode");
        }
    }


}
