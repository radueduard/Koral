//
// Created by radue on 2/20/2026.
//

#pragma once
#include <cstdint>
#include <memory>
#include <glm/fwd.hpp>

#include "api.h"
#include <source_location>

#include "builder.h"
#include "resource.h"
#include "error.h"

namespace kor
{
    class Image;

    /**
     * @brief A window onto part of an Image: which levels and layers, seen as what.
     *
     * Nothing binds an image to a shader or a framebuffer directly — a view does, and the view is
     * what says how the underlying pixels are to be interpreted. That indirection is what lets one
     * image be several things: an array texture sampled as a whole, one of its layers rendered into
     * on its own, or six layers presented to a shader as a cube map.
     *
     * @code
     * kor::ImageView::Builder builder(texture);
     * auto view = builder
     *     .setViewType(kor::ImageView::Type::e2D)
     *     .setMipLevelCount(texture->mipLevels())
     *     .build();
     * @endcode
     *
     * The view keeps a reference to its image, so the image outlives it.
     */
    class KORAL_API ImageView {
    public:
        /**
         * @brief How the shader or attachment sees the image.
         *
         * Must be compatible with the image itself: a cube view needs six array layers, an array
         * view needs the image to have layers at all.
         */
        enum class Type : std::uint8_t {
            e1D,        ///< A single row.
            e2D,        ///< A single 2D image. The ordinary case.
            e3D,        ///< A volume.
            eCube,      ///< Six layers as a cube map, sampled by direction.
            e1DArray,   ///< An array of rows.
            e2DArray,   ///< An array of 2D images, sampled by layer index.
            eCubeArray  ///< An array of cube maps.
        };

        /** @brief What one output channel of a sampled texel is taken from. */
        enum class Swizzle : std::uint8_t {
            eIdentity,  ///< The matching channel of the image, unchanged.
            eZero,      ///< Constant 0.
            eOne,       ///< Constant 1.
            eR,         ///< The image's red channel.
            eG,         ///< The image's green channel.
            eB,         ///< The image's blue channel.
            eA          ///< The image's alpha channel.
        };

        /**
         * @brief Rewires the channels a shader receives when it samples through this view.
         *
         * Left alone, each channel comes from its own — the identity mapping. Change it to
         * broadcast a single-channel image across RGB, to swap an image stored BGRA, or to force
         * alpha to 1 without touching the pixels.
         */
        struct KORAL_API ComponentMapping {
            Swizzle r = Swizzle::eIdentity; ///< Source of the red channel.
            Swizzle g = Swizzle::eIdentity; ///< Source of the green channel.
            Swizzle b = Swizzle::eIdentity; ///< Source of the blue channel.
            Swizzle a = Swizzle::eIdentity; ///< Source of the alpha channel.

            /** @brief Sets where the red channel comes from. */
            ComponentMapping& setR(const Swizzle swizzle) {
                r = swizzle;
                return *this;
            }
            /** @brief Sets where the green channel comes from. */
            ComponentMapping& setG(const Swizzle swizzle) {
                g = swizzle;
                return *this;
            }
            /** @brief Sets where the blue channel comes from. */
            ComponentMapping& setB(const Swizzle swizzle) {
                b = swizzle;
                return *this;
            }
            /** @brief Sets where the alpha channel comes from. */
            ComponentMapping& setA(const Swizzle swizzle) {
                a = swizzle;
                return *this;
            }
        };

        /** @brief Describes the view to create over an image. */
        struct KORAL_API Builder : kor::Builder {
            kor::ResourceRef<const Image> image;        ///< The image being viewed.
            Type type = Type::e2D;                      ///< How it is seen.
            glm::u32 baseMipLevel = 0;                  ///< First mip level included.
            glm::u32 mipLevelCount = 1;                 ///< How many levels are included.
            glm::u32 baseArrayLayer = 0;                ///< First array layer included.
            glm::u32 arrayLayerCount = 1;               ///< How many layers are included.
            ComponentMapping componentMapping = {};     ///< Channel rewiring, identity by default.

            /** @param image The image to view. It must outlive the view. */
            explicit Builder(kor::ResourceRef<const Image> image);

            /** @brief Sets how the image is seen — 2D, cube, array. Must be compatible with the image. */
            Builder& setViewType(Type viewType) {
                this->type = viewType;
                return *this;
            }

            /** @brief Sets the first mip level the view covers, so a shader can be given one level of a chain. */
            Builder& setBaseMipLevel(glm::u32 baseMipLevel) {
                this->baseMipLevel = baseMipLevel;
                return *this;
            }

            /**
             * @brief Sets how many mip levels the view covers, starting at the base.
             *
             * A sampled texture wants the whole chain, or mip mapping has nothing to select from; a
             * render target wants exactly one.
             */
            Builder& setMipLevelCount(glm::u32 mipLevelCount) {
                this->mipLevelCount = mipLevelCount;
                return *this;
            }

            /** @brief Sets the first array layer the view covers. */
            Builder& setBaseArrayLayer(glm::u32 baseArrayLayer) {
                this->baseArrayLayer = baseArrayLayer;
                return *this;
            }

            /** @brief Sets how many array layers the view covers. Six, with Type::eCube, makes a cube map. */
            Builder& setArrayLayerCount(glm::u32 arrayLayerCount) {
                this->arrayLayerCount = arrayLayerCount;
                return *this;
            }

            /** @brief Rewires which image channel feeds which output channel. */
            Builder& setComponentMapping(const ComponentMapping& componentMapping) {
                this->componentMapping = componentMapping;
                return *this;
            }

            /** @brief One build attempt. Internal: prefer build(). */
            [[nodiscard]] Result<std::unique_ptr<ImageView>> create() const;

            /** @brief Creates the view. Poisoned rather than thrown if the image and the view type disagree. */
            [[nodiscard]] kor::Resource<ImageView> build(std::source_location where = std::source_location::current()) const;
        };

        virtual ~ImageView() = default;

        /** @brief The image this is a view of. */
        [[nodiscard]] kor::ResourceRef<const Image> image() const { return _image; }
        /** @brief How the image is seen through this view. */
        [[nodiscard]] Type viewType() const { return _viewType; }
        /** @brief First mip level the view covers. */
        [[nodiscard]] glm::u32 baseMipLevel() const { return _baseMipLevel; }
        /** @brief How many mip levels the view covers. */
        [[nodiscard]] glm::u32 mipLevelCount() const { return _mipLevelCount; }
        /** @brief First array layer the view covers. */
        [[nodiscard]] glm::u32 baseArrayLayer() const { return _baseArrayLayer; }
        /** @brief How many array layers the view covers. */
        [[nodiscard]] glm::u32 arrayLayerCount() const { return _arrayLayerCount; }
        /** @brief The channel rewiring applied when sampling through this view. */
        [[nodiscard]] ComponentMapping componentMapping() const { return _componentMapping; }

        /** @brief Whether the view follows a per-frame image, and so has one instance per frame in flight. */
        [[nodiscard]] bool isPerFrame() const { return _isPerFrame; }

    protected:
        explicit ImageView(const Builder& createInfo);
        kor::ResourceRef<const Image> _image;
        bool _isPerFrame = false;
        Type _viewType;
        glm::u32 _baseMipLevel;
        glm::u32 _mipLevelCount;
        glm::u32 _baseArrayLayer;
        glm::u32 _arrayLayerCount;
        ComponentMapping _componentMapping;
    };
}

