//
// Created by radue on 2/21/2026.
//

#include "framebuffer.h"

#include "imageView.h"
#include "utils/ogl_err_handling.h"

namespace gfx::ogl
{
    Framebuffer::Framebuffer()
    {
        // The default framebuffer
        _id = 0;
        clearValues.clearColor.emplace_back(glm::vec4 { 0.0, 0.0, 0.0, 1.0 });
    }

    Framebuffer::Framebuffer(const gfx::Framebuffer::Builder& createInfo) : gfx::Framebuffer(createInfo) {
        glCreateFramebuffers(1, &_id);
        glCheckError();


        std::vector<GLenum> drawAttachments {};
        for (size_t i = 0; i < colorAttachments.size(); ++i) {
            const std::reference_wrapper imageView = dynamic_cast<const ImageView&>(colorAttachments[i].get());
            glNamedFramebufferTexture(_id, GL_COLOR_ATTACHMENT0 + i, *imageView.get(), 0);
            drawAttachments.emplace_back(GL_COLOR_ATTACHMENT0 + i);
            glCheckError();
        }
        if (depthStencilAttachment) {
            const std::reference_wrapper imageView = dynamic_cast<const ImageView&>(depthStencilAttachment->get());
            glNamedFramebufferTexture(_id, GL_DEPTH_STENCIL_ATTACHMENT, *imageView.get(), 0);
            glCheckError();
        }
        glNamedFramebufferDrawBuffers(_id, drawAttachments.size(), drawAttachments.data());
        glCheckError();

    }

    Framebuffer::~Framebuffer()
    {
        if (_id != 0)
            glDeleteFramebuffers(1, &_id);
    }

    void Framebuffer::Bind() const {
        glBindFramebuffer(GL_FRAMEBUFFER, _id);
    }

    void Framebuffer::Unbind() const
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    bool Framebuffer::hasDepthStencilAttachment() const
    {
        return gfx::Framebuffer::hasDepthStencilAttachment() || _id == 0;
    }
}
