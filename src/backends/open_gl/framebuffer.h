//
// Created by radue on 2/21/2026.
//

#pragma once
#include <GL/glew.h>
#include <framebuffer.h>

namespace kor::ogl {
    class Framebuffer final : public kor::Framebuffer {
    public:
        Framebuffer();

        explicit Framebuffer(const kor::Framebuffer::Builder& createInfo);
        ~Framebuffer() override;

        GLuint operator*() const;
        void Bind() const override;
        void Unbind() const override;
        [[nodiscard]] bool hasDepthStencilAttachment() const;

    private:
        GLuint _id;
    };
}

