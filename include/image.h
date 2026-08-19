//
// Created by radue on 2/18/2026.
//

#pragma once
#include <memory>
#include <span>
#include <vector>
#include <cstddef>
#include <optional>
#include <unordered_map>
#include <glm/glm.hpp>

#include "flags.h"
#include "api.h"
#include "dataRange.h"
#include <source_location>

#include "builder.h"
#include "structs.h"
#include "resource.h"
#include "error.h"

namespace kor
{
    class Buffer;
    class FramebufferImage;

    /**
     * @brief How many samples per pixel a multisampled image stores.
     *
     * More samples means smoother edges and proportionally more memory and bandwidth. A
     * multisampled image cannot be sampled by a shader directly: resolve it first, with
     * CommandBuffer::Resolve or a framebuffer resolve attachment.
     */
    enum class MSAA
    {
        eNone = 0,  ///< One sample per pixel; an ordinary image.
        e2x = 2,    ///< Two samples per pixel.
        e4x = 4,    ///< Four samples per pixel. The usual choice where MSAA is wanted at all.
        e8x = 8,    ///< Eight samples per pixel.
        e16x = 16,  ///< Sixteen samples per pixel. Rarely supported, and rarely worth it.
    };

    /**
     * @brief A texture, render target or storage image: pixels the GPU can sample or write.
     *
     * Built through its builder, like every resource:
     *
     * @code
     * kor::Image::Builder builder;
     * auto texture = builder
     *     .setFormat(kor::Image::Format::eRGBA8_SRGB)
     *     .setExtent(glm::uvec2{width, height})
     *     .setMipLevels(mipCount)
     *     .addUsage(kor::Image::Usage::eSampled)
     *     .setData(std::span<const glm::u8vec4>(pixels))
     *     .build();
     * @endcode
     *
     * Three properties decide what an image can do. The **format** fixes what one pixel holds and
     * how a shader reads it — note that eRGBA8_SRGB and eRGBA8_UNORM store identical bytes but only
     * the first is converted from sRGB on read, which is the difference between correct and washed
     * out colour. The **usage** flags declare every role it will play, and a role not declared is
     * invalid. The **layout** an image must be in for each role is not your problem: the command
     * buffer tracks it per subresource and transitions it for you.
     *
     * @see ImageView for a window onto part of an image, Sampler for how shaders filter it
     */
    class KORAL_API Image
    {
    public:
        /** @brief How many dimensions the image has. */
        enum class Type
        {
            e1D,    ///< A row of pixels. Gradients and lookup tables.
            e2D,    ///< The ordinary case: a texture or a render target.
            e3D,    ///< A volume. Volumetric data and 3D lookup tables.
        };

        /**
         * @brief What one pixel holds, and how a shader reads it.
         *
         * The suffix is the interpretation: UNORM maps the stored integer onto 0..1, SNORM onto
         * -1..1, UINT and SINT keep it an integer, SFLOAT stores a real float, and SRGB is UNORM
         * with the sRGB-to-linear conversion applied on read and back on write — which is what
         * colour textures and colour attachments generally want, and what data textures (normal
         * maps, masks, height fields) generally do not.
         *
         * Not every format is supported by every device for every usage.
         */
        enum class Format
        {
            // 8-bit single channel formats
            eR8_UNORM,
            eR8_SNORM,
            eR8_UINT,
            eR8_SINT,

            // 8-bit dual channel formats
            eRG8_UNORM,
            eRG8_SNORM,
            eRG8_UINT,
            eRG8_SINT,

            // 8-bit triple channel formats
            eRGB8_UNORM,
            eRGB8_SNORM,
            eRGB8_UINT,
            eRGB8_SINT,
            eRGB8_SRGB,

            // 8-bit quad channel formats
            eRGBA8_UNORM,
            eRGBA8_SNORM,
            eRGBA8_UINT,
            eRGBA8_SINT,
            eRGBA8_SRGB,

            // 16-bit single channel formats
            eR16_UNORM,
            eR16_SNORM,
            eR16_UINT,
            eR16_SINT,
            eR16_SFLOAT,

            // 16-bit dual channel formats
            eRG16_UNORM,
            eRG16_SNORM,
            eRG16_UINT,
            eRG16_SINT,
            eRG16_SFLOAT,

            // 16-bit triple channel formats
            eRGB16_UNORM,
            eRGB16_SNORM,
            eRGB16_UINT,
            eRGB16_SINT,
            eRGB16_SFLOAT,

