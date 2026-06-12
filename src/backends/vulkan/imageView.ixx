//
// Created by radue on 2/28/2026.
//

module;

#include <vulkan/vulkan.hpp>

export module vk.imageView;
import gfx.imageView;

namespace gfx::vk
{
    class DescriptorSet;

    export class ImageView final : public gfx::ImageView {
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
