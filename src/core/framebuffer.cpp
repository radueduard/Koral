//
// Created by radue on 2/21/2026.
//

module;

#include <stdexcept>

module gfx.framebuffer;
import vk.framebuffer;
import ogl.framebuffer;
import gfx.log;
import gfx.imageView;

namespace gfx {
    Framebuffer::Builder& Framebuffer::Builder::addColorAttachment(ResourceRef<const ImageView> imageView, ClearColor clearColor)
    {
        if (colorResolveAttachments || depthResolveAttachment || stencilResolveAttachment) {
            gfx::log::error("You can not have some attachments with resolves and others without");
            throw;
        }
        if (sampleCount && imageView->getImage()->getSampleCount() != sampleCount) {
            gfx::log::error("All render attachments must have the same sample count");
            throw;
        }
        if (imageView->getViewType() != ImageView::Type::e2D) {
            gfx::log::error("Only 2D image views can be used as framebuffer attachments");
            throw;
        }
        const auto extent2D = glm::uvec2 { imageView->getImage()->getExtent().x, imageView->getImage()->getExtent().y };
        if (extent && extent.value() != extent2D) {
            gfx::log::error("All attachments must have the same extent");
            throw;
        }

        if (!extent) {
            extent = extent2D;
        }
        if (!sampleCount) {
            sampleCount = imageView->getImage()->getSampleCount();
        }
        colorAttachments.emplace_back(imageView);
        clearValues.clearColor.emplace_back(clearColor);
        return *this;
    }

    Framebuffer::Builder & Framebuffer::Builder::addColorAttachment(ResourceRef<const ImageView> imageView, ResourceRef<const ImageView> resolveView, ClearColor clearColor) {
        if (!colorResolveAttachments && !depthResolveAttachment && !stencilResolveAttachment && sampleCount) {
            gfx::log::error("You can not have some attachments with resolves and others without");
            throw;
        }
        if (resolveView->getImage()->getSampleCount() != SampleCount::e1) {
            gfx::log::error("Resolve attachments must have a sample count of 1");
            throw;
        }

        if (sampleCount && imageView->getImage()->getSampleCount() != sampleCount) {
            gfx::log::error("All render attachments must have the same sample count");
            throw;
        }
        if (imageView->getImage()->getSampleCount() == SampleCount::e1) {
            gfx::log::error("If you use resolve attachments, you can not have single sample remder attachments");
            throw;
        }
        if (imageView->getViewType() != ImageView::Type::e2D || resolveView->getViewType() != ImageView::Type::e2D) {
            gfx::log::error("Only 2D image views can be used as framebuffer attachments");
            throw;
        }
        if (imageView->getImage()->getExtent() != resolveView->getImage()->getExtent()) {
            gfx::log::error("Resolve attachments must have the same extent as their corresponding render attachment");
            throw;
        }
        const auto extent2D = glm::uvec2 { imageView->getImage()->getExtent().x, imageView->getImage()->getExtent().y };
        if (extent && extent.value() != extent2D) {
            gfx::log::error("All attachments must have the same extent");
            throw;
        }
        if (!extent) {
            extent = extent2D;
        }
        if (!sampleCount) {
            sampleCount = imageView->getImage()->getSampleCount();
        }
        colorAttachments.emplace_back(imageView);
        if (!colorResolveAttachments) {
            colorResolveAttachments = std::vector<ResourceRef<const ImageView>> {};
        }
        colorResolveAttachments->emplace_back(resolveView);
        clearValues.clearColor.emplace_back(clearColor);
        if (resolveMode == ResolveMode::eNone) {
            resolveMode = ResolveMode::eAverage;
        }
        return *this;
    }

    Framebuffer::Builder & Framebuffer::Builder::setDepthAttachment(ResourceRef<const ImageView> imageView, const float depth) {
        if (depthAttachment) {
            gfx::log::error("You have already set a depth attachment for this framebuffer!");
            throw;
        }
        if (colorResolveAttachments || stencilResolveAttachment) {
            gfx::log::error("You can not have some attachments with resolves and others without");
            throw;
        }

        if (imageView->getViewType() != ImageView::Type::e2D) {
            gfx::log::error("Only 2D image views can be used as framebuffer attachments");
            throw;
        }
        const auto extent2D = glm::uvec2 { imageView->getImage()->getExtent().x, imageView->getImage()->getExtent().y };
        if (extent && extent.value() != extent2D) {
            gfx::log::error("All attachments must have the same extent");
            throw;
        }
        if (sampleCount && imageView->getImage()->getSampleCount() != sampleCount) {
            gfx::log::error("All render attachments must have the same sample count");
            throw;
        }
        if (!sampleCount) {
            sampleCount = imageView->getImage()->getSampleCount();
        }
        if (!extent) {
            extent = extent2D;
        }
        depthAttachment = imageView;
        clearValues.clearDepth = depth;
        return *this;
    }