            // 16-bit quad channel formats
            eRGBA16_UNORM,
            eRGBA16_SNORM,
            eRGBA16_UINT,
            eRGBA16_SINT,
            eRGBA16_SFLOAT,

            // 32-bit single channel formats
            eR32_UINT,
            eR32_SINT,
            eR32_SFLOAT,

            // 32-bit dual channel formats
            eRG32_UINT,
            eRG32_SINT,
            eRG32_SFLOAT,

            // 32-bit triple channel formats
            eRGB32_UINT,
            eRGB32_SINT,
            eRGB32_SFLOAT,

            // 32-bit quad channel formats
            eRGBA32_UINT,
            eRGBA32_SINT,
            eRGBA32_SFLOAT,

            // Depth/stencil formats. eD32_SFLOAT is the usual depth buffer; the _S8_UINT pairs add a stencil.
            eD16_UNORM,
            eD24_UNORM_S8_UINT,
            eD32_SFLOAT,
            eD32_SFLOAT_S8_UINT,

            // Surface formats. What a swap chain typically presents; rarely chosen by hand.
            eBGRA8_UNORM,
            eBGRA8_SRGB,

            // ---- Block-compressed formats ----------------------------------------------------
            //
            // Texels are stored in fixed-size blocks — four bytes a texel becomes one byte or less —
            // which is what lets a scene's textures fit in video memory. The trade is that a block
            // is the smallest addressable unit: these cannot be rendered into, and a copy's offset
            // and extent must fall on block boundaries. Ask BlockExtentFromImageFormat and
            // BlockSizeFromImageFormat rather than assuming, and size a staging buffer with
            // SizeOfRegion.
            //
            // Which of these a given GPU actually supports varies — BC on desktop, ASTC and ETC2 on
            // mobile — which is why a compressed texture is normally shipped as a universal KTX2 and
            // transcoded into whichever of these is available. @see the image modules.

            // S3TC/DXT and friends: the desktop set.
            eBC1_RGB_UNORM,     ///< RGB, 8 bytes per 4x4 block. The smallest, and the oldest.
            eBC1_RGB_SRGB,
            eBC1_RGBA_UNORM,    ///< RGB plus one bit of alpha.
            eBC1_RGBA_SRGB,
            eBC2_UNORM,         ///< RGB with 4-bit alpha, 16 bytes per block.
            eBC2_SRGB,
            eBC3_UNORM,         ///< RGB with interpolated alpha — the classic DXT5.
            eBC3_SRGB,
            eBC4_UNORM,         ///< One channel, 8 bytes per block. Masks, heightfields, gloss.
            eBC4_SNORM,
            eBC5_UNORM,         ///< Two channels, 16 bytes per block. Tangent-space normal maps.
            eBC5_SNORM,
            eBC6H_UFLOAT,       ///< HDR RGB, 16 bytes per block. The compressed home for a sky.
            eBC6H_SFLOAT,
            eBC7_UNORM,         ///< RGBA, 16 bytes per block. The best quality of the family.
            eBC7_SRGB,

            // ASTC: one block size per format, all 16 bytes — the bigger the block, the smaller the
            // texture. Ubiquitous on mobile, uncommon on desktop.
            eASTC_4x4_UNORM,
            eASTC_4x4_SRGB,
            eASTC_6x6_UNORM,
            eASTC_6x6_SRGB,
            eASTC_8x8_UNORM,
            eASTC_8x8_SRGB,

            // ETC2 / EAC: the OpenGL ES baseline, and what a universal texture falls back to.
            eETC2_RGB8_UNORM,
            eETC2_RGB8_SRGB,
            eETC2_RGBA8_UNORM,
            eETC2_RGBA8_SRGB,
            eEAC_R11_UNORM,     ///< One channel, 8 bytes per block.
            eEAC_R11_SNORM,
            eEAC_RG11_UNORM,    ///< Two channels, 16 bytes per block.
            eEAC_RG11_SNORM,
        };

        /**
         * @brief Every role the image will play. A role not declared here is invalid at runtime.
         */
        enum class Usage
        {
            eTransferSrc = 1 << 0,              ///< Can be copied, blitted or resolved from. Needed to read it back, and to generate mipmaps.
            eTransferDst = 1 << 1,              ///< Can be copied, blitted or cleared into. Needed to upload pixels.
            eSampled = 1 << 2,                  ///< Can be sampled by a shader through a sampler — an ordinary texture.
            eStorage = 1 << 3,                  ///< Can be read and written directly by a shader, without filtering.
            eColorAttachment = 1 << 4,          ///< Can be rendered into as a colour target.
            eDepthStencilAttachment = 1 << 5,   ///< Can be rendered into as a depth and/or stencil target.
        };

