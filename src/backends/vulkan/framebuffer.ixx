//
// Created by radue on 2/28/2026.
//

module;

#include <glm/glm.hpp>

export module gfx:vk_framebuffer;
import :vk_types;

import :framebuffer;

namespace gfx::vk
{
    export class Framebuffer final : public gfx::Framebuffer {
    public:
        void Bind() const override;
        void Unbind() const override;

        Framebuffer();
        explicit Framebuffer(const Builder& builder);

        ~Framebuffer() override;
        void Resize(const glm::uvec2& newExtent) const override;
    };
}
