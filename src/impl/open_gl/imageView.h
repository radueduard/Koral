//
// Created by radue on 2/20/2026.
//

#pragma once
#include <GL/glew.h>

#include "image.h"
#include "../../core/imageView.h"

namespace gfx::ogl
{
    class ImageView : public gfx::ImageView
    {
    public:
        explicit ImageView(const Builder& createInfo);
        ~ImageView() override = default;

        GLuint operator*() const { return *_image; }

        [[nodiscard]] GLenum getFormat() const { return _image.getGLFormat(); }

    private:
        const Image& _image;
    };
}

