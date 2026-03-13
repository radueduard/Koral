//
// Created by radue on 2/21/2026.
//

#include "framebuffer.h"
#include "impl/open_gl/framebuffer.h"
#include "impl/vulkan/framebuffer.h"

#include <ranges>

#include "context.h"
#include "scheduler.h"
#include "io/window.h"

namespace gfx {
    Framebuffer::Builder& Framebuffer::Builder::addColorAttachment(const ImageView& imageView, glm::vec4 clearColor)
    {
        colorAttachments.emplace_back(imageView);
        clearValues.clearColor.emplace_back(clearColor);
        return *this;
    }

    Framebuffer::Builder& Framebuffer::Builder::setDepthStencilAttachment(const ImageView& imageView, float depth, glm::i32 stencil)
    {
        depthStencilAttachment = imageView;
        clearValues.clearDepth = depth;
        clearValues.clearStencil = stencil;
        return *this;
    }

    std::unique_ptr<Framebuffer> Framebuffer::Builder::build() const
    {
        switch (Context::Window().getAPI()) {
        case API::eOpenGL:
            return std::make_unique<ogl::Framebuffer>(*this);
        case API::eVulkan:
            return std::make_unique<vk::Framebuffer>(*this);
        default:
            throw std::runtime_error("Unknown graphics API!");
        }
    }

    std::unique_ptr<Framebuffer> Framebuffer::CreateDefault() {
        switch (Context::Window().getAPI()) {
        case API::eOpenGL:
            return std::make_unique<ogl::Framebuffer>();
        case API::eVulkan:
            return std::make_unique<vk::Framebuffer>();
        default:
            throw std::runtime_error("Unknown graphics API!");
        }
    }

    bool Framebuffer::hasDepthStencilAttachment() const
    {
        return _depthStencilAttachment.has_value();
    }

    const std::vector<std::reference_wrapper<const ImageView>>& Framebuffer::getColorAttachments() const
    {
        return _colorAttachments;
    }

    const ImageView& Framebuffer::getDepthStencilAttachment() const
    {
        return _depthStencilAttachment.has_value() ? _depthStencilAttachment.value().get() : throw std::runtime_error("This framebuffer does not have a depth stencil attachment!");
    }

    const glm::vec4& Framebuffer::getClearColor(const glm::u32 index) const
    {
        return _clearValues.clearColor[index];
    }

    float Framebuffer::getClearDepth() const
    {
        return _clearValues.clearDepth;
    }

    glm::i32 Framebuffer::getClearStencil() const
    {
        return _clearValues.clearStencil;
    }

    Framebuffer::Framebuffer(const Builder& createInfo) :
        _colorAttachments(createInfo.colorAttachments),
        _depthStencilAttachment(createInfo.depthStencilAttachment),
        _clearValues(createInfo.clearValues)
    {
        _extent = _colorAttachments.empty() ? _depthStencilAttachment.has_value() ? _depthStencilAttachment.value().get().getImage().getExtent() : glm::uvec2{ 0, 0 } : _colorAttachments[0].get().getImage().getExtent();
    }
}