        /** @brief Describes the image to create, and optionally the pixels to fill it with. */
        struct KORAL_API Builder : ::Builder {
            bool isPerFrame = false;                    ///< Whether to allocate one copy per frame in flight.
            Type type = Type::e2D;                      ///< How many dimensions it has.
            Format format = Format::eRGBA8_UNORM;       ///< What one pixel holds.
            glm::uvec3 extent = { 1, 1, 1 };            ///< Size in pixels. Unused dimensions are 1.
            glm::u32 mipLevels = 1;                     ///< Number of mip levels, counting the full-size one.
            glm::u32 arrayLayers = 1;                   ///< Number of layers, for texture arrays.
            MSAA msaa = MSAA::eNone;                    ///< Samples per pixel.
            Flags<Usage> usage = Usage::eSampled;       ///< Every role it will play.

            /**
             * @brief Gives the image one copy per frame in flight, for a target written every frame.
             *
             * Without it, writing an image the GPU is still reading from an earlier frame corrupts
             * that frame.
             */
            Builder& setIsPerFrame(const bool isPerFrame) {
                this->isPerFrame = isPerFrame;
                return *this;
            }

            /** @brief Sets how many dimensions the image has. */
            Builder& setType(const Type type) {
                this->type = type;
                return *this;
            }

            /** @brief Sets what one pixel holds and how shaders read it. */
            Builder& setFormat(const Format format) {
                this->format = format;
                return *this;
            }

            /** @brief Sets a cubic extent, the same size on every axis. */
            Builder& setExtent(const glm::u32& extent) {
                this->extent = { extent, extent, extent };
                return *this;
            }

            /** @brief Sets a 2D extent in pixels; depth becomes 1. The usual overload. */
            Builder& setExtent(const glm::uvec2& extent) {
                this->extent = { extent, 1 };
                return *this;
            }

            /** @brief Sets a 3D extent in pixels, for a volume image. */
            Builder& setExtent(const glm::uvec3& extent) {
                this->extent = extent;
                return *this;
            }

            /**
             * @brief Sets how many mip levels the image has, counting the full-size one.
             *
             * More than one asks for a mip chain; the smaller levels start out empty. Fill them
             * with CommandBuffer::GenerateMipmaps, which setData() does for you.
             */
            Builder& setMipLevels(const glm::u32 mipLevels) {
                this->mipLevels = mipLevels;
                return *this;
            }

            /** @brief Sets how many layers the image has, making it a texture array. */
            Builder& setArrayLayers(const glm::u32 arrayLayers) {
                this->arrayLayers = arrayLayers;
                return *this;
            }

            /**
             * @brief Sets the samples per pixel, making the image multisampled.
             *
             * A multisampled image can be rendered into but not sampled; resolve it to a
             * single-sampled one first.
             */
            Builder& setMSAA(const MSAA msaa) {
                this->msaa = msaa;
                return *this;
            }

            /** @brief Sets the samples per pixel from a SampleCount, so a pipeline's multisample state can be passed straight in. */
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

            /** @brief Replaces the usage flags outright, discarding any set before — including those setData() implies. */
            Builder& setUsage(const Flags<Usage>& usage) {
                this->usage = usage;
                return *this;
            }

            /** @brief Adds one role to those already declared. */
            Builder& addUsage(const Usage usage) {
                this->usage |= usage;
                return *this;
            }

            /// Initial pixel data, uploaded to mip 0 of every array layer when build() runs. Bytes
            /// are laid out layer-major, tightly packed, matching the image's format.
            std::vector<std::byte> data {};

            /**
             * @brief Fills the image from a typed span of pixels.
             * @param source The pixels as any range — a vector, an array, a span — of the type one
             *        texel is, e.g. glm::u8vec4 for an 8-bit RGBA image.
             *        Copied during this call, and must match the image's format and extent.
             *
             * Uploaded to mip 0 of every array layer when build() runs; if the image has more mip
             * levels, the rest are generated from it. Implies the transfer usages that needs.
             */
            template<typename R, typename T = std::remove_cvref_t<std::ranges::range_value_t<R>>>
                requires RangeOf<R, T>
            Builder& setData(R&& source) {
                const ContiguousCopy<T> contiguous(std::forward<R>(source));
                const std::span<const T> pixels = contiguous.view();
                const auto bytes = std::as_bytes(pixels);
                data.assign(bytes.begin(), bytes.end());
                usage |= Usage::eTransferDst;
                usage |= Usage::eTransferSrc;
                return *this;
            }

