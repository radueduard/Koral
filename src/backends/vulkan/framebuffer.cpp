//
// Created by radue on 2/28/2026.
//

#include "framebuffer.h"

#include "scheduler.h"
#include <window.h>
#include <framebuffer.h>
#include <surface.h>

namespace gfx::vk
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
        _extent = gfx::Context::Window().getExtent();

        // Default framebuffer has one color attachment which is the swap chain image, and one depth stencil attachment which is the depth image of the swap chain.
        const auto& scheduler = dynamic_cast<const vk::Scheduler&>(gfx::Context::Scheduler());

        auto colorAttachment = scheduler.getSwapChain().getSwapChainImageViews();
        auto depthStencilAttachment = scheduler.getSwapChain().getDepthImageViews();

        _colorAttachments.emplace_back(colorAttachment);
        _depthStencilAttachment = depthStencilAttachment;
        _clearValues.clearColor.emplace_back( 0.0, 0.0, 0.0, 1.0 );
        _clearValues.clearDepth = 1.0f;
        _clearValues.clearStencil = 0;
    }

    Framebuffer::Framebuffer(const Framebuffer::Builder& builder) : gfx::Framebuffer(builder) {}
    Framebuffer::~Framebuffer() = default;
}
