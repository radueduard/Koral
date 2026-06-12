//
// Created by radue on 2/20/2026.
//

module;

#include <glm/fwd.hpp>
#include "api.h"

export module gfx.imageView;
import gfx.resource;

namespace gfx
{
    export class GFX_API ImageView {
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

        enum class Swizzle {
            eIdentity,
            eZero,
            eOne,
            eR,
            eG,
            eB,
            eA
        };

        struct GFX_API ComponentMapping {
            Swizzle r = Swizzle::eIdentity;
            Swizzle g = Swizzle::eIdentity;
            Swizzle b = Swizzle::eIdentity;
            Swizzle a = Swizzle::eIdentity;

            ComponentMapping& setR(const Swizzle swizzle) {
                r = swizzle;
                return *this;
            }
            ComponentMapping& setG(const Swizzle swizzle) {
                g = swizzle;
                return *this;
            }
            ComponentMapping& setB(const Swizzle swizzle) {
                b = swizzle;
                return *this;
            }
            ComponentMapping& setA(const Swizzle swizzle) {
                a = swizzle;
                return *this;
            }
        };

        struct GFX_API Builder {
            ResourceRef<const Image> image;
            Type type = Type::e2D;
            glm::u32 baseMipLevel = 0;
            glm::u32 mipLevelCount = 1;
            glm::u32 baseArrayLayer = 0;
            glm::u32 arrayLayerCount = 1;
            ComponentMapping componentMapping = {};

            explicit Builder(gfx::ResourceRef<const Image> image);

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

            Builder& setComponentMapping(const ComponentMapping& componentMapping) {
                this->componentMapping = componentMapping;
                return *this;
            }

            [[nodiscard]] gfx::Resource<ImageView> build() const;
        };

        virtual ~ImageView() = default;

        [[nodiscard]] gfx::ResourceRef<const Image> getImage() const { return _image; }
        [[nodiscard]] Type getViewType() const { return _viewType; }
        [[nodiscard]] glm::u32 getBaseMipLevel() const { return _baseMipLevel; }
        [[nodiscard]] glm::u32 getMipLevelCount() const { return _mipLevelCount; }
        [[nodiscard]] glm::u32 getBaseArrayLayer() const { return _baseArrayLayer; }
        [[nodiscard]] glm::u32 getArrayLayerCount() const { return _arrayLayerCount; }
        [[nodiscard]] ComponentMapping getComponentMapping() const { return _componentMapping; }

        [[nodiscard]] bool isPerFrame() const { return _isPerFrame; }

    protected:
        explicit ImageView(const Builder& createInfo);
        gfx::ResourceRef<const Image> _image;
        bool _isPerFrame = false;
        Type _viewType;
        glm::u32 _baseMipLevel;
        glm::u32 _mipLevelCount;
        glm::u32 _baseArrayLayer;
        glm::u32 _arrayLayerCount;
        ComponentMapping _componentMapping;
    };
}

