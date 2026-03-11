//
// Created by radue on 2/21/2026.
//

#include "framebuffer.h"

#include <ranges>

#include "imageView.h"
#include "core/scheduler.h"
#include "utils/ogl_err_handling.h"

namespace gfx::ogl
{
    Framebuffer::Framebuffer()
    {
        isDefault = true;
        clearValues.clearColor.emplace_back(glm::vec4 { 0.0, 0.0, 0.0, 1.0 });
    }

    Framebuffer::Framebuffer(const gfx::Framebuffer::Builder& createInfo) : gfx::Framebuffer(createInfo) {
        _ids.resize(_frameCount);
        glCreateFramebuffers(_frameCount, _ids.data());
        glCheckError();

        for (const auto& [i, perAttachmentInfo] : attachments | std::views::enumerate) {
            const auto& colorAttachments = perAttachmentInfo.colorAttachments;
            const auto& depthStencilAttachment = perAttachmentInfo.depthStencilAttachment;

            std::vector<GLenum> drawAttachments {};
            for (glm::uint i = 0; i < colorAttachments.size(); ++i) {
                const std::reference_wrapper imageView = dynamic_cast<const ImageView&>(colorAttachments[i].get());
                glNamedFramebufferTexture(_ids[i], GL_COLOR_ATTACHMENT0 + i, *imageView.get(), 0);
                drawAttachments.emplace_back(GL_COLOR_ATTACHMENT0 + i);
                glCheckError();
            }
            if (depthStencilAttachment) {
                const std::reference_wrapper imageView = dynamic_cast<const ImageView&>(depthStencilAttachment->get());
                glNamedFramebufferTexture(_ids[i], GL_DEPTH_STENCIL_ATTACHMENT, *imageView.get(), 0);
                glCheckError();
            }
            glNamedFramebufferDrawBuffers(_ids[i], drawAttachments.size(), drawAttachments.data());
            glCheckError();
        }
    }

    Framebuffer::~Framebuffer()
    {
        if (!_ids.empty())
            glDeleteFramebuffers(1, _ids.data());
    }

    GLuint Framebuffer::operator*() const
    {
        if (isDefault) {
            return 0;
        }
        return _ids[Context::Scheduler().getCurrentImageIndex()];
    }

    void Framebuffer::Bind() const {
        if (isDefault) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        } else
        {
            glBindFramebuffer(GL_FRAMEBUFFER, _ids[Context::Scheduler().getCurrentImageIndex()]);
        }
    }

    void Framebuffer::Unbind() const
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    bool Framebuffer::hasDepthStencilAttachment() const
    {
        return gfx::Framebuffer::hasDepthStencilAttachment() || _ids.empty();
    }
}
