//
// Created by radue on 2/20/2026.
//

#pragma once
#include <glm/fwd.hpp>

#include "image.h"

namespace gfx
{
    class ImageView {
    public:
        struct Builder {
            const Image& image;
            Image::Type viewType = Image::Type::e2D;
            glm::u32 baseMipLevel = 0;
            glm::u32 mipLevelCount = 1;
            glm::u32 baseArrayLayer = 0;
            glm::u32 arrayLayerCount = 1;

            explicit Builder(const Image& image);

            Builder& setViewType(Image::Type viewType) {
                this->viewType = viewType;
                return *this;
            }

            Builder& setBaseMipLevel(glm::u32 baseMipLevel) {
                this->baseMipLevel = baseMipLevel;
                return *this;
            }

            Builder& setMipLevelCount(glm::u32 mipLevelCount) {
                this->mipLevelCount = mipLevelCount;
                return *this;
            }

            Builder& setBaseArrayLayer(glm::u32 baseArrayLayer) {
                this->baseArrayLayer = baseArrayLayer;
                return *this;
            }

            Builder& setArrayLayerCount(glm::u32 arrayLayerCount) {
                this->arrayLayerCount = arrayLayerCount;
                return *this;
            }

            [[nodiscard]] std::unique_ptr<ImageView> build() const;
        };

        virtual ~ImageView() = default;

        static std::unique_ptr<ImageView> Create(const Image& image, const Builder& createInfo);

        [[nodiscard]] Image::Type GetViewType() const { return _viewType; }
        [[nodiscard]] glm::u32 GetBaseMipLevel() const { return _baseMipLevel; }
        [[nodiscard]] glm::u32 GetMipLevelCount() const { return _mipLevelCount; }
        [[nodiscard]] glm::u32 GetBaseArrayLayer() const { return _baseArrayLayer; }
        [[nodiscard]] glm::u32 GetArrayLayerCount() const { return _arrayLayerCount; }

    protected:
        explicit ImageView(const Builder& createInfo);
        Image::Type _viewType;
        glm::u32 _baseMipLevel;
        glm::u32 _mipLevelCount;
        glm::u32 _baseArrayLayer;
        glm::u32 _arrayLayerCount;
    };
}

