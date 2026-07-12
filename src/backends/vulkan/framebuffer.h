//
// Created by radue on 2/28/2026.
//

#pragma once
#include "../../../include/framebuffer.h"

namespace kor::vk
{
    class Framebuffer final : public kor::Framebuffer {
    public:
        void Bind() const override;
        void Unbind() const override;

        Framebuffer();
        explicit Framebuffer(const Framebuffer::Builder& builder);

        ~Framebuffer() override;
        void Resize(const glm::uvec2& newExtent) const override;
    };
}
