//
// Created by radue on 2/28/2026.
//

module;

#include <vulkan/vulkan.hpp>

module gfx;
import :vk_imageView;
import :vk_device;
import :vk_image;
import :vk_context;
import :vk_enum_conversions;

import :imageView;
import :image;

namespace gfx::vk
{
    ImageView::ImageView(const Builder& builder) : gfx::ImageView(builder)
    {
        ::vk::ImageAspectFlags aspectMask = ::vk::ImageAspectFlagBits::eColor;
        if (gfx::IsDepthStencilFormat(_image->getFormat())) {
            aspectMask = ::vk::ImageAspectFlagBits::eDepth;
            if (gfx::IsStencilFormat(_image->getFormat())) {
                aspectMask |= ::vk::ImageAspectFlagBits::eStencil;
            }
        }

        const auto& vkImage = dynamic_cast<const gfx::vk::Image&>(*_image);
        for (const auto& image : vkImage._images) {
            auto viewInfo = ::vk::ImageViewCreateInfo()
                .setImage(image)
                .setViewType(getVkImageViewType(_viewType))
                .setFormat(getVkFormat(_image->getFormat()))
                .setComponents(::vk::ComponentMapping()
                    .setR(getVkComponentSwizzle(_componentMapping.r))
                    .setG(getVkComponentSwizzle(_componentMapping.g))
                    .setB(getVkComponentSwizzle(_componentMapping.b))
                    .setA(getVkComponentSwizzle(_componentMapping.a)))
                .setSubresourceRange(::vk::ImageSubresourceRange()
                    .setAspectMask(aspectMask)
                    .setBaseMipLevel(_baseMipLevel)
                    .setLevelCount(_mipLevelCount)
                    .setBaseArrayLayer(_baseArrayLayer)
                    .setLayerCount(_arrayLayerCount));
            _imageViews.emplace_back(vk::Context::Device()->createImageView(viewInfo));
        }
    }

    ImageView::~ImageView()
    {
        for (const auto& imageView : _imageViews) {
            vk::Context::Device()->destroyImageView(imageView);
        }
    }

    ::vk::ImageView ImageView::operator*() const
    {
        const auto currentFrame = _isPerFrame ? gfx::Context::GetScheduler().getCurrentImageIndex() : 0;
        return _imageViews[currentFrame];
    }

    ::vk::ImageView ImageView::operator[](size_t i) const {
        if (i >= _imageViews.size()) {
            throw std::out_of_range("ImageView index out of range!");
        }
        return _imageViews[i];
    }
}
