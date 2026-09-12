//
// Created by radue on 2/20/2026.
//

#pragma once
#include <cstdint>
#include <memory>

#include "structs.h"
#include "api.h"
#include <source_location>

#include "builder.h"
#include "resource.h"
#include "error.h"

namespace kor
{
    // Filter lives in structs.h, alongside the rest of the API's vocabulary: a sampler is not its
    // only user — CommandBuffer::Blit picks how it rescales with the same enum.

    /**
     * @brief How a shader reads a texture: the filtering, the addressing and the mip selection.
     *
     * A sampler holds no pixels — it is the rule applied when sampling an image, and one sampler is
     * normally shared by every texture read the same way.
     *
     * @code
     * kor::Sampler::Builder builder;
     * auto linearRepeat = builder
     *     .setMinFilter(kor::Filter::eLinear)
     *     .setMagFilter(kor::Filter::eLinear)
     *     .setMipmapMode(kor::Sampler::MipmapMode::eLinear)
     *     .setAnisotropyEnable(true)
     *     .setMaxAnisotropy(16.f)
     *     .setMaxLod(static_cast<float>(texture->mipLevels()))
     *     .build();
     * @endcode
     *
     * Bind it alongside an image view with Descriptor(imageView, sampler).
     */
    class KORAL_API Sampler
    {
    public:
        /** @brief How the two nearest mip levels are combined. */
        enum class MipmapMode : std::uint8_t {
            eNearest,   ///< Take the single closest level. Cheaper, and visibly seams where the level changes.
            eLinear     ///< Blend the two closest levels — trilinear filtering.
        };

        /** @brief What happens when a texture coordinate falls outside 0..1. */
        enum class AddressMode : std::uint8_t {
            eRepeat,            ///< Tile the texture. The default, and what a tiling material wants.
            eMirroredRepeat,    ///< Tile, mirroring alternate copies, which hides the seam.
            eClampToEdge,       ///< Stretch the edge texel outwards. What a full-screen or UI texture wants.
            eClampToBorder      ///< Return a border colour outside the texture.
        };

        /** @brief Describes the sampler to create. */
        struct KORAL_API Builder : kor::Builder
        {
            Filter minFilter = Filter::eLinear;                 ///< Filtering when the texture is minified.
            Filter magFilter = Filter::eLinear;                 ///< Filtering when the texture is magnified.
            MipmapMode mipmapMode = MipmapMode::eLinear;        ///< How mip levels are combined.
            AddressMode addressModeU = AddressMode::eRepeat;    ///< Addressing on the U axis.
            AddressMode addressModeV = AddressMode::eRepeat;    ///< Addressing on the V axis.
            AddressMode addressModeW = AddressMode::eRepeat;    ///< Addressing on the W axis, for 3D textures.
            float mipLodBias = 0.f;                             ///< Added to the computed mip level; negative sharpens, positive blurs.
            bool anisotropyEnable = false;                      ///< Whether to filter anisotropically.
            float maxAnisotropy = 1.f;                          ///< Maximum anisotropy, capped by the device.
            bool compareEnable = false;                         ///< Whether this is a comparison sampler, for shadow mapping.
            CompareOp compareOp = CompareOp::eAlways;           ///< The comparison a comparison sampler performs.
            float minLod = 0.f;                                 ///< Lowest mip level the sampler will select.
            float maxLod = 0.f;                                 ///< Highest mip level it will select. Leave at 0 and no mip below the top is ever used.
            bool unnormalizedCoordinates = false;               ///< Whether coordinates are in texels rather than 0..1.

            /** @brief Sets the filtering used when the texture is minified — drawn smaller than its pixel size. */
            Builder& setMinFilter(Filter minFilter) {
                this->minFilter = minFilter;
                return *this;
            }

            /** @brief Sets the filtering used when the texture is magnified. eNearest gives crisp texels; eLinear smooths them. */
            Builder& setMagFilter(Filter magFilter) {
                this->magFilter = magFilter;
                return *this;
            }

            /** @brief Sets how the two nearest mip levels are combined. */
            Builder& setMipmapMode(MipmapMode mipmapMode) {
                this->mipmapMode = mipmapMode;
                return *this;
            }

