//
// Created by radue on 2/28/2026.
//

#pragma once
#include "image.h"
#include "core/imageView.h"
#include "utils/vk_wrapper.h"

namespace gfx::vk
{
    class DescriptorSet;

    class ImageView final : public gfx::ImageView {
        friend class gfx::vk::DescriptorSet;
    public:
        explicit ImageView(const Builder& builder);
        ~ImageView() override;

        ::vk::ImageView operator*() const;

    private:
        ::vk::ImageView operator[](const int i) const { return _imageViews[i]; }

        std::vector<::vk::ImageView> _imageViews {};
    };
}
