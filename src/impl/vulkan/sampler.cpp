//
// Created by radue on 2/28/2026.
//

#include "sampler.h"

#include "device.h"
#include "vulkanContext.h"
#include "utils/vk_enum_conversions.h"

namespace gfx::vk
{
    Sampler::Sampler(const Builder& builder) : gfx::Sampler(builder)
    {
        const auto samplerInfo = ::vk::SamplerCreateInfo()
            .setMagFilter(getVkFilter(_magFilter))
            .setMinFilter(getVkFilter(_minFilter))
            .setAddressModeU(getVkSamplerAddressMode(_addressModeU))
            .setAddressModeV(getVkSamplerAddressMode(_addressModeV))
            .setAddressModeW(getVkSamplerAddressMode(_addressModeW))
            .setAnisotropyEnable(_anisotropyEnable)
            .setMaxAnisotropy(_maxAnisotropy)
            .setBorderColor(::vk::BorderColor::eFloatOpaqueBlack)
            .setUnnormalizedCoordinates(_unnormalizedCoordinates)
            .setCompareEnable(_compareEnable)
            .setCompareOp(getVkCompareOp(_compareOp))
            .setMipmapMode(getVkSamplerMipmapMode(_mipmapMode))
            .setMipLodBias(_mipLodBias)
            .setMinLod(_minLod)
            .setMaxLod(_maxLod);

        _handle = vk::Context::Device()->createSampler(samplerInfo);
    }

    Sampler::~Sampler()
    {
        vk::Context::Device()->destroySampler(_handle);
    }
}
