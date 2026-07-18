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

namespace kor
{
    kor::Result<std::unique_ptr<Sampler>> Sampler::Builder::create() const
    {
        beginAttempt();

        if (auto v = validate(); !v) return std::unexpected(v.error());

        const auto api = Context::activeAPI();
        if (api != API::eOpenGL && api != API::eVulkan)
            return fail(ErrorCode::eUnknownApi, "Unknown graphics API!");

        return guard(ErrorCode::eBackend, [&]() -> std::unique_ptr<Sampler> {
            return (api == API::eVulkan)
                ? kor::MakeBackendPtr<Sampler, vk::Sampler>(*this)
                : kor::MakeBackendPtr<Sampler, ogl::Sampler>(*this);
        });
    }

    kor::Resource<Sampler> Sampler::Builder::build(const std::source_location where) const
    {
        return materialize<Sampler>(*this, "Sampler", where);
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
