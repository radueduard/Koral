//
// Created by radue on 2/21/2026.
//

#include "framebuffer.h"

#include <ranges>

#include <image.h>
#include <scheduler.h>

#include "imageView.h"
#include "ogl_err_handling.h"

namespace kor::ogl
{
    Framebuffer::Framebuffer() : _id(0)
    {
        _isDefault = true;
        _clearValues.clearColor.emplace_back(glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
    }

    Framebuffer::Framebuffer(const kor::Framebuffer::Builder& createInfo) : kor::Framebuffer(createInfo) {
        glCreateFramebuffers(1, &_id);
        glCheckError();

        attachAll();
    }

    void Framebuffer::attachAll() const
    {
        std::vector<GLenum> drawAttachments {};
        _attachedColor.clear();
        for (glm::uint i = 0; i < _colorAttachments.size(); ++i) {
            const auto& imageView = dynamic_cast<const ImageView&>(*_colorAttachments[i].view);
            glNamedFramebufferTexture(_id, GL_COLOR_ATTACHMENT0 + i, *imageView, 0);
            _attachedColor.emplace_back(imageView.getImage()->generation());
            drawAttachments.emplace_back(GL_COLOR_ATTACHMENT0 + i);
            glCheckError();
        }
        _attachedDepth = 0;
        if (_depthAttachment.has_value()) {
            const auto& imageView = dynamic_cast<const ImageView&>(*_depthAttachment->view);
            glNamedFramebufferTexture(_id, GL_DEPTH_ATTACHMENT, *imageView, 0);
            _attachedDepth = imageView.getImage()->generation();
            glCheckError();
        }
        _attachedStencil = 0;
        if (_stencilAttachment.has_value() && (!_depthAttachment.has_value() || _stencilAttachment->view.get() != _depthAttachment->view.get())) {
            const auto& imageView = dynamic_cast<const ImageView&>(*_stencilAttachment->view);
            glNamedFramebufferTexture(_id, GL_STENCIL_ATTACHMENT, *imageView, 0);
            _attachedStencil = imageView.getImage()->generation();
            glCheckError();
        }
        glNamedFramebufferDrawBuffers(_id, static_cast<GLsizei>(drawAttachments.size()), drawAttachments.data());
        glCheckError();

        // Complain here, where the cause is, rather than leaving every later clear and draw to
        // return GL_INVALID_FRAMEBUFFER_OPERATION with nothing to point at.
        _attached = true;
        glCheckFramebufferComplete(_id);
    }

    void Framebuffer::Refresh() const
    {
        const auto generationOf = [](const auto& attachment) -> glm::u64 {
            return dynamic_cast<const ImageView&>(attachment).getImage()->generation();
        };

        bool stale = !_attached || _attachedColor.size() != _colorAttachments.size();
        for (glm::uint i = 0; !stale && i < _colorAttachments.size(); ++i)
            stale = _attachedColor[i] != generationOf(*_colorAttachments[i].view);
        if (!stale && _depthAttachment.has_value())
            stale = _attachedDepth != generationOf(*_depthAttachment->view);
        if (!stale && _stencilAttachment.has_value()
            && (!_depthAttachment.has_value() || _stencilAttachment->view.get() != _depthAttachment->view.get()))
            stale = _attachedStencil != generationOf(*_stencilAttachment->view);

        if (stale) attachAll();
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
            // Every render pass starts here, which makes it the one place guaranteed to run
            // between an attachment being resized and anything being drawn to it.
            Refresh();
            glBindFramebuffer(GL_FRAMEBUFFER, _id);
        }
    }

    void Framebuffer::Unbind() const
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    bool Framebuffer::hasDepthStencilAttachment() const
    {
        return kor::Framebuffer::hasDepthAttachment() || _id == 0;
    }
}
