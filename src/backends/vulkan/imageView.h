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
        // (Re)creates one view per copy of the image. Called by the constructor, and again by the
        // accessors when the image has been rebuilt underneath them. @see kor::Image::generation
        void build() const;

        /**
         * @brief Rebuilds the views if the image has been replaced since they were made.
         *
         * A Vulkan image view names a particular VkImage, and a resize allocates a new one — so a view
         * that was not rebuilt points at freed memory. Checked lazily, on use, rather than pushed from
         * the image: the image would otherwise have to keep a list of its views and their lifetimes,
         * and every use of a view goes through one of the two accessors anyway.
         */
        void refreshIfStale() const;

        mutable std::vector<::vk::ImageView> _imageViews {};
        mutable glm::u64 _imageGeneration = 0;
    };
}
