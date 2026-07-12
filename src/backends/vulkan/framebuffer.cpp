//
// Created by radue on 2/28/2026.
//

#include "framebuffer.h"

#include "scheduler.h"
#include <window.h>
#include <framebuffer.h>
#include <surface.h>

namespace kor::vk
{
    void Framebuffer::Bind() const
    {
    }

    void Framebuffer::Unbind() const
    {
    }

    Framebuffer::Framebuffer()
    {
        _isDefault = true;
        _extent = kor::Context::Window().getExtent();

        // Default framebuffer has one color attachment which is the swap chain image, and one depth stencil attachment which is the depth image of the swap chain.
        const auto& scheduler = dynamic_cast<const vk::Scheduler&>(kor::Context::Scheduler());

        auto colorAttachment = scheduler.getSwapChain().getSwapChainImageViews();
        auto depthStencilAttachment = scheduler.getSwapChain().getDepthImageViews();

        _colorAttachments.emplace_back(colorAttachment);
        _depthAttachment = depthStencilAttachment;
        _stencilAttachment = depthStencilAttachment;
        _clearValues.clearColor.emplace_back(glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
        _clearValues.clearDepth = 1.0f;
        _clearValues.clearStencil = 0;
    }

    Framebuffer::Framebuffer(const Framebuffer::Builder& builder) : kor::Framebuffer(builder) {}
    Framebuffer::~Framebuffer() = default;

    void Framebuffer::Resize(const glm::uvec2& newExtent) const
    {
        kor::Framebuffer::Resize(newExtent);

        if (_isDefault)
        {
            const auto& scheduler = dynamic_cast<const vk::Scheduler&>(kor::Context::Scheduler());
            auto colorAttachment = scheduler.getSwapChain().getSwapChainImageViews();
            auto depthStencilAttachment = scheduler.getSwapChain().getDepthImageViews();

            _extent = newExtent;
            _colorAttachments.clear();
            _colorAttachments.emplace_back(colorAttachment);
            _depthAttachment = depthStencilAttachment;
            _stencilAttachment = depthStencilAttachment;
        }
    }
}
