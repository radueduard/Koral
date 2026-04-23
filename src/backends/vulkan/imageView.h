//
// Created by radue on 2/28/2026.
//

#pragma once
#include "image.h"
#include <imageView.h>

namespace gfx::vk
{
    class DescriptorSet;

    class ImageView final : public gfx::ImageView {
        friend class gfx::vk::DescriptorSet;
    public:
        explicit ImageView(const Builder& builder);
        ~ImageView() override;

        ::vk::ImageView operator*() const;
        ::vk::ImageView operator[](size_t i) const;
    private:
        std::vector<::vk::ImageView> _imageViews {};
    };
}
