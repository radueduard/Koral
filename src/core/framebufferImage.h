//
// Created by eduard on 11.03.2026.
//

#pragma once
#include "image.h"

namespace gfx
{
    class ImageView;

    class FramebufferImage
    {
        friend class Image::Builder;
    public:
        ~FramebufferImage() = default;

        [[nodiscard]] std::vector<std::reference_wrapper<const ImageView>> getImageViews() const;
    private:
        explicit FramebufferImage(const Image::Builder& createInfo);

        std::vector<std::unique_ptr<Image>> _images {};
        std::vector<std::unique_ptr<ImageView>> _imageViews {};
    };
}
