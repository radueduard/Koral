//
// Created by radue on 2/20/2026.
//

#include "imageView.h"

#include <stdexcept>

#include "image.h"

namespace gfx::ogl
{
    ImageView::ImageView(const Builder& createInfo) : gfx::ImageView(createInfo) {
        glGenTextures(1, &_textureViewID);
        // _image is a ResourceRef handle; the backend image is behind it.
        const auto& image = dynamic_cast<const gfx::ogl::Image&>(*_image);

        GLenum target;
        switch (createInfo.type) {
            case Type::e1D:
                target = image.getArrayLayers() == 1 ? GL_TEXTURE_1D : GL_TEXTURE_1D_ARRAY;
                break;
            case Type::e2D:
                target = image.getArrayLayers() == 1
                    ? (image.getMSAA() == MSAA::eNone ? GL_TEXTURE_2D : GL_TEXTURE_2D_MULTISAMPLE)
                    : (image.getMSAA() == MSAA::eNone ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D_MULTISAMPLE_ARRAY);
                break;
            case Type::e3D:
                target = GL_TEXTURE_3D;
                break;
            default:
                throw std::runtime_error("Unsupported image type!");
        }

        glTextureView(
            _textureViewID,
            target,
            *image,
            gfx::ogl::Image::InternalFormatFromImageFormat(image.getFormat()),
            _baseMipLevel,
            _mipLevelCount,
            _baseArrayLayer,
            _arrayLayerCount);
    }

    GLuint ImageView::operator*() const
    {
        return *dynamic_cast<const gfx::ogl::Image&>(*_image);
    }

    GLenum ImageView::getFormat() const
    {
        return dynamic_cast<const gfx::ogl::Image&>(*_image).getGLFormat();
    }
}
