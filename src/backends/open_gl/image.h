//
// Created by radue on 2/18/2026.
//

#pragma once
#include <GL/glew.h>

#include <image.h>


namespace kor::ogl {
    class Image : public kor::Image {
    public:
        explicit Image(const kor::Image::Builder& createInfo);
        ~Image() override;

        GLuint operator*() const { return _id; }

        void Resize(const glm::uvec3 &extent) override;

        GLenum getGLFormat() const { return InternalFormatFromImageFormat(_format); }

        [[nodiscard]] static GLenum InternalFormatFromImageFormat(kor::Image::Format format);
        [[nodiscard]] static GLenum BaseFormatFromImageFormat(kor::Image::Format format);
        [[nodiscard]] static GLenum DataTypeFromImageFormat(kor::Image::Format format);
    private:
        GLuint _id = 0;
    };
}
