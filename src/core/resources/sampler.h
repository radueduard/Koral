//
// Created by radue on 2/20/2026.
//

#pragma once
#include <memory>
#include <glm/vec4.hpp>

namespace gfx
{
    class Sampler
    {
    public:
        enum class Filter
        {
            eNearest,
            eLinear,
        };

        enum class MipmapMode {
            eNearest,
            eLinear
        };

        enum class AddressMode {
            eRepeat,
            eMirroredRepeat,
            eClampToEdge,
            eClampToBorder
        };

        enum class CompareOp {
            eNever,
            eLess,
            eEqual,
            eLessOrEqual,
            eGreater,
            eNotEqual,
            eGreaterOrEqual,
            eAlways
        };

        struct CreateInfo
        {
            Filter minFilter = Filter::eLinear;
            Filter magFilter = Filter::eLinear;
            MipmapMode mipmapMode = MipmapMode::eLinear;
            AddressMode addressModeU = AddressMode::eRepeat;
            AddressMode addressModeV = AddressMode::eRepeat;
            AddressMode addressModeW = AddressMode::eRepeat;
            float mipLodBias = 0.f;
            bool anisotropyEnable = false;
            float maxAnisotropy = 1.f;
            bool compareEnable = false;
            CompareOp compareOp = CompareOp::eAlways;
            float minLod = 0.f;
            float maxLod = 0.f;
            glm::vec4 borderColor = glm::vec4(0.f);
            bool unnormalizedCoordinates = false;

            CreateInfo& setMinFilter(Filter minFilter) {
                this->minFilter = minFilter;
                return *this;
            }

            CreateInfo& setMagFilter(Filter magFilter) {
                this->magFilter = magFilter;
                return *this;
            }

            CreateInfo& setMipmapMode(MipmapMode mipmapMode) {
                this->mipmapMode = mipmapMode;
                return *this;
            }

            CreateInfo& setAddressModeU(AddressMode addressModeU) {
                this->addressModeU = addressModeU;
                return *this;
            }

            CreateInfo& setAddressModeV(AddressMode addressModeV) {
                this->addressModeV = addressModeV;
                return *this;
            }

            CreateInfo& setAddressModeW(AddressMode addressModeW) {
                this->addressModeW = addressModeW;
                return *this;
            }

            CreateInfo& setMipLodBias(float mipLodBias) {
                this->mipLodBias = mipLodBias;
                return *this;
            }

            CreateInfo& setAnisotropyEnable(bool anisotropyEnable) {
                this->anisotropyEnable = anisotropyEnable;
                return *this;
            }

            CreateInfo& setMaxAnisotropy(float maxAnisotropy) {
                this->maxAnisotropy = maxAnisotropy;
                return *this;
            }

            CreateInfo& setCompareEnable(bool compareEnable) {
                this->compareEnable = compareEnable;
                return *this;
            }

            CreateInfo& setCompareOp(CompareOp compareOp) {
                this->compareOp = compareOp;
                return *this;
            }

            CreateInfo& setMinLod(float minLod) {
                this->minLod = minLod;
                return *this;
            }

            CreateInfo& setMaxLod(float maxLod) {
                this->maxLod = maxLod;
                return *this;
            }

            CreateInfo& setBorderColor(const glm::vec4& borderColor) {
                this->borderColor = borderColor;
                return *this;
            }

            CreateInfo& setUnnormalizedCoordinates(bool unnormalizedCoordinates) {
                this->unnormalizedCoordinates = unnormalizedCoordinates;
                return *this;
            }

            [[nodiscard]] std::unique_ptr<Sampler> build() const;
        };

        virtual ~Sampler() = default;

    protected:
        explicit Sampler() = default;
    };
}

