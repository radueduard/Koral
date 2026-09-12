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

#include <ranges>

namespace kor {

    void Framebuffer::Resize(const glm::uvec2& newExtent) const
    {
        if (newExtent.x == 0 || newExtent.y == 0) return;

        // The default framebuffer is the swap chain's, and the swap chain resizes its own images: the
        // backend override re-points this at the new ones. Resizing them from here would be resizing
        // images this framebuffer does not own.
        if (_isDefault) return;

        if (_extent == newExtent) return;
        _extent = newExtent;

        // Each attachment's image. Two attachments can name the same image — a depth and a stencil
        // attachment usually do — and Image::Resize is a no-op the second time round, since by then
        // the extent already matches.
        const auto resize = [&newExtent](const ResourceRef<const ImageView>& view) {
            if (!view.valid()) return;
            if (const auto image = view->getImage(); image.valid()) {
                const_cast<Image&>(*image).Resize({ newExtent.x, newExtent.y, 1 });
            }
        };

        const auto resizeAttachment = [&resize](const Attachment& attachment) {
            resize(attachment.view);
            resize(attachment.resolve);
        };

        for (const auto& attachment : _colorAttachments) resizeAttachment(attachment);
        if (_depthAttachment) resizeAttachment(*_depthAttachment);
        if (_stencilAttachment) resizeAttachment(*_stencilAttachment);
    }

    namespace {
        // The view an Image gives when it is handed straight to a framebuffer: its top mip level,
        // every array layer, shaped the way the image itself suggests. An attachment view must name
        // exactly one level — rendering into a view of several is invalid — which is what makes
        // this the eTopLevel coverage rather than the whole-image one a texture gets.
        ResourceRef<const ImageView> attachmentViewOf(const ResourceRef<const Image>& image)
        {
            if (!image.valid()) return {};
            return image->view(image->naturalShape(), Image::ViewCoverage::eTopLevel);
        }
    }

    // Extent and sample count are taken from the first attachment that can answer, and every later
    // one is expected to agree. An unusable attachment answers for neither, and is left to poison
    // the build through adopt() rather than being dereferenced here.
    void Framebuffer::Builder::adoptGeometry(const ResourceRef<const ImageView>& imageView)
    {
        if (!imageView.valid()) return;
        const auto image = imageView->getImage();
        if (!image.valid()) return;
        if (!extent) extent = glm::uvec2{ image->getExtent().x, image->getExtent().y };
        if (!sampleCount) sampleCount = image->getSampleCount();
    }

    Framebuffer::Builder& Framebuffer::Builder::addColorAttachment(ResourceRef<const ImageView> imageView, ClearColor clearColor)
    {
        return addColorAttachment(std::string_view{}, std::move(imageView), clearColor);
    }

    Framebuffer::Builder& Framebuffer::Builder::addColorAttachment(ResourceRef<const ImageView> imageView, ResourceRef<const ImageView> resolveView, ClearColor clearColor)
    {
        return addColorAttachment(std::string_view{}, std::move(imageView), std::move(resolveView), clearColor);
    }

    Framebuffer::Builder& Framebuffer::Builder::addColorAttachment(ResourceRef<const Image> image, ClearColor clearColor)
    {
        return addColorAttachment(std::string_view{}, attachmentViewOf(image), clearColor);
    }

    Framebuffer::Builder& Framebuffer::Builder::addColorAttachment(const std::string_view name, ResourceRef<const ImageView> imageView, ClearColor clearColor)
    {
        adoptGeometry(imageView);
        colorAttachments.push_back(Attachment{ std::move(imageView), {}, std::string(name) });
        clearValues.clearColor.emplace_back(clearColor);
        return *this;
    }

    Framebuffer::Builder& Framebuffer::Builder::addColorAttachment(const std::string_view name, ResourceRef<const ImageView> imageView, ResourceRef<const ImageView> resolveView, ClearColor clearColor)
    {
        adoptGeometry(imageView);
        colorAttachments.push_back(Attachment{ std::move(imageView), std::move(resolveView), std::string(name) });
        clearValues.clearColor.emplace_back(clearColor);
        if (resolveMode == ResolveMode::eNone) resolveMode = ResolveMode::eAverage;
        return *this;
    }

    Framebuffer::Builder& Framebuffer::Builder::addColorAttachment(const std::string_view name, ResourceRef<const Image> image, ClearColor clearColor)
    {
        return addColorAttachment(name, attachmentViewOf(image), clearColor);
    }

    Framebuffer::Builder& Framebuffer::Builder::setDepthAttachment(ResourceRef<const ImageView> imageView, float depth)
    {
        return setDepthAttachment(std::string_view{}, std::move(imageView), depth);
    }

