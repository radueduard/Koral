//
// Created by radue on 2/28/2026.
//

#include "framebuffer.h"

#include "scheduler.h"
#include "vulkanContext.h"
#include "io/window.h"

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
        isDefault = true;
        _extent = gfx::Context::Window().getExtent();

        // Default framebuffer has one color attachment which is the swap chain image, and one depth stencil attachment which is the depth image of the swap chain.
        const auto& scheduler = dynamic_cast<const vk::Scheduler&>(gfx::Context::Scheduler());

        auto colorAttachments = scheduler.getSwapChain().getSwapChainImageViews();
        auto depthStencilAttachments = scheduler.getSwapChain().getDepthImageViews();

        attachments.resize(scheduler.getImageCount());
        for (glm::u32 i = 0; i < scheduler.getImageCount(); ++i) {
            attachments[i].colorAttachments.emplace_back(colorAttachments[i]);
            attachments[i].depthStencilAttachment = depthStencilAttachments[i];
        }
        clearValues.clearColor.emplace_back( 0.0, 0.0, 0.0, 1.0 );
        clearValues.clearDepth = 1.0f;
        clearValues.clearStencil = 0;
    }

    Framebuffer::Framebuffer(const Framebuffer::Builder& builder) : gfx::Framebuffer(builder)
    {

    }

    Framebuffer::~Framebuffer() = default;
}
