//
// Created by radue on 2/18/2026.
//

#pragma once
#include <GL/glew.h>

#include <image.h>


namespace gfx::ogl {
    class Image : public gfx::Image {
    public:
        explicit Image(const gfx::Image::Builder& createInfo);
        ~Image() override;

        GLuint operator*() const { return _id; }

        void GenerateMipmaps() override;
        std::vector<std::byte> ReadData(glm::u32 mipLevel, glm::u32 arrayLayer) const override;
        void CopyFrom(const gfx::Buffer& buffer, glm::u32 mipLevel, glm::u32 layer) const override;

        GLenum getGLFormat() const { return InternalFormatFromImageFormat(_format); }

        [[nodiscard]] static GLenum InternalFormatFromImageFormat(gfx::Image::Format format);
        [[nodiscard]] static GLenum BaseFormatFromImageFormat(gfx::Image::Format format);
        [[nodiscard]] static GLenum DataTypeFromImageFormat(gfx::Image::Format format);
    private:
        GLuint _id = 0;
    };
}