    Framebuffer::Builder& Framebuffer::Builder::setDepthAttachment(ResourceRef<const ImageView> imageView, ResourceRef<const ImageView> resolveView, float depth)
    {
        adoptGeometry(imageView);
        depthAttachment = Attachment{ std::move(imageView), std::move(resolveView), "depth" };
        clearValues.clearDepth = depth;
        if (resolveMode == ResolveMode::eNone) resolveMode = ResolveMode::eAverage;
        return *this;
    }

    Framebuffer::Builder& Framebuffer::Builder::setDepthAttachment(ResourceRef<const Image> image, float depth)
    {
        return setDepthAttachment(std::string_view{}, attachmentViewOf(image), depth);
    }

    Framebuffer::Builder& Framebuffer::Builder::setDepthAttachment(const std::string_view name, ResourceRef<const ImageView> imageView, float depth)
    {
        adoptGeometry(imageView);
        // Named "depth" when the caller did not name it, so the usual target is reachable by the
        // obvious name without every framebuffer having to say so. @see Framebuffer::image
        depthAttachment = Attachment{ std::move(imageView), {}, name.empty() ? "depth" : std::string(name) };
        clearValues.clearDepth = depth;
        return *this;
    }

    Framebuffer::Builder& Framebuffer::Builder::setDepthAttachment(const std::string_view name, ResourceRef<const Image> image, float depth)
    {
        return setDepthAttachment(name, attachmentViewOf(image), depth);
    }

    Framebuffer::Builder& Framebuffer::Builder::setStencilAttachment(ResourceRef<const ImageView> imageView, glm::i32 stencil)
    {
        return setStencilAttachment(std::string_view{}, std::move(imageView), stencil);
    }

    Framebuffer::Builder& Framebuffer::Builder::setStencilAttachment(ResourceRef<const ImageView> imageView, ResourceRef<const ImageView> resolveView, glm::i32 stencil)
    {
        adoptGeometry(imageView);
        stencilAttachment = Attachment{ std::move(imageView), std::move(resolveView), "stencil" };
        clearValues.clearStencil = stencil;
        if (resolveMode == ResolveMode::eNone) resolveMode = ResolveMode::eAverage;
        return *this;
    }

    Framebuffer::Builder& Framebuffer::Builder::setStencilAttachment(ResourceRef<const Image> image, glm::i32 stencil)
    {
        return setStencilAttachment(std::string_view{}, attachmentViewOf(image), stencil);
    }

    Framebuffer::Builder& Framebuffer::Builder::setStencilAttachment(const std::string_view name, ResourceRef<const ImageView> imageView, glm::i32 stencil)
    {
        adoptGeometry(imageView);
        stencilAttachment = Attachment{ std::move(imageView), {}, name.empty() ? "stencil" : std::string(name) };
        clearValues.clearStencil = stencil;
        return *this;
    }