            /** @brief Sets what happens outside 0..1 on the U axis. */
            Builder& setAddressModeU(AddressMode addressModeU) {
                this->addressModeU = addressModeU;
                return *this;
            }

            /** @brief Sets what happens outside 0..1 on the V axis. */
            Builder& setAddressModeV(AddressMode addressModeV) {
                this->addressModeV = addressModeV;
                return *this;
            }

            /** @brief Sets what happens outside 0..1 on the W axis, for 3D textures. */
            Builder& setAddressModeW(AddressMode addressModeW) {
                this->addressModeW = addressModeW;
                return *this;
            }

            /** @brief Shifts the mip level the sampler picks. Negative values sharpen at the cost of aliasing. */
            Builder& setMipLodBias(float mipLodBias) {
                this->mipLodBias = mipLodBias;
                return *this;
            }

            /**
             * @brief Enables anisotropic filtering.
             *
             * Sharpens textures viewed at a steep angle — ground planes above all — where ordinary
             * mip filtering blurs them. Costs bandwidth in proportion to setMaxAnisotropy.
             */
            Builder& setAnisotropyEnable(bool anisotropyEnable) {
                this->anisotropyEnable = anisotropyEnable;
                return *this;
            }

            /** @brief Sets the maximum anisotropy, silently capped by what the device supports. 16 is the usual maximum. */
            Builder& setMaxAnisotropy(float maxAnisotropy) {
                this->maxAnisotropy = maxAnisotropy;
                return *this;
            }

            /**
             * @brief Makes this a comparison sampler.
             *
             * Instead of returning the stored value, it compares each texel against a reference the
             * shader supplies and returns the filtered *result* of those comparisons — which is what
             * gives shadow maps a smooth edge in one lookup.
             */
            Builder& setCompareEnable(bool compareEnable) {
                this->compareEnable = compareEnable;
                return *this;
            }

            /** @brief Sets the comparison a comparison sampler performs. */
            Builder& setCompareOp(CompareOp compareOp) {
                this->compareOp = compareOp;
                return *this;
            }

            /** @brief Sets the lowest mip level the sampler may select. */
            Builder& setMinLod(float minLod) {
                this->minLod = minLod;
                return *this;
            }

            /**
             * @brief Sets the highest mip level the sampler may select.
             *
             * @warning Left at its default of 0, no level below the top is ever used and a mipped
             *          texture will alias as though it had no mip chain at all. Set it to the
             *          image's mip level count.
             */
            Builder& setMaxLod(float maxLod) {
                this->maxLod = maxLod;
                return *this;
            }

            /** @brief Switches coordinates from 0..1 to texel counts. Restricts the sampler: no mips, no repeat addressing. */
            Builder& setUnnormalizedCoordinates(bool unnormalizedCoordinates) {
                this->unnormalizedCoordinates = unnormalizedCoordinates;
                return *this;
            }

            /** @brief One build attempt. Internal: prefer build(). */
            [[nodiscard]] Result<std::unique_ptr<Sampler>> create() const;
            /** @brief Creates the sampler, poisoned rather than thrown if the device rejects the combination. */
            [[nodiscard]] kor::Resource<Sampler> build(std::source_location where = std::source_location::current()) const;
        };

        virtual ~Sampler() = default;

    protected:
        explicit Sampler(const Builder& builder);

        Filter _minFilter = Filter::eLinear;
        Filter _magFilter = Filter::eLinear;
        MipmapMode _mipmapMode = MipmapMode::eLinear;
        AddressMode _addressModeU = AddressMode::eRepeat;
        AddressMode _addressModeV = AddressMode::eRepeat;
        AddressMode _addressModeW = AddressMode::eRepeat;
        float _mipLodBias = 0.f;
        bool _anisotropyEnable = false;
        float _maxAnisotropy = 1.f;
        bool _compareEnable = false;
        CompareOp _compareOp = CompareOp::eAlways;
        float _minLod = 0.f;
        float _maxLod = 0.f;
        bool _unnormalizedCoordinates = false;

    };
}

