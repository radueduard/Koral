//
// Created by radue on 2/18/2026.
//

#pragma once

#include <GL/glew.h>

#include "core/resources/buffer.h"
#include "core/resources/image.h"

namespace gfx::ogl {
    class Image : public gfx::Image {
    public:
        explicit Image(const gfx::Image::CreateInfo& createInfo);
        ~Image() override;

        GLuint operator*() const { return _id; }

        std::vector<std::byte> ReadData(glm::u32 mipLevel, glm::u32 arrayLayer) const override;
        void CopyFrom(const gfx::Buffer& buffer) override;

        GLenum getGLFormat() const { return InternalFormatFromImageFormat(format); }

    private:
        GLuint _id = 0;
        [[nodiscard]] static GLenum InternalFormatFromImageFormat(gfx::Image::Format format);
        [[nodiscard]] static GLenum BaseFormatFromImageFormat(gfx::Image::Format format);
        [[nodiscard]] static GLenum DataTypeFromImageFormat(gfx::Image::Format format);
        [[nodiscard]] static glm::u32 PixelSizeFromImageFormat(gfx::Image::Format format);
        [[nodiscard]] static glm::u32 ChannelCountFromImageFormat(gfx::Image::Format format);
    };
}
