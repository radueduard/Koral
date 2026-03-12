//
// Created by eduard on 11.03.2026.
//

#include "framebufferImage.h"

#include "imageView.h"
#include "scheduler.h"

namespace gfx
{
    std::vector<std::reference_wrapper<const ImageView>> FramebufferImage::getImageViews() const
    {
        std::vector<std::reference_wrapper<const ImageView>> imageViews;
        imageViews.reserve(_imageViews.size());
        for (const auto& imageView : _imageViews) {
            imageViews.emplace_back(*imageView);
        }
        return imageViews;
    }

    FramebufferImage::FramebufferImage(const Image::Builder& createInfo)
    {
        for (glm::u32 i = 0; i < Context::Scheduler().getImageCount(); ++i) {
            const auto& image = _images.emplace_back(createInfo.build());
            _imageViews.emplace_back(ImageView::Builder(*image).setViewType(ImageView::Type::e2D).build());
        }
    }
}
