//
// Created by radue on 2/28/2026.
//

#include "framebuffer.h"

#include "scheduler.h"
#include "surface.h"
#include "swapChain.h"
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

    Framebuffer::Framebuffer(const kor::Window& window)
    {
        _isDefault = true;
        _extent = window.Extent();
        _swapChain = &dynamic_cast<const vk::Surface&>(window.RenderSurface()).swapChain();
        _extent = _swapChain->extent();
        attachSwapChain();
        _clearValues.clearColor.emplace_back(kor::Vec4(0.0f, 0.0f, 0.0f, 0.0f));
        _clearValues.clearDepth = 1.0f;
        _clearValues.clearStencil = 0;
    }

    void Framebuffer::attachSwapChain()
    {
        // One colour attachment, the swap chain's image, and its depth target for depth and stencil.
        // Named, like any other framebuffer's targets, so the image a frame is presented from is
        // reachable by `DefaultFramebuffer()->ImageNamed("color")` rather than only by index.
        // @see Framebuffer::ImageNamed
        const auto colorAttachment = _swapChain->getSwapChainImageViews();
        const auto depthStencilAttachment = _swapChain->getDepthImageViews();
        _colorAttachments.clear();
        _colorAttachments.push_back(Attachment{ colorAttachment, {}, "color" });
        _depthAttachment = Attachment{ depthStencilAttachment, {}, "depth" };
        _stencilAttachment = Attachment{ depthStencilAttachment, {}, "stencil" };
    }

    Framebuffer::Framebuffer(const Framebuffer::Builder& builder) : kor::Framebuffer(builder) {}
    Framebuffer::~Framebuffer() = default;

    void Framebuffer::DoResize(const kor::UVec2& newExtent)
    {
        // The base has already done the shared work (or skipped it, for the default framebuffer).
        if (_isDefault)
        {
            _extent = newExtent;
            attachSwapChain();
        }
    }
}
