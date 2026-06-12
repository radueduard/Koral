//
// Created by radue on 2/20/2026.
//

module;

#include <stdexcept>

module gfx.sampler;
import vk.sampler;
import ogl.sampler;
import gfx.context;
import gfx.window;

namespace gfx
{
    gfx::Resource<Sampler> Sampler::Builder::build() const
    {
        switch (Context::Window().getAPI()) {
            case API::eOpenGL:
            return gfx::MakeResource<ogl::Sampler>(*this);
            case API::eVulkan:
            return gfx::MakeResource<vk::Sampler>(*this);
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
