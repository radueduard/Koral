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

    void Framebuffer::Resize(const glm::uvec2& newExtent)
    {
        if (newExtent.x == 0 || newExtent.y == 0) return;

        // The default framebuffer is the swap chain's, and the swap chain resizes its own images, so
        // the shared work below is skipped for it — resizing them from here would be resizing images
        // this framebuffer does not own. doResize still runs: re-pointing the default at the swap
        // chain's new images is exactly what the backend half is for.
        if (!_isDefault) {
            if (_extent == newExtent) return;
            _extent = newExtent;

            // Each attachment's image. Two attachments can name the same image — a depth and a
            // stencil attachment usually do — and Image::Resize is a no-op the second time round,
            // since by then the extent already matches.
            const auto resize = [&newExtent](const ResourceRef<const ImageView>& view) {
                if (!view.Valid()) return;
                if (const auto image = view->SourceImage(); image.Valid()) {
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

        DoResize(newExtent);
    }

    namespace {
        // The view an Image gives when it is handed straight to a framebuffer: its top mip level,
        // every array layer, shaped the way the image itself suggests. An attachment view must name
        // exactly one level — rendering into a view of several is invalid — which is what makes
        // this the eTopLevel coverage rather than the whole-image one a texture gets.
        ResourceRef<const ImageView> attachmentViewOf(const ResourceRef<const Image>& image)
        {
            if (!image.Valid()) return {};
            return image->View(image->NaturalShape(), Image::ViewCoverage::eTopLevel);
        }
    }

    // Extent and sample count are taken from the first attachment that can answer, and every later
    // one is expected to agree. An unusable attachment answers for neither, and is left to poison
    // the build through Adopt() rather than being dereferenced here.
    void Framebuffer::Builder::AdoptGeometry(const ResourceRef<const ImageView>& imageView)
    {
        if (!imageView.Valid()) return;
        const auto image = imageView->SourceImage();
        if (!image.Valid()) return;
        if (!extent) extent = glm::uvec2{ image->Extent().x, image->Extent().y };
        if (!sampleCount) sampleCount = image->Samples();
    }

    // ---- AttachmentSource: an Image or a view of one ---------------------------------------

    Framebuffer::Builder::AttachmentSource::AttachmentSource(ResourceRef<const ImageView> view)
        : view(std::move(view)) {}

    Framebuffer::Builder::AttachmentSource::AttachmentSource(ResourceRef<const Image> image)
        : view(attachmentViewOf(image)) {}

    Framebuffer::Builder::AttachmentSource::AttachmentSource(const Resource<ImageView>& view)
        : view(ResourceRef<const ImageView>(view)) {}

    Framebuffer::Builder::AttachmentSource::AttachmentSource(const Resource<Image>& image)
        : view(attachmentViewOf(ResourceRef<const Image>(image))) {}

    // ---- Attachments -------------------------------------------------------------------------

    Framebuffer::Builder& Framebuffer::Builder::AddColor(const ColorAttachment& attachment)
    {
        AdoptGeometry(attachment.view.view);
        colorAttachments.push_back(Attachment{
            attachment.view.view, attachment.resolve.view, std::string(attachment.name) });
        clearValues.clearColor.emplace_back(attachment.clear);

        // A resolve target with no mode named would collapse to nothing, so assume the usual one.
        // Left alone once set, so an explicit setResolveMode wins wherever it appears in the chain.
        if (attachment.resolve.view.Alive() && resolveMode == ResolveMode::eNone) {
            resolveMode = ResolveMode::eAverage;
        }
        return *this;
    }

    Framebuffer::Builder& Framebuffer::Builder::SetDepth(const DepthStencilAttachment& attachment)
    {
        AdoptGeometry(attachment.view.view);
        depthAttachment = Attachment{
            attachment.view.view, attachment.resolve.view, std::string(attachment.name) };
        clearValues.clearDepth = attachment.depth;
        if (attachment.resolve.view.Alive() && resolveMode == ResolveMode::eNone) {
            resolveMode = ResolveMode::eAverage;
        }
        return *this;
    }

    Framebuffer::Builder& Framebuffer::Builder::SetStencil(const DepthStencilAttachment& attachment)
    {
        AdoptGeometry(attachment.view.view);
        stencilAttachment = Attachment{
            attachment.view.view, attachment.resolve.view, std::string(attachment.name) };
        clearValues.clearStencil = attachment.stencil;
        if (attachment.resolve.view.Alive() && resolveMode == ResolveMode::eNone) {
            resolveMode = ResolveMode::eAverage;
        }
        return *this;
    }

    Framebuffer::Builder& Framebuffer::Builder::SetDepthStencil(const DepthStencilAttachment& attachment)
    {
        // One view, two roles, one name: a combined depth-stencil format is a single target, and
        // naming it twice would put the same image in the lookup under two names for no gain.
        SetDepth(attachment);
        SetStencil(attachment);
        return *this;
    }

    Framebuffer::Builder& Framebuffer::Builder::SetResolveMode(ResolveMode mode)
    {
        resolveMode = mode;
        return *this;
    }

    Result<std::unique_ptr<Framebuffer>> Framebuffer::Builder::Create() const
    {
        BeginAttempt();

        // Adopted here, not as they are set: the attachments are held by tracked reference, so a
        // build attempt can inspect the state each one is in *now* — which is what lets a
        // framebuffer whose attachment was repaired come back with it.
        for (const auto& attachment : colorAttachments) {
            Adopt(attachment.view, attachment.name.empty() ? "colour attachment" : attachment.name);
            if (attachment.resolve.Alive()) Adopt(attachment.resolve, "colour resolve attachment");
        }
        if (depthAttachment) {
            Adopt(depthAttachment->view, "depth attachment");
            if (depthAttachment->resolve.Alive()) Adopt(depthAttachment->resolve, "depth resolve attachment");
        }
        if (stencilAttachment) {
            Adopt(stencilAttachment->view, "stencil attachment");
            if (stencilAttachment->resolve.Alive()) Adopt(stencilAttachment->resolve, "stencil resolve attachment");
        }

        if (auto v = Validate(); !v) return std::unexpected(v.error());

        const auto api = Context::ActiveAPI();
        if (api != API::eOpenGL && api != API::eVulkan)
            return Fail(ErrorCode::eUnknownApi, "Unknown graphics API!");

        return Guard(ErrorCode::eBackend, [&]() -> std::unique_ptr<Framebuffer> {
            return (api == API::eVulkan)
                ? MakeBackendPtr<Framebuffer, vk::Framebuffer>(*this)
                : MakeBackendPtr<Framebuffer, ogl::Framebuffer>(*this);
        });
    }

    kor::Resource<Framebuffer> Framebuffer::Builder::Build(const std::source_location where) const
    {
        return Materialize<Framebuffer>(*this, "Framebuffer", where);
    }

    kor::Resource<Framebuffer> Framebuffer::CreateDefault() {
        switch (Context::ActiveAPI()) {
        case API::eOpenGL:
            return kor::MakeBackendResource<Framebuffer, ogl::Framebuffer>();
        case API::eVulkan:
            return kor::MakeBackendResource<Framebuffer, vk::Framebuffer>();
        default:
            throw std::runtime_error("Unknown graphics API!");
        }
    }


    glm::u32 Framebuffer::ColorAttachmentCount() const { return static_cast<glm::u32>(_colorAttachments.size()); }
    SampleCount Framebuffer::Samples() const { return _sampleCount; }

    const std::vector<Framebuffer::Attachment>& Framebuffer::ColorAttachments() const
    {
        return _colorAttachments;
    }

    ResourceRef<const ImageView> Framebuffer::ColorAttachment(const glm::u32 index) const
    {
        if (index >= _colorAttachments.size()) return {};
        return _colorAttachments[index].view;
    }

    bool Framebuffer::HasDepthAttachment() const { return _depthAttachment.has_value(); }

    ResourceRef<const ImageView> Framebuffer::DepthAttachment() const
    {
        // An empty ref rather than a throw: the callers that ask are deciding whether to do
        // something with the depth target, and "there isn't one" is an answer they can act on.
        if (!_depthAttachment) return {};
        return _depthAttachment->view;
    }

    bool Framebuffer::HasStencilAttachment() const { return _stencilAttachment.has_value(); }

    ResourceRef<const ImageView> Framebuffer::StencilAttachment() const
    {
        if (!_stencilAttachment) return {};
        return _stencilAttachment->view;
    }

    bool Framebuffer::HasResolveAttachments() const
    {
        for (const auto& attachment : _colorAttachments) {
            if (attachment.resolve.Alive()) return true;
        }
        if (_depthAttachment && _depthAttachment->resolve.Alive()) return true;
        if (_stencilAttachment && _stencilAttachment->resolve.Alive()) return true;
        return false;
    }

    ResourceRef<const ImageView> Framebuffer::ResolveAttachment(const glm::u32 index) const
    {
        if (index >= _colorAttachments.size()) return {};
        return _colorAttachments[index].resolve;
    }

    ResourceRef<const ImageView> Framebuffer::AttachmentNamed(const std::string_view name) const
    {
        for (const auto& candidate : _colorAttachments) {
            if (candidate.NamedBy(name)) return candidate.view;
        }
        if (_depthAttachment && _depthAttachment->NamedBy(name)) return _depthAttachment->view;
        if (_stencilAttachment && _stencilAttachment->NamedBy(name)) return _stencilAttachment->view;
        return {};
    }

    ResourceRef<const Image> Framebuffer::ImageNamed(const std::string_view name) const
    {
        const auto view = AttachmentNamed(name);
        if (!view.Valid()) return {};
        return view->SourceImage();
    }

    ResourceRef<const Image> Framebuffer::ColorImage(const glm::u32 index) const
    {
        const auto view = ColorAttachment(index);
        if (!view.Valid()) return {};
        return view->SourceImage();
    }

    ResourceRef<const Image> Framebuffer::DepthImage() const
    {
        const auto view = DepthAttachment();
        if (!view.Valid()) return {};
        return view->SourceImage();
    }

    std::vector<std::string> Framebuffer::AttachmentNames() const
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
    const ClearColor& Framebuffer::ClearColorAt(const glm::u32 index) const
    {
        return _clearValues.clearColor[index];
    }

    float Framebuffer::ClearDepth() const
    {
        return _clearValues.clearDepth;
    }

    glm::i32 Framebuffer::ClearStencil() const
    {
        return _clearValues.clearStencil;
    }

    ResolveMode Framebuffer::ResolveMethod() const { return _resolveMode; }

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
            // poisoned for: Create() adopts every attachment, so an unusable one is reported there
            // rather than dereferenced here.
            const auto extentOf = [](const ResourceRef<const ImageView>& view) {
                if (!view.Valid()) return glm::uvec2{ 0, 0 };
                const auto image = view->SourceImage();
                if (!image.Valid()) return glm::uvec2{ 0, 0 };
                return glm::uvec2{ image->Extent().x, image->Extent().y };
            };
            _extent = _colorAttachments.empty()
                ? (_depthAttachment ? extentOf(_depthAttachment->view) : glm::uvec2{ 0, 0 })
                : extentOf(_colorAttachments[0].view);
        }
    }
}
