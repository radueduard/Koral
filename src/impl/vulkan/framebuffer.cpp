//
// Created by radue on 2/28/2026.
//

#include "framebuffer.h"

namespace gfx::vk
{
    void Framebuffer::Bind() const
    {
    }

    void Framebuffer::Unbind() const
    {
    }

    Framebuffer::Framebuffer()
    {

    }

    Framebuffer::Framebuffer(const Framebuffer::Builder& builder) : gfx::Framebuffer(builder)
    {

    }

    Framebuffer::~Framebuffer()
    {
    }
}
