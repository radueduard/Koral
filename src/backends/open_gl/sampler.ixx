//
// Created by radue on 2/20/2026.
//

module;

#include <GL/glew.h>

export module gfx:ogl_sampler;
import :ogl_types;

import :sampler;

namespace gfx::ogl
{
    class Sampler final : public gfx::Sampler
    {
    public:
        explicit Sampler(const gfx::Sampler::Builder& createInfo);
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
