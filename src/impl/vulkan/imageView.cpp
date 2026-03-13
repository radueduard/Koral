//
// Created by radue on 2/28/2026.
//

#include "imageView.h"

#include "device.h"
#include "vulkanContext.h"
#include "core/scheduler.h"
#include "utils/vk_enum_conversions.h"

namespace gfx::vk
{
    ImageView::ImageView(const Builder& builder) : gfx::ImageView(builder)
    {
        ::vk::ImageAspectFlags aspectMask = ::vk::ImageAspectFlagBits::eColor;
        if (gfx::IsDepthStencilFormat(_image.getFormat())) {
            aspectMask = ::vk::ImageAspectFlagBits::eDepth;
            if (gfx::IsStencilFormat(_image.getFormat())) {
                aspectMask |= ::vk::ImageAspectFlagBits::eStencil;
            }
        }

        const auto& vkImage = dynamic_cast<const gfx::vk::Image&>(_image);
        for (const auto& image : vkImage._images) {
            auto viewInfo = ::vk::ImageViewCreateInfo()
                .setImage(image)
                .setViewType(getVkImageViewType(_viewType))
                .setFormat(getVkFormat(_image.getFormat()))
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
        const auto currentFrame = _isPerFrame ? gfx::Context::Scheduler().getCurrentImageIndex() : 0;
        return _imageViews[currentFrame];
    }
}
