//
// Created by radue on 2/18/2026.
//

#pragma once
#include <memory>
#include <span>
#include <vector>
#include <cstddef>
#include <glm/glm.hpp>

#include "flags.h"
#include "api.h"
#include <source_location>

#include "builder.h"
#include "structs.h"
#include "resource.h"
#include "error.h"

namespace kor
{
    class Buffer;
    class FramebufferImage;

    enum class MSAA
    {
        eNone = 0,
        e2x = 2,
        e4x = 4,
        e8x = 8,
        e16x = 16,
    };

    class KORAL_API Image
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

            // </hint> Surface formats
            eBGRA8_UNORM,
            eBGRA8_SRGB,

            // </hint> Compressed formats
            eBC1_RGB_UNORM,
            eBC1_RGB_SRGB,
            eBC1_RGBA_UNORM,
            eBC1_RGBA_SRGB,
            eBC2_UNORM,
            eBC2_SRGB,
            eBC3_UNORM,
            eBC3_SRGB,
            eBC7_UNORM,
            eBC7_SRGB,
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

        struct KORAL_API Builder : ::Builder {
            bool isPerFrame = false;
            Type type = Type::e2D;
            Format format = Format::eRGBA8_UNORM;
            glm::uvec3 extent = { 1, 1, 1 };
            glm::u32 mipLevels = 1;
            glm::u32 arrayLayers = 1;
            MSAA msaa = MSAA::eNone;
            Flags<Usage> usage = Usage::eSampled;

            Builder& setIsPerFrame(const bool isPerFrame) {
                this->isPerFrame = isPerFrame;
                return *this;
            }

            Builder& setType(const Type type) {
                this->type = type;
                return *this;
            }

            Builder& setFormat(const Format format) {
                this->format = format;
                return *this;
            }

            Builder& setExtent(const glm::u32& extent) {
                this->extent = { extent, extent, extent };
                return *this;
            }

            Builder& setExtent(const glm::uvec2& extent) {
                this->extent = { extent, 1 };
                return *this;
            }

            Builder& setExtent(const glm::uvec3& extent) {
                this->extent = extent;
                return *this;
            }

            Builder& setMipLevels(const glm::u32 mipLevels) {
                this->mipLevels = mipLevels;
                return *this;
            }

            Builder& setArrayLayers(const glm::u32 arrayLayers) {
                this->arrayLayers = arrayLayers;
                return *this;
            }

            Builder& setMSAA(const MSAA msaa) {
                this->msaa = msaa;
                return *this;
            }

            // Convenience: set MSAA from a SampleCount value.
            Builder& setSampleCount(const SampleCount sampleCount) {
                switch (sampleCount) {
                case SampleCount::e1:  this->msaa = MSAA::eNone; break;
                case SampleCount::e2:  this->msaa = MSAA::e2x;   break;
                case SampleCount::e4:  this->msaa = MSAA::e4x;   break;
                case SampleCount::e8:  this->msaa = MSAA::e8x;   break;
                case SampleCount::e16: this->msaa = MSAA::e16x;  break;
                default:               this->msaa = MSAA::eNone; break;
                }
                return *this;
            }

            Builder& setUsage(const Flags<Usage>& usage) {
                this->usage = usage;
                return *this;
            }

            Builder& addUsage(const Usage usage) {
                this->usage |= usage;
                return *this;
            }

            // Initial pixel data, uploaded to mip 0 of every array layer when build() runs.
            // Bytes are laid out layer-major, tightly packed, matching the image's format.
            // If the image has more than one mip level, the remaining levels are generated.
            std::vector<std::byte> data {};

            // Sets initial pixel data from a typed span (e.g. std::span<const glm::u8vec4>).
            // Implies eTransferDst/eTransferSrc usage so the upload (and mip generation) work.
            template<typename T>
            Builder& setData(std::span<const T> pixels) {
                const auto bytes = std::as_bytes(pixels);
                data.assign(bytes.begin(), bytes.end());
                usage |= Usage::eTransferDst;
                usage |= Usage::eTransferSrc;
                return *this;
            }

            // Raw-bytes overload for callers that already have an untyped pixel buffer.
            Builder& setData(const void* pixels, const glm::u64 sizeBytes) {
                const auto* p = static_cast<const std::byte*>(pixels);
                data.assign(p, p + sizeBytes);
                usage |= Usage::eTransferDst;
                usage |= Usage::eTransferSrc;
                return *this;
            }

            /** @brief One build attempt. Internal: prefer build(). */
            [[nodiscard]] Result<std::unique_ptr<Image>> create() const;
            [[nodiscard]] kor::Resource<Image> build(std::source_location where = std::source_location::current()) const;
        };

        virtual ~Image() = default;

        virtual void Resize(const glm::uvec3& extent) = 0;

        [[nodiscard]] glm::uvec3 getExtent() const { return _extent; }

        [[nodiscard]] Type getType() const { return _type; }
        [[nodiscard]] Format getFormat() const { return _format; }
        [[nodiscard]] MSAA getMSAA() const { return _msaa; }

        [[nodiscard]] SampleCount getSampleCount() const {
            switch (_msaa) {
            case MSAA::eNone:  return SampleCount::e1;
            case MSAA::e2x:    return SampleCount::e2;
            case MSAA::e4x:    return SampleCount::e4;
            case MSAA::e8x:    return SampleCount::e8;
            case MSAA::e16x:   return SampleCount::e16;
            default:           return SampleCount::e1;
            }
        }
        [[nodiscard]] Flags<Usage> getUsage() const { return _usage; }
        [[nodiscard]] glm::u32 getMipLevels() const { return _mipLevels; }
        [[nodiscard]] glm::u32 getArrayLayers() const { return _arrayLayers; }

        [[nodiscard]] static glm::u32 ChannelSizeFromImageFormat(kor::Image::Format format);
        [[nodiscard]] static glm::u32 ChannelCountFromImageFormat(kor::Image::Format format);

        [[nodiscard]] bool isPerFrame() const { return _isPerFrame; }

    protected:
        explicit Image(const Builder&);
        bool _isPerFrame = false;
        Type _type;
        Format _format;
        glm::uvec3 _extent;
        glm::u32 _mipLevels;
        glm::u32 _arrayLayers;
        MSAA _msaa;
        Flags<Usage> _usage;
    };

    bool IsDepthStencilFormat(Image::Format format);
    bool IsStencilFormat(Image::Format format);
}

