//
// Created by radue on 2/21/2026.
//

module;

#include <GL/glew.h>

export module gfx:ogl_framebuffer;
import :framebuffer;

namespace gfx::ogl {
    export class Framebuffer final : public gfx::Framebuffer {
    public:
        Framebuffer();

        explicit Framebuffer(const gfx::Framebuffer::Builder& createInfo);
        ~Framebuffer() override;

        GLuint operator*() const;
        void Bind() const override;
        void Unbind() const override;
    private:
        GLuint _id;
    };
}

