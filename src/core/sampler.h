//
// Created by radue on 2/20/2026.
//

#pragma once
#include <memory>

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

        struct Builder
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
            bool unnormalizedCoordinates = false;

            Builder& setMinFilter(Filter minFilter) {
                this->minFilter = minFilter;
                return *this;
            }

            Builder& setMagFilter(Filter magFilter) {
                this->magFilter = magFilter;
                return *this;
            }

            Builder& setMipmapMode(MipmapMode mipmapMode) {
                this->mipmapMode = mipmapMode;
                return *this;
            }

            Builder& setAddressModeU(AddressMode addressModeU) {
                this->addressModeU = addressModeU;
                return *this;
            }

            Builder& setAddressModeV(AddressMode addressModeV) {
                this->addressModeV = addressModeV;
                return *this;
            }

            Builder& setAddressModeW(AddressMode addressModeW) {
                this->addressModeW = addressModeW;
                return *this;
            }

            Builder& setMipLodBias(float mipLodBias) {
                this->mipLodBias = mipLodBias;
                return *this;
            }

            Builder& setAnisotropyEnable(bool anisotropyEnable) {
                this->anisotropyEnable = anisotropyEnable;
                return *this;
            }

            Builder& setMaxAnisotropy(float maxAnisotropy) {
                this->maxAnisotropy = maxAnisotropy;
                return *this;
            }

            Builder& setCompareEnable(bool compareEnable) {
                this->compareEnable = compareEnable;
                return *this;
            }

            Builder& setCompareOp(CompareOp compareOp) {
                this->compareOp = compareOp;
                return *this;
            }

            Builder& setMinLod(float minLod) {
                this->minLod = minLod;
                return *this;
            }

            Builder& setMaxLod(float maxLod) {
                this->maxLod = maxLod;
                return *this;
            }

            Builder& setUnnormalizedCoordinates(bool unnormalizedCoordinates) {
                this->unnormalizedCoordinates = unnormalizedCoordinates;
                return *this;
            }

            [[nodiscard]] std::unique_ptr<Sampler> build() const;
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

