//
// Created by radue on 2/20/2026.
//


#include "../backends/open_gl/imageView.h"
#include "../backends/vulkan/imageView.h"

#include <imageView.h>
#include <context.h>
#include <window.h>
#include <framebuffer.h>
#include <surface.h>

namespace kor
{
    ImageView::Builder::Builder(kor::ResourceRef<const Image> image) : image(image) {
        arrayLayerCount = image->ArrayLayers();
        mipLevelCount = image->MipLevels();
    }

    kor::Result<std::unique_ptr<ImageView>> ImageView::Builder::Create() const
    {
        BeginAttempt();
        Adopt(image, "image view's image");

        if (auto v = Validate(); !v) return std::unexpected(v.error());

        const auto api = Context::ActiveAPI();
        if (api != API::eOpenGL && api != API::eVulkan)
            return Fail(ErrorCode::eUnknownApi, "Unknown graphics API!");

        return Guard(ErrorCode::eBackend, [&]() -> std::unique_ptr<ImageView> {
            return (api == API::eVulkan)
                ? kor::MakeBackendPtr<ImageView, vk::ImageView>(*this)
                : kor::MakeBackendPtr<ImageView, ogl::ImageView>(*this);
        });
    }

    kor::Resource<ImageView> ImageView::Builder::Build(const std::source_location where) const
    {
        return Materialize<ImageView>(*this, "ImageView", where);
    }

    ImageView::ImageView(const Builder& createInfo) :
        _image(createInfo.image),
        _isPerFrame(_image->IsPerFrame()),
        _viewType(createInfo.type),
        _baseMipLevel(createInfo.baseMipLevel),
        _mipLevelCount(createInfo.mipLevelCount),
        _baseArrayLayer(createInfo.baseArrayLayer),
        _arrayLayerCount(createInfo.arrayLayerCount),
        _componentMapping(createInfo.componentMapping) {}
}