            /**
             * @brief Fills the image from an untyped pixel buffer.
             * @param pixels Start of the pixel data. Copied during this call.
             * @param sizeBytes How many bytes to take, which must match the image's format and extent.
             */
            Builder& setData(const void* pixels, const glm::u64 sizeBytes) {
                const auto* p = static_cast<const std::byte*>(pixels);
                data.assign(p, p + sizeBytes);
                usage |= Usage::eTransferDst;
                usage |= Usage::eTransferSrc;
                return *this;
            }

            /** @brief One build attempt. Internal: prefer build(). */
            [[nodiscard]] Result<std::unique_ptr<Image>> create() const;

            /**
             * @brief Creates the image and uploads any initial data.
             * @return The image as a Resource, poisoned rather than thrown if the build failed.
             */
            [[nodiscard]] kor::Resource<Image> build(std::source_location where = std::source_location::current()) const;
        };

        virtual ~Image() = default;

        /**
         * @brief Reallocates the image at a new size, discarding its contents.
         * @param extent The new size in pixels.
         *
         * For render targets that follow the window. Anything referring to the old storage — image
         * views, descriptor sets — has to be rebuilt afterwards.
         */
        void Resize(const glm::uvec3& extent);

        /**
         * @brief How many times this image has been rebuilt.
         *
         * A resize does not resize an image — it *replaces* it, because the storage a GPU image is
         * allocated with is immutable. Anything holding a handle to the old one (an image view above
         * all) is therefore stale, and this is how it finds out. @see Resize
         */
        [[nodiscard]] glm::u64 generation() const { return _generation; }

        /** @brief The image's size in pixels. Unused dimensions are 1. */
        [[nodiscard]] glm::uvec3 getExtent() const { return _extent; }

        /** @brief How many dimensions the image has. */
        [[nodiscard]] Type getType() const { return _type; }
        /** @brief What one pixel holds. */
        [[nodiscard]] Format getFormat() const { return _format; }
        /** @brief The samples per pixel. */
        [[nodiscard]] MSAA getMSAA() const { return _msaa; }

        /** @brief The samples per pixel as a SampleCount, to match against a pipeline's multisample state. */
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
        /** @brief Every role the image was created for. */
        [[nodiscard]] Flags<Usage> getUsage() const { return _usage; }
        /** @brief How many mip levels the image has, counting the full-size one. */
        [[nodiscard]] glm::u32 getMipLevels() const { return _mipLevels; }
        /** @brief How many array layers the image has. */
        [[nodiscard]] glm::u32 getArrayLayers() const { return _arrayLayers; }

        /**
         * @brief Bytes in one channel of @p format — 1 for an 8-bit format, 4 for a 32-bit one.
         * @throws std::runtime_error for a block-compressed format, which has no per-channel size.
         *         Guard with IsBlockCompressed, or use SizeOfRegion, which answers for both kinds.
         */
        [[nodiscard]] static glm::u32 ChannelSizeFromImageFormat(kor::Image::Format format);
        /**
         * @brief Channels in @p format — 1 for eR8_UNORM, 4 for eRGBA8_UNORM.
         * @throws std::runtime_error for a block-compressed format. @see ChannelSizeFromImageFormat
         */
        [[nodiscard]] static glm::u32 ChannelCountFromImageFormat(kor::Image::Format format);

        /**
         * @brief Whether the active device can hold an image of @p format in the roles @p usage names.
         * @param format The format to ask about, compressed or not.
         * @param usage What the image would be for. Only the roles that constrain a format are
         *        looked at — sampling, storage, and the two attachment kinds.
         * @return false when the device cannot, and when there is no device yet.
         *
         * Which *compressed* formats exist is the question this is really for: BC is desktop, ASTC
         * and ETC2 are mobile, and a universal texture has to be transcoded into whichever family
         * the machine actually has. Asking beats assuming — an image created in a format the device
         * lacks fails at creation, in the driver's words rather than yours.
         */
        [[nodiscard]] static bool IsFormatSupported(kor::Image::Format format,
                                                   Flags<Usage> usage = Usage::eSampled);

