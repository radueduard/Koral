//
// Created by radue on 2/20/2026.
//

#pragma once
#include <GL/glew.h>

#include "image.h"
#include "core/resources/imageView.h"

namespace gfx::ogl
{
    class ImageView : public gfx::ImageView
    {
    public:
        ImageView(const Image& image, const CreateInfo& createInfo);
        ~ImageView() override = default;

        GLuint operator*() const { return *_image; }

        GLenum getFormat() const { return _image.getGLFormat(); }

    private:
        const Image& _image;
    };
}

