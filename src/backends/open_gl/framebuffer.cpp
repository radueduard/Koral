//
// Created by radue on 2/21/2026.
//

#include "framebuffer.h"

#include <ranges>
#include <scheduler.h>

#include "imageView.h"
#include "ogl_err_handling.h"

namespace gfx::ogl
{
    Framebuffer::Framebuffer() : _id(0)
    {
        _isDefault = true;
        _clearValues.clearColor.emplace_back(glm::vec4 { 0.0, 0.0, 0.0, 1.0 });
    }

    Framebuffer::Framebuffer(const gfx::Framebuffer::Builder& createInfo) : gfx::Framebuffer(createInfo) {
        glCreateFramebuffers(1, &_id);
        glCheckError();


        std::vector<GLenum> drawAttachments {};
        for (glm::uint i = 0; i < _colorAttachments.size(); ++i) {
            const std::reference_wrapper imageView = dynamic_cast<const ImageView&>(_colorAttachments[i].get());
            glNamedFramebufferTexture(_id, GL_COLOR_ATTACHMENT0 + i, *imageView.get(), 0);
            drawAttachments.emplace_back(GL_COLOR_ATTACHMENT0 + i);
            glCheckError();
        }
        if (_depthStencilAttachment.has_value()) {
            const auto& imageView = dynamic_cast<const ImageView&>(_depthStencilAttachment->get());
            glNamedFramebufferTexture(_id, GL_DEPTH_STENCIL_ATTACHMENT, *imageView, 0);
            glCheckError();
        }
        glNamedFramebufferDrawBuffers(_id, drawAttachments.size(), drawAttachments.data());
        glCheckError();
    }

    Framebuffer::~Framebuffer()
    {
        glDeleteFramebuffers(1, &_id);
        glCheckError();
    }

    GLuint Framebuffer::operator*() const
    {
        if (_isDefault) {
            return 0;
        }
        return _id;
    }

    void Framebuffer::Bind() const {
        if (_isDefault) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        } else
        {
            glBindFramebuffer(GL_FRAMEBUFFER, _id);
        }
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