        /**
         * @brief Whether @p format stores blocks of texels rather than texels.
         *
         * The one question worth asking before doing arithmetic on an image's size: a compressed
         * format has no texel size, and its rows are counted in blocks.
         */
        [[nodiscard]] static bool IsBlockCompressed(kor::Image::Format format);

        /**
         * @brief The texels one block of @p format covers — 4x4 for BC, 8x8 for ASTC 8x8.
         * @return {1, 1} for an uncompressed format, so the same arithmetic works for both.
         */
        [[nodiscard]] static glm::uvec2 BlockExtentFromImageFormat(kor::Image::Format format);

        /**
         * @brief Bytes one block of @p format occupies — 8 for BC1, 16 for BC7.
         * @return For an uncompressed format, the size of one texel, since that is its block.
         */
        [[nodiscard]] static glm::u32 BlockSizeFromImageFormat(kor::Image::Format format);

        /**
         * @brief Bytes a tightly packed region of @p format occupies.
         * @param format What the texels are.
         * @param extent The region in *texels*; a partial block at the edge still costs a whole block.
         * @param layerCount How many array layers the region covers.
         *
         * What to size a staging buffer with, and what the copy guards measure against. Correct for
         * compressed and uncompressed alike, which is the point of it existing.
         */
        [[nodiscard]] static glm::u64 SizeOfRegion(kor::Image::Format format, glm::uvec3 extent,
                                                   glm::u32 layerCount = 1);

        /** @brief Whether the image holds a separate copy per frame in flight. */
        [[nodiscard]] bool isPerFrame() const { return _isPerFrame; }

        /**
         * @brief The access one subresource was last synchronised for.
         * @param mipLevel Which mip level.
         * @param arrayLayer Which array layer.
         * @return The access, or nullopt if that subresource has never been synchronised — an image
         *         in undefined layout, which always needs a barrier before its first use.
         *
         * Read by the command buffer's barrier resolver. Tracked per subresource because a mip
         * chain legitimately holds several at once while it is being generated.
         */
        [[nodiscard]] std::optional<ResourceAccess> getTrackedAccess(glm::u32 mipLevel = 0, glm::u32 arrayLayer = 0) const;

        /** @brief Records the access a subresource has been synchronised for. Called by the barrier resolver. */
        void setTrackedAccess(ResourceAccess access, glm::u32 mipLevel = 0, glm::u32 arrayLayer = 0) const;

    protected:
        /**
         * @brief Which copy of a per-frame image the tracked access below refers to.
         *
         * A per-frame image is several images, one per frame in flight, and they are transitioned
         * independently — so they have to be *tracked* independently. Keying them together makes the
         * barrier resolver decide that a copy it has never seen is already in the layout a different
         * copy reached, and the GPU then samples an untransitioned image. Always 0 for an ordinary
         * image, which has one copy.
         */
        [[nodiscard]] glm::u32 trackingFrame() const;

        /**
         * @brief Rebuilds the backend's image at the new extent. Called by Resize, never directly.
         *
         * Resize is not virtual on purpose: a resize *replaces* the image, and everything that follows
         * from that — the new extent, a bumped generation, and above all forgetting what the old image
         * had been synchronised for — belongs to every backend equally. A backend that had to remember
         * to do all of it would eventually forget one, and forgetting the last leaves the barrier
         * resolver believing a brand-new image is already in the layout the old one reached.
         */
        virtual void doResize(const glm::uvec3& extent) = 0;

        /// Subresource key: which copy, which mip, which layer.
        [[nodiscard]] glm::u64 trackingKey(const glm::u32 mipLevel, const glm::u32 arrayLayer) const {
            return static_cast<glm::u64>(trackingFrame()) << 48
                 | static_cast<glm::u64>(mipLevel) << 32
                 | arrayLayer;
        }
        mutable std::unordered_map<glm::u64, ResourceAccess> _trackedAccess;

        explicit Image(const Builder&);
        bool _isPerFrame = false;
        Type _type;
        Format _format;
        glm::uvec3 _extent;
        /// Bumped by a backend's Resize. @see generation
        glm::u64 _generation = 0;
        glm::u32 _mipLevels;
        glm::u32 _arrayLayers;
        MSAA _msaa;
        Flags<Usage> _usage;
    };

    /** @brief Whether @p format is a depth and/or stencil format, and so belongs in a depth attachment. */
    bool IsDepthStencilFormat(Image::Format format);

    /** @brief Whether @p format carries a stencil component. */
    bool IsStencilFormat(Image::Format format);
}

