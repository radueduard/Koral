//
// Created by radue on 2/18/2026.
//
module;

#include <GL/glew.h>
#include <glm/glm.hpp>

export module gfx:ogl_image;
import :ogl_types;

import :image;

namespace gfx::ogl {
    export class Image : public gfx::Image {
    public:
        explicit Image(const gfx::Image::Builder& createInfo);
        ~Image() override;

        GLuint operator*() const { return _id; }

        void Resize(const glm::uvec3 &extent) override;

        GLenum getGLFormat() const { return InternalFormatFromImageFormat(_format); }

        [[nodiscard]] static GLenum InternalFormatFromImageFormat(gfx::Image::Format format);
        [[nodiscard]] static GLenum BaseFormatFromImageFormat(gfx::Image::Format format);
        [[nodiscard]] static GLenum DataTypeFromImageFormat(gfx::Image::Format format);
    private:
        GLuint _id = 0;
    };
}
