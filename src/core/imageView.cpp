//
// Created by radue on 2/20/2026.
//

module gfx;
import :imageView;

import std;
import :vk_imageView;
import :ogl_imageView;

namespace gfx
{
    ImageView::Builder::Builder(ResourceRef<const Image> image) : image(image) {
        arrayLayerCount = image->getArrayLayers();
        mipLevelCount = image->getMipLevels();
    }

    Resource<ImageView> ImageView::Builder::build() const
    {
        switch (Context::GetWindow().getAPI()) {
        case API::eOpenGL:
            return MakeResource<ogl::ImageView>(*this);
        case API::eVulkan:
            return MakeResource<vk::ImageView>(*this);
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
