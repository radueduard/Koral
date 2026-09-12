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

        void doResize(const glm::uvec3 &extent) override;

        GLenum getGLFormat() const { return InternalFormatFromImageFormat(_format); }

        // Whether the driver has this format at all, asked through glGetInternalformativ rather
        // than assumed from an extension string. @see kor::Image::isFormatSupported
        [[nodiscard]] static bool isFormatSupported(kor::Image::Format format, Flags<kor::Image::Usage> usage);

        [[nodiscard]] static GLenum InternalFormatFromImageFormat(kor::Image::Format format);
        [[nodiscard]] static GLenum BaseFormatFromImageFormat(kor::Image::Format format);
        [[nodiscard]] static GLenum DataTypeFromImageFormat(kor::Image::Format format);
    private:
        GLuint _id = 0;
    };
}
