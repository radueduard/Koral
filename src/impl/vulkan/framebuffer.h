//
// Created by radue on 2/28/2026.
//

#pragma once
#include "core/framebuffer.h"

namespace gfx::vk
{
    class Framebuffer final : public gfx::Framebuffer {
    public:
        void Bind() const override;
        void Unbind() const override;

        Framebuffer();
        explicit Framebuffer(const Framebuffer::Builder& builder);

        ~Framebuffer() override;
    };
}
