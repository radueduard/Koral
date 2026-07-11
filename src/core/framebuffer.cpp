//
// Created by radue on 2/21/2026.
//

#include <framebuffer.h>
#include <scheduler.h>
#include <context.h>
#include <window.h>
#include <surface.h>

#include "../backends/open_gl/framebuffer.h"
#include "../backends/vulkan/framebuffer.h"

#include "image.h"
#include "imageView.h"

namespace gfx {
    Framebuffer::Builder& Framebuffer::Builder::addColorAttachment(ResourceRef<const ImageView> imageView, ClearColor clearColor)
    {
        colorAttachments.emplace_back(*imageView);
        clearValues.clearColor.emplace_back(clearColor);
        const auto extent2D = glm::uvec2{ imageView->getImage()->getExtent().x, imageView->getImage()->getExtent().y };
        if (!extent) extent = extent2D;
        if (!sampleCount) sampleCount = imageView->getImage()->getSampleCount();
        return *this;
    }

    Framebuffer::Builder& Framebuffer::Builder::addColorAttachment(ResourceRef<const ImageView> imageView, ResourceRef<const ImageView> resolveView, ClearColor clearColor)
    {
        colorAttachments.emplace_back(*imageView);
        if (!colorResolveAttachments) colorResolveAttachments = std::vector<std::reference_wrapper<const ImageView>>{};
        colorResolveAttachments->emplace_back(*resolveView);
        clearValues.clearColor.emplace_back(clearColor);
        const auto extent2D = glm::uvec2{ imageView->getImage()->getExtent().x, imageView->getImage()->getExtent().y };
        if (!extent) extent = extent2D;
        if (!sampleCount) sampleCount = imageView->getImage()->getSampleCount();
        if (resolveMode == ResolveMode::eNone) resolveMode = ResolveMode::eAverage;
        return *this;
    }

    Framebuffer::Builder& Framebuffer::Builder::setDepthAttachment(ResourceRef<const ImageView> imageView, float depth)
    {
        depthAttachment = std::cref(*imageView);
        clearValues.clearDepth = depth;
        const auto extent2D = glm::uvec2{ imageView->getImage()->getExtent().x, imageView->getImage()->getExtent().y };
        if (!extent) extent = extent2D;
        if (!sampleCount) sampleCount = imageView->getImage()->getSampleCount();
        return *this;
    }

    Framebuffer::Builder& Framebuffer::Builder::setDepthAttachment(ResourceRef<const ImageView> imageView, ResourceRef<const ImageView> resolveView, float depth)
    {
        depthAttachment = std::cref(*imageView);
        depthResolveAttachment = std::cref(*resolveView);
        clearValues.clearDepth = depth;
        const auto extent2D = glm::uvec2{ imageView->getImage()->getExtent().x, imageView->getImage()->getExtent().y };
        if (!extent) extent = extent2D;
        if (!sampleCount) sampleCount = imageView->getImage()->getSampleCount();
        if (resolveMode == ResolveMode::eNone) resolveMode = ResolveMode::eAverage;
        return *this;
    }

    Framebuffer::Builder& Framebuffer::Builder::setStencilAttachment(ResourceRef<const ImageView> imageView, glm::i32 stencil)
    {
        stencilAttachment = std::cref(*imageView);
        clearValues.clearStencil = stencil;
        const auto extent2D = glm::uvec2{ imageView->getImage()->getExtent().x, imageView->getImage()->getExtent().y };
        if (!extent) extent = extent2D;
        if (!sampleCount) sampleCount = imageView->getImage()->getSampleCount();
        return *this;
    }

    Framebuffer::Builder& Framebuffer::Builder::setStencilAttachment(ResourceRef<const ImageView> imageView, ResourceRef<const ImageView> resolveView, glm::i32 stencil)
    {
        stencilAttachment = std::cref(*imageView);
        stencilResolveAttachment = std::cref(*resolveView);
        clearValues.clearStencil = stencil;
        const auto extent2D = glm::uvec2{ imageView->getImage()->getExtent().x, imageView->getImage()->getExtent().y };
        if (!extent) extent = extent2D;
        if (!sampleCount) sampleCount = imageView->getImage()->getSampleCount();
        if (resolveMode == ResolveMode::eNone) resolveMode = ResolveMode::eAverage;
        return *this;
    }

    Framebuffer::Builder& Framebuffer::Builder::setDepthStencilAttachment(ResourceRef<const ImageView> imageView, float depth, glm::i32 stencil)
    {
        setDepthAttachment(imageView, depth);
        setStencilAttachment(imageView, stencil);
        return *this;
    }

