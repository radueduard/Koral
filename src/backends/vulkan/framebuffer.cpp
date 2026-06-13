//
// Created by radue on 2/28/2026.
//

module;

#include <glm/glm.hpp>

module gfx;
import :vk_framebuffer;
import :vk_scheduler;
import :vk_swapChain;

namespace gfx::vk
{
    Framebuffer::Framebuffer()
    {
        _isDefault = true;
        _extent = gfx::Context::GetWindow().getExtent();

        // Default framebuffer has one color attachment which is the swap chain image, and one depth stencil attachment which is the depth image of the swap chain.
        const auto& scheduler = dynamic_cast<const vk::Scheduler&>(gfx::Context::GetScheduler());

        auto colorAttachment = scheduler.getSwapChain().getSwapChainImageViews();
        auto depthStencilAttachment = scheduler.getSwapChain().getDepthImageViews();

        _colorAttachments.emplace_back(colorAttachment);
        _depthAttachment = depthStencilAttachment;
        _clearValues.clearColor.emplace_back(glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
        _clearValues.clearDepth = 1.0f;
    }

    Framebuffer::Framebuffer(const Framebuffer::Builder& builder) : gfx::Framebuffer(builder) {}
    Framebuffer::~Framebuffer() = default;

    void Framebuffer::Resize(const glm::uvec2& newExtent) const
    {
        gfx::Framebuffer::Resize(newExtent);

        if (_isDefault)
        {
            const auto& scheduler = dynamic_cast<const vk::Scheduler&>(gfx::Context::GetScheduler());
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
