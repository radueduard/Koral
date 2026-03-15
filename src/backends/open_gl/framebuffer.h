//
// Created by radue on 2/21/2026.
//

#pragma once
#include <GL/glew.h>
#include <framebuffer.h>

namespace gfx::ogl {
    class Framebuffer final : public gfx::Framebuffer {
    public:
        Framebuffer();

        explicit Framebuffer(const gfx::Framebuffer::Builder& createInfo);
        ~Framebuffer() override;

        GLuint operator*() const;
        void Bind() const override;
        void Unbind() const override;
        [[nodiscard]] bool hasDepthStencilAttachment() const override;

    private:
        GLuint _id;
    };
}

