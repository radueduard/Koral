//
// Created by radue on 2/28/2026.
//

#pragma once
#include "image.h"
#include "core/imageView.h"
#include "utils/vk_wrapper.h"

namespace gfx::vk
{
    class ImageView final : public gfx::ImageView, public vk::Wrapper<::vk::ImageView> {
    public:
        explicit ImageView(const Builder& builder);
        ~ImageView() override;
    };
}