    Framebuffer::Builder & Framebuffer::Builder::setDepthAttachment(ResourceRef<const ImageView> imageView, ResourceRef<const ImageView> resolveView, const float depth) {
        if (depthAttachment) {
            gfx::log::error("You have already set a depth attachment for this framebuffer!");
            throw;
        }
        if (!colorResolveAttachments && !stencilResolveAttachment && sampleCount) {
            gfx::log::error("You can not have some attachments with resolves and others without");
            throw;
        }
        if (resolveView->getImage()->getSampleCount() != SampleCount::e1) {
            gfx::log::error("Resolve attachments must have a sample count of 1");
            throw;
        }
        if (imageView->getViewType() != ImageView::Type::e2D || resolveView->getViewType() != ImageView::Type::e2D) {
            gfx::log::error("Only 2D image views can be used as framebuffer attachments");
            throw;
        }
        if (imageView->getImage()->getExtent() != resolveView->getImage()->getExtent()) {
            gfx::log::error("Resolve attachments must have the same extent as their corresponding render attachment");
            throw;
        }
        const auto extent2D = glm::uvec2 { imageView->getImage()->getExtent().x, imageView->getImage()->getExtent().y };
        if (extent && extent.value() != extent2D) {
            gfx::log::error("All attachments must have the same extent");
            throw;
        }
        if (sampleCount && imageView->getImage()->getSampleCount() != sampleCount) {
            gfx::log::error("All render attachments must have the same sample count");
            throw;
        }
        if (!sampleCount) {
            sampleCount = imageView->getImage()->getSampleCount();
        }
        if (!extent) {
            extent = extent2D;
        }
        depthAttachment = imageView;
        depthResolveAttachment = resolveView;
        clearValues.clearDepth = depth;
        if (resolveMode == ResolveMode::eNone) {
            resolveMode = ResolveMode::eAverage;
        }
        return *this;
    }

    Framebuffer::Builder & Framebuffer::Builder::setStencilAttachment(ResourceRef<const ImageView> imageView, glm::i32 stencil) {
        if (stencilAttachment) {
            gfx::log::error("You have already set a stencil attachment for this framebuffer!");
            throw;
        }
        if (colorResolveAttachments || depthResolveAttachment) {
            gfx::log::error("You can not have some attachments with resolves and others without");
            throw;
        }
        if (imageView->getViewType() != ImageView::Type::e2D) {
            gfx::log::error("Only 2D image views can be used as framebuffer attachments");
            throw;
        }
        const auto extent2D = glm::uvec2 { imageView->getImage()->getExtent().x, imageView->getImage()->getExtent().y };
        if (extent && extent.value() != extent2D) {
            gfx::log::error("All attachments must have the same extent");
            throw;
        }
        if (sampleCount && imageView->getImage()->getSampleCount() != sampleCount) {
            gfx::log::error("All render attachments must have the same sample count");
            throw;
        }
        if (!sampleCount) {
            sampleCount = imageView->getImage()->getSampleCount();
        }
        if (!extent) {
            extent = extent2D;
        }
        depthAttachment = imageView;
        clearValues.clearStencil = stencil;
        return *this;
    }

    Framebuffer::Builder & Framebuffer::Builder::setStencilAttachment(ResourceRef<const ImageView> imageView, ResourceRef<const ImageView> resolveView, glm::i32 stencil) {
        if (stencilAttachment) {
            gfx::log::error("You have already set a stencil attachment for this framebuffer!");
            throw;
        }
        if (!colorResolveAttachments && !depthResolveAttachment && sampleCount) {
            gfx::log::error("You can not have some attachments with resolves and others without");
            throw;
        }
        if (imageView->getImage()->getSampleCount() == SampleCount::e1) {
            gfx::log::error("If you use resolve attachments, you can not have single sample remder attachments");
            throw;
        }
        if (resolveView->getImage()->getSampleCount() != SampleCount::e1) {
            gfx::log::error("Resolve attachments must have a sample count of 1");
            throw;
        }
        if (resolveView->getViewType() != ImageView::Type::e2D || imageView->getViewType() != ImageView::Type::e2D) {
            gfx::log::error("Only 2D image views can be used as framebuffer attachments");
            throw;
        }
        if (imageView->getImage()->getExtent() != resolveView->getImage()->getExtent()) {
            gfx::log::error("Resolve attachments must have the same extent as their corresponding render attachment");
            throw;
        }
        const auto extent2D = glm::uvec2 { imageView->getImage()->getExtent().x, imageView->getImage()->getExtent().y };
        if (extent && extent.value() != extent2D) {
            gfx::log::error("All attachments must have the same extent");
            throw;
        }
        if (sampleCount && imageView->getImage()->getSampleCount() != sampleCount) {
            gfx::log::error("All render attachments must have the same sample count");
            throw;
        }
        if (!sampleCount) {
            sampleCount = imageView->getImage()->getSampleCount();
        }
        if (!extent) {
            extent = extent2D;
        }
        depthAttachment = imageView;
        stencilResolveAttachment = resolveView;
        clearValues.clearStencil = stencil;
        if (resolveMode == ResolveMode::eNone) {
            resolveMode = ResolveMode::eAverage;
        }
        return *this;
    }

