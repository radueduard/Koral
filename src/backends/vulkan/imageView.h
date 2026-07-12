//
// Created by radue on 2/28/2026.
//

#pragma once
#include "image.h"
#include <imageView.h>

namespace kor::vk
{
    class DescriptorSet;

    class ImageView final : public kor::ImageView {
        friend class kor::vk::DescriptorSet;
    public:
        explicit ImageView(const Builder& builder);
        ~ImageView() override;

        ::vk::ImageView operator*() const;
        ::vk::ImageView operator[](size_t i) const;
    private:
        std::vector<::vk::ImageView> _imageViews {};
    };
}
