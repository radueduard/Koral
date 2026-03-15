//
// Created by radue on 2/20/2026.
//

#pragma once
#include <memory>
#include <glm/fwd.hpp>

#include "api.h"

namespace gfx
{
    class Image;

    class GFX_API ImageView {
    public:
        enum class Type {
            e1D,
            e2D,
            e3D,
            eCube,
            e1DArray,
            e2DArray,
            eCubeArray
        };

        struct GFX_API Builder {
            const Image& image;
            Type type = Type::e2D;
            glm::u32 baseMipLevel = 0;
            glm::u32 mipLevelCount = 1;
            glm::u32 baseArrayLayer = 0;
            glm::u32 arrayLayerCount = 1;

            explicit Builder(const Image& image);

            Builder& setViewType(Type viewType) {
                this->type = viewType;
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

        [[nodiscard]] const Image& getImage() const { return _image; }
        [[nodiscard]] Type getViewType() const { return _viewType; }
        [[nodiscard]] glm::u32 getBaseMipLevel() const { return _baseMipLevel; }
        [[nodiscard]] glm::u32 getMipLevelCount() const { return _mipLevelCount; }
        [[nodiscard]] glm::u32 getBaseArrayLayer() const { return _baseArrayLayer; }
        [[nodiscard]] glm::u32 getArrayLayerCount() const { return _arrayLayerCount; }

        [[nodiscard]] bool isPerFrame() const { return _isPerFrame; }

    protected:
        explicit ImageView(const Builder& createInfo);
        const Image& _image;
        bool _isPerFrame = false;
        Type _viewType;
        glm::u32 _baseMipLevel;
        glm::u32 _mipLevelCount;
        glm::u32 _baseArrayLayer;
        glm::u32 _arrayLayerCount;
    };
}

