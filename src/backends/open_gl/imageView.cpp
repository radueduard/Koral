//
// Created by radue on 2/20/2026.
//

#include "imageView.h"
#include "image.h"

namespace gfx::ogl
{
    ImageView::ImageView(const Builder& createInfo) : gfx::ImageView(createInfo) {}

    GLuint ImageView::operator*() const
    {
        return *reinterpret_cast<const gfx::ogl::Image&>(_image);
    }

    GLenum ImageView::getFormat() const
    {
        return reinterpret_cast<const gfx::ogl::Image&>(_image).getGLFormat();
    }
}
