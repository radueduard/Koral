//
// Created by radue on 2/28/2026.
//

module;

#include <glm/glm.hpp>

export module vk.framebuffer;
import gfx.framebuffer;

namespace gfx::vk
{
    export class Framebuffer final : public gfx::Framebuffer {
    public:
        void Bind() const override;
        void Unbind() const override;

        Framebuffer();
        explicit Framebuffer(const Framebuffer::Builder& builder);

        ~Framebuffer() override;
        void Resize(const glm::uvec2& newExtent) const override;
    };
}
