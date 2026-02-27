//
// Created by radue on 2/21/2026.
//

#include "framebuffer.h"
#include "impl/open_gl/resources/framebuffer.h"

#include "context.h"
#include "io/window.h"

namespace gfx {
    std::unique_ptr<Framebuffer> Framebuffer::Create(const CreateInfo& createInfo) {
        switch (Context::Window().getAPI()) {
        case API::OpenGL:
            return std::make_unique<ogl::Framebuffer>(createInfo);
        case API::Vulkan:
            throw std::runtime_error("Vulkan is not supported yet!");
        default:
            throw std::runtime_error("Unknown graphics API!");
        }
    }

    std::unique_ptr<Framebuffer> Framebuffer::CreateDefault() {
        switch (Context::Window().getAPI()) {
        case API::OpenGL:
            return std::make_unique<ogl::Framebuffer>();
        case API::Vulkan:
            throw std::runtime_error("Vulkan is not supported yet!");
        default:
            throw std::runtime_error("Unknown graphics API!");
        }
    }

    bool Framebuffer::hasDepthStencilAttachment() const
    {
        return depthStencilAttachment.has_value();
    }

    const std::vector<std::reference_wrapper<const ImageView>>& Framebuffer::getColorAttachments() const
    {
        return colorAttachments;
    }

    const ImageView& Framebuffer::getDepthStencilAttachment() const
    {
        return depthStencilAttachment.value();
    }

    const glm::vec4& Framebuffer::getClearColor(const glm::u32 index) const
    {
        return clearValues.clearColor[index];
    }

    float Framebuffer::getClearDepth() const
    {
        return clearValues.clearDepth;
    }

    glm::i32 Framebuffer::getClearStencil() const
    {
        return clearValues.clearStencil;
    }

    Framebuffer::Framebuffer(const CreateInfo& createInfo) :
        colorAttachments(createInfo.colorAttachments),
        depthStencilAttachment(createInfo.depthStencilAttachment),
        clearValues(createInfo.clearValues) {}
}
