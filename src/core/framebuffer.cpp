//
// Created by radue on 2/21/2026.
//

#include "framebuffer.h"
#include "impl/open_gl/framebuffer.h"
#include "impl/vulkan/framebuffer.h"

#include <algorithm>
#include <ranges>

#include "context.h"
#include "framebufferImage.h"
#include "scheduler.h"
#include "io/window.h"

namespace gfx {
    Framebuffer::Builder::Builder()
    {
        attachments.resize(Context::Scheduler().getImageCount());
    }

    Framebuffer::Builder& Framebuffer::Builder::addColorAttachment(const FramebufferImage& framebufferImage, glm::vec4 clearColor)
    {
        for (const auto& [i, imageView] : framebufferImage.getImageViews() | std::views::enumerate) {
            attachments[i].colorAttachments.emplace_back(imageView);
        }
        clearValues.clearColor.emplace_back(clearColor);
        return *this;
    }

    Framebuffer::Builder& Framebuffer::Builder::setDepthStencilAttachment(const FramebufferImage& framebufferImage, float depth, glm::i32 stencil)
    {
        for (const auto& [i, imageView] : framebufferImage.getImageViews() | std::views::enumerate) {
            attachments[i].depthStencilAttachment = imageView;
        }
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
            throw std::runtime_error("Vulkan is not supported yet!");
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
        return std::ranges::all_of(attachments, [](const auto& attachmentInfo) {
            return attachmentInfo.depthStencilAttachment.has_value();
        });
    }

    const std::vector<std::reference_wrapper<const ImageView>>& Framebuffer::getColorAttachments() const
    {
        return attachments[Context::Scheduler().getCurrentImageIndex()].colorAttachments;
    }

    const ImageView& Framebuffer::getDepthStencilAttachment() const
    {
        return attachments[Context::Scheduler().getCurrentImageIndex()].depthStencilAttachment->get();
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

    Framebuffer::Framebuffer(const Builder& createInfo) :
        _frameCount(createInfo.attachments.size()),
        attachments(createInfo.attachments),
        clearValues(createInfo.clearValues)
    {
        _extent = attachments[0].colorAttachments[0].get().getImage().getExtent();
    }
}
