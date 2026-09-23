//
// Created by radue on 12.09.2026.
//

#pragma once
#include <GL/glew.h>

#include <bufferView.h>

namespace kor::ogl
{
    /**
     * @brief A texel buffer, which GL has no object for: it is a GL_TEXTURE_BUFFER texture whose
     *        storage is the buffer.
     *
     * So binding a view is binding that texture — to a texture unit for a `samplerBuffer`, or to an
     * image unit for an `imageBuffer` — and the format the shader fetches lives on the texture
     * rather than on the buffer.
     */
    class BufferView final : public kor::BufferView
    {
    public:
        explicit BufferView(const Builder& createInfo);
        ~BufferView() override;

        BufferView(const BufferView&) = delete;
        BufferView& operator=(const BufferView&) = delete;

        GLuint operator*() const { return _textureID; }

        /**
         * @brief The sized internal format the texture was attached with.
         *
         * Named apart from kor::BufferView::format() on purpose: that one answers in
         * kor::Image::Format, and a getter that hides it with a different return type is the kind
         * of thing that compiles and then binds the wrong enum.
         */
        [[nodiscard]] GLenum getGLFormat() const;

    private:
        GLuint _textureID = 0;
    };
}