    Framebuffer::Builder& Framebuffer::Builder::setDepthStencilAttachment(ResourceRef<const ImageView> imageView, ResourceRef<const ImageView> resolveView, float depth, glm::i32 stencil)
    {
        setDepthAttachment(imageView, resolveView, depth);
        setStencilAttachment(imageView, resolveView, stencil);
        return *this;
    }

    Framebuffer::Builder& Framebuffer::Builder::setResolveMode(ResolveMode mode)
    {
        resolveMode = mode;
        return *this;
    }

    Result<std::unique_ptr<Framebuffer>> Framebuffer::Builder::create() const
    {
        beginAttempt();

        // Attachments are adopted as they are set, not here: this builder stores plain references
        // to its image views, so it must inspect each one at the moment it is handed over.
        if (auto v = validate(); !v) return std::unexpected(v.error());

        const auto api = Context::activeAPI();
        if (api != API::eOpenGL && api != API::eVulkan)
            return fail(ErrorCode::eUnknownApi, "Unknown graphics API!");

        return guard(ErrorCode::eBackend, [&]() -> std::unique_ptr<Framebuffer> {
            return (api == API::eVulkan)
                ? MakeBackendPtr<Framebuffer, vk::Framebuffer>(*this)
                : MakeBackendPtr<Framebuffer, ogl::Framebuffer>(*this);
        });
    }

    gfx::Resource<Framebuffer> Framebuffer::Builder::build() const
    {
        return materialize<Framebuffer>(*this, "Framebuffer");
    }

    gfx::Resource<Framebuffer> Framebuffer::CreateDefault() {
        switch (Context::activeAPI()) {
        case API::eOpenGL:
            return gfx::MakeBackendResource<Framebuffer, ogl::Framebuffer>();
        case API::eVulkan:
            return gfx::MakeBackendResource<Framebuffer, vk::Framebuffer>();
        default:
            throw std::runtime_error("Unknown graphics API!");
        }
    }

    glm::u32 Framebuffer::getColorAttachmentCount() const { return static_cast<glm::u32>(_colorAttachments.size()); }
    SampleCount Framebuffer::getSampleCount() const { return _sampleCount; }

    const std::vector<std::reference_wrapper<const ImageView>>& Framebuffer::getColorAttachments() const
    {
        return _colorAttachments;
    }

    bool Framebuffer::hasDepthAttachment() const { return _depthAttachment.has_value(); }

    const ImageView& Framebuffer::getDepthAttachment() const
    {
        if (!_depthAttachment)
            throw std::runtime_error("Framebuffer does not have a depth attachment!");
        return _depthAttachment.value().get();
    }

    bool Framebuffer::hasStencilAttachment() const { return _stencilAttachment.has_value(); }

    const ImageView& Framebuffer::getStencilAttachment() const
    {
        if (!_stencilAttachment)
            throw std::runtime_error("Framebuffer does not have a stencil attachment!");
        return _stencilAttachment.value().get();
    }

    bool Framebuffer::hasResolveAttachments() const
    {
        return _colorResolveAttachments.has_value() || _depthResolveAttachment.has_value() || _stencilResolveAttachment.has_value();
    }

    const ImageView& Framebuffer::getResolveAttachment(glm::u32 index) const
    {
        if (!_colorResolveAttachments)
            throw std::runtime_error("Framebuffer does not have color resolve attachments!");
        return (*_colorResolveAttachments)[index].get();
    }

    const ClearColor& Framebuffer::getClearColor(const glm::u32 index) const
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

    ResolveMode Framebuffer::getResolveMode() const { return _resolveMode; }

    Framebuffer::Framebuffer(const Builder& createInfo) :
        _colorAttachments(createInfo.colorAttachments),
        _colorResolveAttachments(createInfo.colorResolveAttachments),
        _depthAttachment(createInfo.depthAttachment),
        _depthResolveAttachment(createInfo.depthResolveAttachment),
        _stencilAttachment(createInfo.stencilAttachment),
        _stencilResolveAttachment(createInfo.stencilResolveAttachment),
        _clearValues(createInfo.clearValues),
        _resolveMode(createInfo.resolveMode)
    {
        _isDefault = false;
        if (createInfo.sampleCount)
            _sampleCount = createInfo.sampleCount.value();
        if (createInfo.extent)
            _extent = createInfo.extent.value();
        else {
            _extent = _colorAttachments.empty() ?
                (_depthAttachment.has_value() ? glm::uvec2{ _depthAttachment->get().getImage()->getExtent().x, _depthAttachment->get().getImage()->getExtent().y } : glm::uvec2{ 0, 0 }) :
                glm::uvec2{ _colorAttachments[0].get().getImage()->getExtent().x, _colorAttachments[0].get().getImage()->getExtent().y };
        }
    }
}
