//
// Created by radue on 2/20/2026.
//

#pragma once
#include <GL/glew.h>

#include <sampler.h>

namespace kor::ogl
{
    class Sampler final : public kor::Sampler
    {
    public:
        explicit Sampler(const kor::Sampler::Builder& createInfo);
        ~Sampler() override;

        GLuint operator*() const { return _id; }
    private:
        GLuint _id = 0;

        static constexpr GLenum GetMinFilterFromFilterMode(Filter filterMode, MipmapMode mipmapMode);
        static constexpr GLenum GetMagFilterFromFilterMode(Filter filterMode);
        static constexpr GLenum GetWrapMode(AddressMode addressMode);
        static constexpr GLenum GetCompareOpFromCompareMode(CompareOp compareOp);
    };
}
