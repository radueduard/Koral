//
// Created by radue on 2/20/2026.
//

#pragma once
#include <GL/glew.h>

#include <imageView.h>

namespace gfx::ogl
{
    class ImageView : public gfx::ImageView
    {
    public:
        explicit ImageView(const Builder& createInfo);
        ~ImageView() override = default;

        GLuint operator*() const;

        [[nodiscard]] GLenum getFormat() const;
    };
}

