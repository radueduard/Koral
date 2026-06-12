//
// Created by radue on 2/20/2026.
//

module;

#include <GL/glew.h>

export module ogl.imageView;
import gfx.imageView;

namespace gfx::ogl
{
    export class ImageView : public gfx::ImageView
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

