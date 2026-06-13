//
// Created by radue on 2/20/2026.
//

module;

#include <GL/glew.h>

export module gfx:ogl_imageView;
import :ogl_types;

import :imageView;

namespace gfx::ogl
{
    class ImageView : public gfx::ImageView
    {
    public:
        explicit ImageView(const Builder& createInfo);
        ~ImageView() override = default;

        GLuint operator*() const;

        [[nodiscard]] GLenum getFormat() const;

    private:
        GLuint _textureViewID;
    };
}

