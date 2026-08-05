//
// Created by radue on 2/28/2026.
//

#include "imageView.h"

#include "device.h"
#include "vulkanContext.h"
#include <scheduler.h>

#include "context.h"
#include "vk_enum_conversions.h"

namespace kor::vk
{
    ImageView::ImageView(const Builder& builder) : kor::ImageView(builder)
    {
        build();
    }

    void ImageView::build() const
    {
        ::vk::ImageAspectFlags aspectMask = ::vk::ImageAspectFlagBits::eColor;
        if (kor::IsDepthStencilFormat(_image->getFormat())) {
            aspectMask = ::vk::ImageAspectFlagBits::eDepth;
            if (kor::IsStencilFormat(_image->getFormat())) {
                aspectMask |= ::vk::ImageAspectFlagBits::eStencil;
            }
        }

        const auto& vkImage = dynamic_cast<const kor::vk::Image&>(*_image);
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
        _imageGeneration = _image->generation();
    }

    void ImageView::refreshIfStale() const
    {
        if (!_image.valid() || _imageGeneration == _image->generation()) return;

        // The image was resized, so every view of it names a VkImage that no longer exists. Waiting
        // for the device is what makes destroying them safe: a resize happens between frames, but the
        // frames in flight may still hold these handles.
        vk::Context::Device()->waitIdle();
        for (const auto& imageView : _imageViews) {
            vk::Context::Device()->destroyImageView(imageView);
        }
        _imageViews.clear();
        build();
    }

    ImageView::~ImageView()
    {
        for (const auto& imageView : _imageViews) {
            vk::Context::Device()->destroyImageView(imageView);
        }
    }

    ::vk::ImageView ImageView::operator*() const
    {
        refreshIfStale();
        const auto currentFrame = _isPerFrame ? kor::Context::Scheduler().getCurrentImageIndex() : 0;
        return _imageViews[currentFrame];
    }

    ::vk::ImageView ImageView::operator[](size_t i) const {
        refreshIfStale();
        if (!_isPerFrame) {
            return _imageViews[0];
        }
        if (i >= _imageViews.size()) {
            throw std::out_of_range("ImageView index out of range!");
        }
        return _imageViews[i];
    }
}
