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
    kor::Result<std::unique_ptr<Sampler>> Sampler::Builder::Create() const
    {
        BeginAttempt();

        if (auto v = Validate(); !v) return std::unexpected(v.error());

        const auto api = Context::ActiveAPI();
        if (api != API::eOpenGL && api != API::eVulkan)
            return Fail(ErrorCode::eUnknownApi, "Unknown graphics API!");

        return Guard(ErrorCode::eBackend, [&]() -> std::unique_ptr<Sampler> {
            return (api == API::eVulkan)
                ? kor::MakeBackendPtr<Sampler, vk::Sampler>(*this)
                : kor::MakeBackendPtr<Sampler, ogl::Sampler>(*this);
        });
    }

    kor::Resource<Sampler> Sampler::Builder::Build(const std::source_location where) const
    {
        return Materialize<Sampler>(*this, "Sampler", where);
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
