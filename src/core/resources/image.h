//
// Created by radue on 2/18/2026.
//

#pragma once
#include <memory>
#include <glm/glm.hpp>

#include "buffer.h"
#include "utils/flags.h"

namespace gfx
{
    class Image
    {
    public:
        enum class Type
        {
            e1D,
            e2D,
            e3D,
        };

        enum class Format
        {
            // </hint> 8-bit single channel formats
            eR8_UNORM,
            eR8_SNORM,
            eR8_UINT,
            eR8_SINT,

            // </hint> 8-bit dual channel formats
            eRG8_UNORM,
            eRG8_SNORM,
            eRG8_UINT,
            eRG8_SINT,

            // </hint> 8-bit triple channel formats
            eRGB8_UNORM,
            eRGB8_SNORM,
            eRGB8_UINT,
            eRGB8_SINT,
            eRGB8_SRGB,

            // </hint> 8-bit quad channel formats
            eRGBA8_UNORM,
            eRGBA8_SNORM,
            eRGBA8_UINT,
            eRGBA8_SINT,
            eRGBA8_SRGB,

            // </hint> 16-bit single channel formats
            eR16_UNORM,
            eR16_SNORM,
            eR16_UINT,
            eR16_SINT,
            eR16_SFLOAT,

            // </hint> 16-bit dual channel formats
            eRG16_UNORM,
            eRG16_SNORM,
            eRG16_UINT,
            eRG16_SINT,
            eRG16_SFLOAT,

            // </hint> 16-bit triple channel formats
            eRGB16_UNORM,
            eRGB16_SNORM,
            eRGB16_UINT,
            eRGB16_SINT,
            eRGB16_SFLOAT,

            // </hint> 16-bit quad channel formats
            eRGBA16_UNORM,
            eRGBA16_SNORM,
            eRGBA16_UINT,
            eRGBA16_SINT,
            eRGBA16_SFLOAT,

            // </hint> 32-bit single channel formats
            eR32_UINT,
            eR32_SINT,
            eR32_SFLOAT,

            // </hint> 32-bit dual channel formats
            eRG32_UINT,
            eRG32_SINT,
            eRG32_SFLOAT,

            // </hint> 32-bit triple channel formats
            eRGB32_UINT,
            eRGB32_SINT,
            eRGB32_SFLOAT,

            // </hint> 32-bit quad channel formats
            eRGBA32_UINT,
            eRGBA32_SINT,
            eRGBA32_SFLOAT,

            // </hint> Depth/stencil formats
            eD16_UNORM,
            eD24_UNORM_S8_UINT,
            eD32_SFLOAT,
            eD32_SFLOAT_S8_UINT,
        };

        enum class MSAA
        {
            eNone = 0,
            e2x = 2,
            e4x = 4,
            e8x = 8,
            e16x = 16,
        };

        enum class Usage
        {
            eTransferSrc = 1 << 0,
            eTransferDst = 1 << 1,
            eSampled = 1 << 2,
            eStorage = 1 << 3,
            eColorAttachment = 1 << 4,
            eDepthStencilAttachment = 1 << 5,
        };

        struct CreateInfo {
            Type type = Type::e2D;
            Format format = Format::eRGBA8_UNORM;
            glm::uvec3 extent = { 1, 1, 1 };
            glm::u32 mipLevels = 1;
            glm::u32 arrayLayers = 1;
            MSAA msaa = MSAA::eNone;
            Flags<Usage> usage = Usage::eSampled;

            CreateInfo& setType(const Type type) {
                this->type = type;
                return *this;
            }

            CreateInfo& setFormat(const Format format) {
                this->format = format;
                return *this;
            }

            CreateInfo& setExtent(const glm::u32& extent) {
                this->extent = { extent, extent, extent };
                return *this;
            }

            CreateInfo& setExtent(const glm::uvec2& extent) {
                this->extent = { extent, 1 };
                return *this;
            }

            CreateInfo& setExtent(const glm::uvec3& extent) {
                this->extent = extent;
                return *this;
            }

            CreateInfo& setMipLevels(const glm::u32 mipLevels) {
                this->mipLevels = mipLevels;
                return *this;
            }

            CreateInfo& setArrayLayers(const glm::u32 arrayLayers) {
                this->arrayLayers = arrayLayers;
                return *this;
            }

            CreateInfo& setMSAA(const MSAA msaa) {
                this->msaa = msaa;
                return *this;
            }

            CreateInfo& setUsage(const Flags<Usage>& usage) {
                this->usage = usage;
                return *this;
            }

            CreateInfo& addUsage(const Usage usage) {
                this->usage |= usage;
                return *this;
            }
        };

        static std::unique_ptr<Image> Create(const CreateInfo& createInfo);

        virtual ~Image() = default;

        virtual std::vector<std::byte> ReadData(glm::u32 mipLevel = 0, glm::u32 arrayLayer = 0) const = 0;
        virtual void CopyFrom(const gfx::Buffer& buffer) = 0;

        glm::uvec3 getExtent() const { return extent; }

    protected:
        explicit Image(const CreateInfo&);

        Type type;
        Format format;
        glm::uvec3 extent;
        glm::u32 mipLevels;
        glm::u32 arrayLayers;
        MSAA msaa;
        Flags<Usage> usage;
    };

    bool IsDepthStencilFormat(Image::Format format);
}

