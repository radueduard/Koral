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

namespace gfx
{
    ImageView::Builder::Builder(gfx::ResourceRef<const Image> image) : image(image) {
        arrayLayerCount = image->getArrayLayers();
        mipLevelCount = image->getMipLevels();
    }

    gfx::Result<std::unique_ptr<ImageView>> ImageView::Builder::create() const
    {
        beginAttempt();
        adopt(image, "image view's image");

        if (auto v = validate(); !v) return std::unexpected(v.error());

        const auto api = Context::activeAPI();
        if (api != API::eOpenGL && api != API::eVulkan)
            return fail(ErrorCode::eUnknownApi, "Unknown graphics API!");

        return guard(ErrorCode::eBackend, [&]() -> std::unique_ptr<ImageView> {
            return (api == API::eVulkan)
                ? gfx::MakeBackendPtr<ImageView, vk::ImageView>(*this)
                : gfx::MakeBackendPtr<ImageView, ogl::ImageView>(*this);
        });
    }

    gfx::Resource<ImageView> ImageView::Builder::build() const
    {
        return materialize<ImageView>(*this, "ImageView");
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