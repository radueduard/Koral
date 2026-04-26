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
    ImageView::Builder::Builder(gfx::ResourceRef<Image> image) : image(image) {
        arrayLayerCount = image->getArrayLayers();
        mipLevelCount = image->getMipLevels();
    }

    gfx::Resource<ImageView> ImageView::Builder::build() const
    {
        switch (Context::Window().getAPI()) {
        case API::eOpenGL:
            return gfx::MakeResource<ogl::ImageView>(*this);
        case API::eVulkan:
            return gfx::MakeResource<vk::ImageView>(*this);
        default:
            throw std::runtime_error("Unknown graphics API!");
        }
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