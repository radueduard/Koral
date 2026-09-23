//
// Created by radue on 12.09.2026.
//

#include "bufferView.h"

#include "buffer.h"
#include "image.h"
#include "ogl_err_handling.h"

namespace kor::ogl
{
    BufferView::BufferView(const Builder& createInfo) : kor::BufferView(createInfo)
    {
        const auto& buffer = dynamic_cast<const kor::ogl::Buffer&>(*_buffer);

        glCreateTextures(GL_TEXTURE_BUFFER, 1, &_textureID);
        glCheckError();

        // The range form rather than glTextureBuffer, so a view of part of a buffer works the same
        // way it does on Vulkan. The core builder has already checked that offset and range are
        // whole texels and inside the buffer.
        glTextureBufferRange(
            _textureID,
            getGLFormat(),
            *buffer,
            static_cast<GLintptr>(_offset),
            static_cast<GLsizeiptr>(_range));
        glCheckError();
    }

    BufferView::~BufferView()
    {
        if (_textureID != 0) {
            glDeleteTextures(1, &_textureID);
        }
    }

    GLenum BufferView::getGLFormat() const
    {
        return kor::ogl::Image::InternalFormatFromImageFormat(_format);
    }
}