    Framebuffer::Builder& Framebuffer::Builder::setDepthStencilAttachment(ResourceRef<const ImageView> imageView, float depth, glm::i32 stencil)
    {
        setDepthAttachment(imageView, depth);
        setStencilAttachment(imageView, stencil);
        return *this;
    }

    Framebuffer::Builder & Framebuffer::Builder::setDepthStencilAttachment(ResourceRef<const ImageView> imageView, ResourceRef<const ImageView> resolveView, float depth, glm::i32 stencil) {
        setDepthAttachment(imageView, resolveView, depth);
        setStencilAttachment(imageView, resolveView, stencil);
        return *this;
    }

    Framebuffer::Builder & Framebuffer::Builder::setResolveMode(ResolveMode mode) {
        if (mode == ResolveMode::eNone && (colorResolveAttachments || depthResolveAttachment || stencilResolveAttachment)) {
            gfx::log::error("You can not set a resolve mode to eNone if you have added resolve attachments");
            throw;
        }
        if (mode != ResolveMode::eNone && !colorResolveAttachments && !depthResolveAttachment && !stencilResolveAttachment) {
            gfx::log::error("You set a resolve mode, but you have not added any resolve attachments");
            throw;
        }
        resolveMode = mode;
        return *this;
    }

    Resource<Framebuffer> Framebuffer::Builder::build() const
    {
        switch (Context::Window().getAPI()) {
        case API::eOpenGL:
            return MakeResource<ogl::Framebuffer>(*this);
        case API::eVulkan:
            return MakeResource<vk::Framebuffer>(*this);
        default:
            throw std::runtime_error("Unknown graphics API!");
        }
    }

    gfx::Resource<Framebuffer> Framebuffer::CreateDefault() {
        switch (Context::Window().getAPI()) {
        case API::eOpenGL:
            return gfx::MakeResource<ogl::Framebuffer>();
        case API::eVulkan:
            return gfx::MakeResource<vk::Framebuffer>();
        default:
            throw std::runtime_error("Unknown graphics API!");
        }
    }

    glm::u32 Framebuffer::getColorAttachmentCount() const { return static_cast<glm::u32>(_colorAttachments.size()); }
    SampleCount Framebuffer::getSampleCount() const { return _sampleCount; }
    const glm::uvec2 & Framebuffer::getExtent() const { return _extent; }

    ResourceRef<const ImageView> Framebuffer::getColorAttachment(glm::u32 index) const {
        if (index >= _colorAttachments.size()) {
            gfx::log::error("Color attachment index out of bounds!");
            throw;
        }
        return _colorAttachments[index];
    }

    const std::vector<ResourceRef<const ImageView>> & Framebuffer::getColorAttachments() const { return _colorAttachments; }

    bool Framebuffer::hasDepthAttachment() const { return _depthAttachment.has_value(); }

    ResourceRef<const ImageView> Framebuffer::getDepthAttachment() const
    {
        if (!_depthAttachment) {
            gfx::log::error("Framebuffer does not have a depth attachment!");
            throw;
        }
        return _depthAttachment.value();
    }

    bool Framebuffer::hasStencilAttachment() const { return _stencilAttachment.has_value(); }

    ResourceRef<const ImageView> Framebuffer::getStencilAttachment() const {
        if (!_stencilAttachment) {
            gfx::log::error("Framebuffer does not have a stencil attachment!");
            throw;
        }
        return _stencilAttachment.value();
    }

    bool Framebuffer::hasResolveAttachments() const {
        return _colorResolveAttachments || _depthResolveAttachment || _stencilResolveAttachment;
    }

    ResourceRef<const ImageView> Framebuffer::getResolveAttachment(glm::u32 index) const {
        if (!_colorResolveAttachments) {
            gfx::log::error("Framebuffer does not have resolve attachments!");
            throw;
        }
        if (index >= _colorResolveAttachments->size()) {
            gfx::log::error("Color resolve attachment index out of bounds!");
            throw;
        }
        return (*_colorResolveAttachments)[index];
    }

    std::vector<ResourceRef<const ImageView>> & Framebuffer::getColorResolveAttachments() const {
        if (!_colorResolveAttachments) {
            gfx::log::error("Framebuffer does not have resolve attachments!");
            throw;
        }
        return *_colorResolveAttachments;
    }

    ResourceRef<const ImageView> Framebuffer::getDepthResolveAttachment() const {
        if (!_depthResolveAttachment) {
            gfx::log::error("Framebuffer does not have resolve attachments!");
            throw;
        }
        return _depthResolveAttachment.value();
    }

    ResourceRef<const ImageView> Framebuffer::getStencilResolveAttachment() const {
        if (!_stencilResolveAttachment) {
            gfx::log::error("Framebuffer does not have resolve attachments!");
            throw;
        }
        return _stencilResolveAttachment.value();
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
        if (!createInfo.sampleCount) {
            gfx::log::error("No attachment added to the framebuffer!");
            throw;
        }
        _sampleCount = createInfo.sampleCount.value();
        _extent = createInfo.extent.value();
        _isDefault = false;
    }
}
