//
// Created by radue on 2/20/2026.
//

#include "../backends/open_gl/sampler.h"
#include "../backends/vulkan/sampler.h"

#include <sampler.h>
#include <context.h>
#include <window.h>
#include <framebuffer.h>
#include <surface.h>

namespace gfx
{
    std::unique_ptr<Sampler> Sampler::Builder::build() const
    {
        switch (Context::Window().getAPI()) {
        case API::eOpenGL:
            return std::make_unique<ogl::Sampler>(*this);
        case API::eVulkan:
            return std::make_unique<vk::Sampler>(*this);
        default:
            throw std::runtime_error("Unknown graphics API!");
        }
    }

    Sampler::Sampler(const Builder& builder) :
        _minFilter(builder.minFilter),
        _magFilter(builder.magFilter),
        _mipmapMode(builder.mipmapMode),
        _addressModeU(builder.addressModeU),
        _addressModeV(builder.addressModeV),
        _addressModeW(builder.addressModeW),
        _mipLodBias(builder.mipLodBias),
        _anisotropyEnable(builder.anisotropyEnable),
        _maxAnisotropy(builder.maxAnisotropy),
        _compareEnable(builder.compareEnable),
        _compareOp(builder.compareOp),
        _minLod(builder.minLod),
        _maxLod(builder.maxLod),
        _unnormalizedCoordinates(builder.unnormalizedCoordinates) {}
}