    Framebuffer::Builder& Framebuffer::Builder::setStencilAttachment(const std::string_view name, ResourceRef<const Image> image, glm::i32 stencil)
    {
        return setStencilAttachment(name, attachmentViewOf(image), stencil);
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

    Framebuffer::Builder& Framebuffer::Builder::setDepthStencilAttachment(ResourceRef<const Image> image, float depth, glm::i32 stencil)
    {
        return setDepthStencilAttachment(attachmentViewOf(image), depth, stencil);
    }

    Framebuffer::Builder& Framebuffer::Builder::setDepthStencilAttachment(const std::string_view name, ResourceRef<const Image> image, float depth, glm::i32 stencil)
    {
        const auto view = attachmentViewOf(image);
        // One view, two roles, one name: a combined depth-stencil format is a single target, and
        // naming it twice would put the same image in the lookup under two names for no gain.
        setDepthAttachment(name, view, depth);
        setStencilAttachment(name, view, stencil);
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

        // Adopted here, not as they are set: the attachments are held by tracked reference, so a
        // build attempt can inspect the state each one is in *now* — which is what lets a
        // framebuffer whose attachment was repaired come back with it.
        for (const auto& attachment : colorAttachments) {
            adopt(attachment.view, attachment.name.empty() ? "colour attachment" : attachment.name);
            if (attachment.resolve.alive()) adopt(attachment.resolve, "colour resolve attachment");
        }
        if (depthAttachment) {
            adopt(depthAttachment->view, "depth attachment");
            if (depthAttachment->resolve.alive()) adopt(depthAttachment->resolve, "depth resolve attachment");
        }
        if (stencilAttachment) {
            adopt(stencilAttachment->view, "stencil attachment");
            if (stencilAttachment->resolve.alive()) adopt(stencilAttachment->resolve, "stencil resolve attachment");
        }

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

    kor::Resource<Framebuffer> Framebuffer::Builder::build(const std::source_location where) const
    {
        return materialize<Framebuffer>(*this, "Framebuffer", where);
    }

    kor::Resource<Framebuffer> Framebuffer::CreateDefault() {
        switch (Context::activeAPI()) {
        case API::eOpenGL:
            return kor::MakeBackendResource<Framebuffer, ogl::Framebuffer>();
        case API::eVulkan:
            return kor::MakeBackendResource<Framebuffer, vk::Framebuffer>();
        default:
            throw std::runtime_error("Unknown graphics API!");
        }
    }


    glm::u32 Framebuffer::getColorAttachmentCount() const { return static_cast<glm::u32>(_colorAttachments.size()); }
    SampleCount Framebuffer::getSampleCount() const { return _sampleCount; }

    const std::vector<Framebuffer::Attachment>& Framebuffer::getColorAttachments() const
    {
        return _colorAttachments;
    }

    ResourceRef<const ImageView> Framebuffer::getColorAttachment(const glm::u32 index) const
    {
        if (index >= _colorAttachments.size()) return {};
        return _colorAttachments[index].view;
    }

    bool Framebuffer::hasDepthAttachment() const { return _depthAttachment.has_value(); }

    ResourceRef<const ImageView> Framebuffer::getDepthAttachment() const
    {
        // An empty ref rather than a throw: the callers that ask are deciding whether to do
        // something with the depth target, and "there isn't one" is an answer they can act on.
        if (!_depthAttachment) return {};
        return _depthAttachment->view;
    }

    bool Framebuffer::hasStencilAttachment() const { return _stencilAttachment.has_value(); }

    ResourceRef<const ImageView> Framebuffer::getStencilAttachment() const
    {
        if (!_stencilAttachment) return {};
        return _stencilAttachment->view;
    }

    bool Framebuffer::hasResolveAttachments() const
    {
        for (const auto& attachment : _colorAttachments) {
            if (attachment.resolve.alive()) return true;
        }
        if (_depthAttachment && _depthAttachment->resolve.alive()) return true;
        if (_stencilAttachment && _stencilAttachment->resolve.alive()) return true;
        return false;
    }

    ResourceRef<const ImageView> Framebuffer::getResolveAttachment(const glm::u32 index) const
    {
        if (index >= _colorAttachments.size()) return {};
        return _colorAttachments[index].resolve;
    }

    ResourceRef<const ImageView> Framebuffer::attachment(const std::string_view name) const
    {
        for (const auto& candidate : _colorAttachments) {
            if (candidate.namedBy(name)) return candidate.view;
        }
        if (_depthAttachment && _depthAttachment->namedBy(name)) return _depthAttachment->view;
        if (_stencilAttachment && _stencilAttachment->namedBy(name)) return _stencilAttachment->view;
        return {};
    }

    ResourceRef<const Image> Framebuffer::image(const std::string_view name) const
    {
        const auto view = attachment(name);
        if (!view.valid()) return {};
        return view->getImage();
    }

    ResourceRef<const Image> Framebuffer::colorImage(const glm::u32 index) const
    {
        const auto view = getColorAttachment(index);
        if (!view.valid()) return {};
        return view->getImage();
    }

    ResourceRef<const Image> Framebuffer::depthImage() const
    {
        const auto view = getDepthAttachment();
        if (!view.valid()) return {};
        return view->getImage();
    }

    std::vector<std::string> Framebuffer::attachmentNames() const
    {
        std::vector<std::string> names;
        for (const auto& candidate : _colorAttachments) {
            if (!candidate.name.empty()) names.push_back(candidate.name);
        }
        if (_depthAttachment && !_depthAttachment->name.empty()) names.push_back(_depthAttachment->name);
        // The stencil target is usually the depth target under a second name, and listing one
        // image twice helps nobody reading a "no such attachment" message.
        if (_stencilAttachment && !_stencilAttachment->name.empty() &&
            std::ranges::find(names, _stencilAttachment->name) == names.end()) {
            names.push_back(_stencilAttachment->name);
        }
        return names;
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
        _depthAttachment(createInfo.depthAttachment),
        _stencilAttachment(createInfo.stencilAttachment),
        _clearValues(createInfo.clearValues),
        _resolveMode(createInfo.resolveMode)
    {
        _isDefault = false;
        if (createInfo.sampleCount)
            _sampleCount = createInfo.sampleCount.value();
        if (createInfo.extent)
            _extent = createInfo.extent.value();
        else {
            // Nothing usable to measure leaves it at zero, which the build has already been
            // poisoned for: create() adopts every attachment, so an unusable one is reported there
            // rather than dereferenced here.
            const auto extentOf = [](const ResourceRef<const ImageView>& view) {
                if (!view.valid()) return glm::uvec2{ 0, 0 };
                const auto image = view->getImage();
                if (!image.valid()) return glm::uvec2{ 0, 0 };
                return glm::uvec2{ image->getExtent().x, image->getExtent().y };
            };
            _extent = _colorAttachments.empty()
                ? (_depthAttachment ? extentOf(_depthAttachment->view) : glm::uvec2{ 0, 0 })
                : extentOf(_colorAttachments[0].view);
        }
    }
}
