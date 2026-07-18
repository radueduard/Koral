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
        arrayLayerCount = image->getArrayLayers();
        mipLevelCount = image->getMipLevels();
    }

    kor::Result<std::unique_ptr<ImageView>> ImageView::Builder::create() const
    {
        beginAttempt();
        adopt(image, "image view's image");

        if (auto v = validate(); !v) return std::unexpected(v.error());

        const auto api = Context::activeAPI();
        if (api != API::eOpenGL && api != API::eVulkan)
            return fail(ErrorCode::eUnknownApi, "Unknown graphics API!");

        return guard(ErrorCode::eBackend, [&]() -> std::unique_ptr<ImageView> {
            return (api == API::eVulkan)
                ? kor::MakeBackendPtr<ImageView, vk::ImageView>(*this)
                : kor::MakeBackendPtr<ImageView, ogl::ImageView>(*this);
        });
    }

    kor::Resource<ImageView> ImageView::Builder::build(const std::source_location where) const
    {
        return materialize<ImageView>(*this, "ImageView", where);
    }

    ImageView::ImageView(const Builder& createInfo) :
        _image(createInfo.image),
        _isPerFrame(_image->isPerFrame()),
        _viewType(createInfo.type),
        _baseMipLevel(createInfo.baseMipLevel),
        _mipLevelCount(createInfo.mipLevelCount),
        _baseArrayLayer(createInfo.baseArrayLayer),
        _arrayLayerCount(createInfo.arrayLayerCount),
        _componentMapping(createInfo.componentMapping) {}
}