//
// Created by radue on 2/18/2026.
//

#include <image.h>
#include <buffer.h>
#include <commandBuffer.h>
#include <framebuffer.h>
#include <surface.h>

#include "../backends/open_gl/image.h"
#include "../backends/vulkan/image.h"
#include "../../include/window.h"

#include "context.h"
#include "imageView.h"

namespace kor
{
    // The per-subresource access tracking lives here, in the core, rather than in a backend, so the
    // barrier resolver and both backends share one notion of current state. It persists between
    // command buffers because the resource's state does: what one frame leaves behind is what the
    // next starts from.
    //
    // That is sound only because a frame records and submits exactly one command buffer
    // (Scheduler::Draw), so record order is execute order. Should that become several buffers, or
    // several threads, the resolver needs per-buffer entry/exit states reconciled at submit instead
    // of a single value read at record time.
    glm::u32 Image::trackingFrame() const
    {
        // Only a per-frame image has more than one copy, and only then does which frame it is matter.
        // Asked of the scheduler rather than remembered, so it is always the copy a command recorded
        // now would actually touch.
        if (!_isPerFrame) return 0;
        if (!Context::hasDevice() || Context::isHeadless()) return 0;
        return Context::Scheduler().currentImageIndex();
    }

    std::optional<ResourceAccess> Image::trackedAccess(const glm::u32 mipLevel, const glm::u32 arrayLayer) const
    {
        const auto tracked = _trackedAccess.find(trackingKey(mipLevel, arrayLayer));
        if (tracked == _trackedAccess.end()) return std::nullopt;
        return tracked->second;
    }

    void Image::setTrackedAccess(const ResourceAccess access, const glm::u32 mipLevel, const glm::u32 arrayLayer) const
    {
        _trackedAccess[trackingKey(mipLevel, arrayLayer)] = access;
    }

    kor::Result<std::unique_ptr<Image>> Image::Builder::create() const
    {
        beginAttempt();

        if (auto v = validate(); !v) return std::unexpected(v.error());

        const auto api = Context::activeAPI();
        if (api != API::eOpenGL && api != API::eVulkan)
            return fail(ErrorCode::eUnknownApi, "Unknown graphics API!");

        // Construct and (optionally) upload inside guard(): any backend exception becomes a
        // kor::Error, and a staging-buffer failure is re-thrown with its own cause attached.
        return guard(ErrorCode::eBackend, [&]() -> std::unique_ptr<Image> {
        // The object, not a Resource: materialize() builds the owning Resource around it. The
        // upload below needs a ResourceRef, so it takes an unsafe (untracked) one — sound here
        // because the image cannot outlive this scope before we hand it over.
        std::unique_ptr<Image> image = (api == API::eVulkan)
            ? kor::MakeBackendPtr<Image, vk::Image>(*this)
            : kor::MakeBackendPtr<Image, ogl::Image>(*this);

        const auto imageRef = ResourceRef<const Image>(image.get());

        // Upload initial pixel data, if any was supplied via setData(). Uses the same
        // staging-buffer + copy path as the importer, so it works on both backends.
        if (!data.empty()) {
            const auto staging = kor::Buffer::Builder<std::byte>()
                .setDataView(std::span<const std::byte>(data))
                .setUsage(kor::Buffer::Usage::eTransferSrc)
                .setType(kor::Buffer::Type::eStaging)
                .build();

            // The staging buffer is an internal detail of the upload, so its failure is *our*
            // failure — rethrow it so guard() turns it back into our error, with the allocation
            // failure kept as the cause the user actually needs to see.
            if (!staging.valid()) {
                throw BackendException(causedBy(
                    Error{ .code = ErrorCode::eBackend, .message = "Could not stage the image's initial pixel data." },
                    staging.errorPtr()));
            }

            CommandBuffer::SingleTimeCommand([&](CommandBuffer& commandBuffer) {
                commandBuffer.CopyBufferToImage(staging, imageRef, kor::Copy {
                    .imageBaseArrayLayer = 0,
                    .imageLayerCount = image->arrayLayers(),
                    .imageMipLevel = 0,
                });
            });

            if (image->mipLevels() > 1) {
                CommandBuffer::SingleTimeCommand([&](CommandBuffer& commandBuffer) {
                    commandBuffer.GenerateMipmaps(imageRef);
                });
            }

            // Leave the image shader-readable: the copy/mip commands leave it in a
            // transfer-destination state, but descriptors bind sampled images as read-only.
            CommandBuffer::SingleTimeCommand([&](CommandBuffer& commandBuffer) {
                commandBuffer.Barrier({}, {{ imageRef, ResourceAccess::eAllShaderRead }});
            });
        }

        return image;
        });
    }


    kor::Resource<Image> Image::Builder::build(const std::source_location where) const
    {
        return materialize<Image>(*this, "Image", where);
    }

    glm::u32 Image::channelSize(const kor::Image::Format format)
    {
        switch (format)
        {
        case Format::eR8_UNORM:
        case Format::eR8_SNORM:
        case Format::eR8_UINT:
        case Format::eR8_SINT:
            return 1;
        case Format::eRG8_UNORM:
        case Format::eRG8_SNORM:
        case Format::eRG8_UINT:
        case Format::eRG8_SINT:
            return 1;
        case Format::eRGB8_UNORM:
        case Format::eRGB8_SNORM:
        case Format::eRGB8_UINT:
        case Format::eRGB8_SINT:
        case Format::eRGB8_SRGB:
            return 1;
        case Format::eRGBA8_UNORM:
        case Format::eRGBA8_SNORM:
        case Format::eRGBA8_UINT:
        case Format::eRGBA8_SINT:
        case Format::eRGBA8_SRGB:
            return 1;
        case Format::eR16_UNORM:
        case Format::eR16_SNORM:
        case Format::eR16_UINT:
        case Format::eR16_SINT:
        case Format::eR16_SFLOAT:
            return 2;
        case Format::eRG16_UNORM:
        case Format::eRG16_SNORM:
        case Format::eRG16_UINT:
        case Format::eRG16_SINT:
        case Format::eRG16_SFLOAT:
            return 2;
        case Format::eRGB16_UNORM:
        case Format::eRGB16_SNORM:
        case Format::eRGB16_UINT:
        case Format::eRGB16_SINT:
        case Format::eRGB16_SFLOAT:
            return 2;
        case Format::eRGBA16_UNORM:
        case Format::eRGBA16_SNORM:
        case Format::eRGBA16_UINT:
        case Format::eRGBA16_SINT:
        case Format::eRGBA16_SFLOAT:
            return 2;
        case Format::eR32_UINT:
        case Format::eR32_SINT:
        case Format::eR32_SFLOAT:
            return 4;
        case Format::eRG32_UINT:
        case Format::eRG32_SINT:
        case Format::eRG32_SFLOAT:
            return 4;
        case Format::eRGB32_UINT:
        case Format::eRGB32_SINT:
        case Format::eRGB32_SFLOAT:
            return 4;
        case Format::eRGBA32_UINT:
        case Format::eRGBA32_SINT:
        case Format::eRGBA32_SFLOAT:
            return 4;
        case Format::eD16_UNORM:
            return 2;
        case Format::eD24_UNORM_S8_UINT:
            return 4;
        case Format::eD32_SFLOAT:
            return 4;
        case Format::eD32_SFLOAT_S8_UINT:
            return 4;
        default: throw std::runtime_error("Unsupported image format for pixel size!");
        }
    }

    glm::u32 Image::channelCount(const kor::Image::Format format)
    {
        switch (format)
        {
        case Format::eR8_UNORM:
        case Format::eR8_SNORM:
        case Format::eR8_UINT:
        case Format::eR8_SINT:
        case Format::eR16_UNORM:
        case Format::eR16_SNORM:
        case Format::eR16_UINT:
        case Format::eR16_SINT:
        case Format::eR16_SFLOAT:
        case Format::eR32_UINT:
        case Format::eR32_SINT:
        case Format::eR32_SFLOAT:
            return 1;
        case Format::eRG8_UNORM:
        case Format::eRG8_SNORM:
        case Format::eRG8_UINT:
        case Format::eRG8_SINT:
        case Format::eRG16_UNORM:
        case Format::eRG16_SNORM:
        case Format::eRG16_UINT:
        case Format::eRG16_SINT:
        case Format::eRG16_SFLOAT:
        case Format::eRG32_UINT:
        case Format::eRG32_SINT:
        case Format::eRG32_SFLOAT:
            return 2;
        case Format::eRGB8_UNORM:
        case Format::eRGB8_SNORM:
        case Format::eRGB8_UINT:
        case Format::eRGB8_SINT:
        case Format::eRGB8_SRGB:
        case Format::eRGB16_UNORM:
        case Format::eRGB16_SNORM:
        case Format::eRGB16_UINT:
        case Format::eRGB16_SINT:
        case Format::eRGB16_SFLOAT:
        case Format::eRGB32_UINT:
        case Format::eRGB32_SINT:
        case Format::eRGB32_SFLOAT:
            return 3;
        case Format::eRGBA8_UNORM:
        case Format::eRGBA8_SNORM:
        case Format::eRGBA8_UINT:
        case Format::eRGBA8_SINT:
        case Format::eRGBA8_SRGB:
        case Format::eRGBA16_UNORM:
        case Format::eRGBA16_SNORM:
        case Format::eRGBA16_UINT:
        case Format::eRGBA16_SINT:
        case Format::eRGBA16_SFLOAT:
        case Format::eRGBA32_UINT:
        case Format::eRGBA32_SINT:
        case Format::eRGBA32_SFLOAT:
            return 4;
        case Format::eD16_UNORM:
        case Format::eD32_SFLOAT:
            return 1;
        case Format::eD24_UNORM_S8_UINT:
        case Format::eD32_SFLOAT_S8_UINT:
            return 2;
        default: throw std::runtime_error("Unsupported image format for channel count!");
        }
    }

    void Image::Resize(const glm::uvec3& extent)
    {
        if (_extent == extent || extent.x == 0 || extent.y == 0 || extent.z == 0) return;

        // The extent first, since a backend builds the new image from it.
        _extent = extent;
        doResize(extent);

        // A replaced image is a *new* image: it starts in an undefined layout with nothing to wait on,
        // whatever the one before it had been transitioned to. Forgetting that here is what makes the
        // next use emit the barrier it needs — without it the resolver compares against the old
        // image's state, decides nothing is required, and the GPU reads an untransitioned image.
        _trackedAccess.clear();

        // The views handed out by view() are views of the storage that has just been replaced. They
        // cannot be repaired — an ImageView is built against an image and a resize is a new image —
        // so they are dropped, and the next caller gets a view of the image that now exists. Without
        // this a resized render target keeps handing out views of freed storage.
        for (auto& view : _defaultViews) view = {};

        // Anything holding a handle to the old image — an image view above all — finds out through this.
        ++_generation;
    }

    ImageShape Image::naturalShape() const
    {
        switch (_type) {
        case Type::e1D: return _arrayLayers > 1 ? ImageShape::e1DArray : ImageShape::e1D;
        case Type::e3D: return ImageShape::e3D;   // a volume has no array form
        // Six layers could be a cube map, and an attachment cannot tell: rendering into a cube is
        // rendering into its layers, which is what an array view gives. Only a shader's own
        // declaration settles the other reading, and a descriptor set uses that instead of this.
        default:        return _arrayLayers > 1 ? ImageShape::e2DArray : ImageShape::e2D;
        }
    }

    ResourceRef<const ImageView> Image::view(const ImageShape shape, const ViewCoverage coverage) const
    {
        const auto slot = viewSlot(shape, coverage);
        if (slot >= _defaultViews.size()) return {};

        // A poisoned entry is kept rather than retried: the reason it failed is a disagreement
        // between this image and the shape asked for, and nothing about a second attempt would
        // change that. It carries its error, and whoever binds it inherits it.
        if (_defaultViews[slot].valid() || _defaultViews[slot].poisoned())
            return ResourceRef<const ImageView>(_defaultViews[slot]);

        // What the shader asked for, mapped onto how a view says it. A shape reflection could not
        // name leaves nothing to build.
        const auto type = [shape]() -> std::optional<ImageView::Type> {
            switch (shape) {
            case ImageShape::e1D:        return ImageView::Type::e1D;
            case ImageShape::e2D:        return ImageView::Type::e2D;
            case ImageShape::e3D:        return ImageView::Type::e3D;
            case ImageShape::eCube:      return ImageView::Type::eCube;
            case ImageShape::e1DArray:   return ImageView::Type::e1DArray;
            case ImageShape::e2DArray:   return ImageView::Type::e2DArray;
            case ImageShape::eCubeArray: return ImageView::Type::eCubeArray;
            default:                     return std::nullopt;
            }
        }();

        if (!type) {
            log::error("Cannot make a default view of this image: the binding's shape is not one a "
                       "view can be built for. Build the view yourself with ImageView::Builder.");
            return {};
        }

        // Every layer either way, and every mip level only for a view that will be sampled: a
        // texture with one mip level has nothing for mip mapping to select from, while a view
        // rendered into must name exactly one. Neither is ImageView::Builder's own default, which
        // is one level of one layer — that is for saying precisely what you want, and this is for
        // not having to. @see Image::ViewCoverage
        //
        // An untracked ref to ourselves is sound here, and only here: the view is owned by this
        // image, so it cannot outlive the thing it points at. Every other route to an image takes a
        // tracked ref, because every other holder can.
        auto view = ImageView::Builder(ResourceRef<const Image>(this))
            .setViewType(*type)
            .setBaseMipLevel(0)
            .setMipLevelCount(coverage == ViewCoverage::eWholeImage ? _mipLevels : 1)
            .setBaseArrayLayer(0)
            .setArrayLayerCount(_arrayLayers)
            .build();

        _defaultViews[slot] = std::move(view);
        return ResourceRef<const ImageView>(_defaultViews[slot]);
    }

    bool Image::isFormatSupported(const kor::Image::Format format, const Flags<Usage> usage)
    {
        // No device, no answer — and "no" is the safe one: a caller choosing a format from what is
        // supported would otherwise pick something that cannot be created a moment later.
        if (!Context::hasDevice()) return false;

        if (Context::activeAPI() == API::eVulkan)
            return vk::Image::isFormatSupported(format, usage);
        if (Context::activeAPI() == API::eOpenGL)
            return ogl::Image::isFormatSupported(format, usage);
        return false;
    }

    bool Image::isBlockCompressed(const kor::Image::Format format)
    {
        switch (format)
        {
        case Format::eBC1_RGB_UNORM:   case Format::eBC1_RGB_SRGB:
        case Format::eBC1_RGBA_UNORM:  case Format::eBC1_RGBA_SRGB:
        case Format::eBC2_UNORM:       case Format::eBC2_SRGB:
        case Format::eBC3_UNORM:       case Format::eBC3_SRGB:
        case Format::eBC4_UNORM:       case Format::eBC4_SNORM:
        case Format::eBC5_UNORM:       case Format::eBC5_SNORM:
        case Format::eBC6H_UFLOAT:     case Format::eBC6H_SFLOAT:
        case Format::eBC7_UNORM:       case Format::eBC7_SRGB:
        case Format::eASTC_4x4_UNORM:  case Format::eASTC_4x4_SRGB:
        case Format::eASTC_6x6_UNORM:  case Format::eASTC_6x6_SRGB:
        case Format::eASTC_8x8_UNORM:  case Format::eASTC_8x8_SRGB:
        case Format::eETC2_RGB8_UNORM: case Format::eETC2_RGB8_SRGB:
        case Format::eETC2_RGBA8_UNORM:case Format::eETC2_RGBA8_SRGB:
        case Format::eEAC_R11_UNORM:   case Format::eEAC_R11_SNORM:
        case Format::eEAC_RG11_UNORM:  case Format::eEAC_RG11_SNORM:
            return true;
        default:
            return false;
        }
    }

    glm::uvec2 Image::blockExtent(const kor::Image::Format format)
    {
        switch (format)
        {
        // ASTC is the only family here with a choice of block size, and the format names it.
        case Format::eASTC_6x6_UNORM: case Format::eASTC_6x6_SRGB:
            return { 6, 6 };
        case Format::eASTC_8x8_UNORM: case Format::eASTC_8x8_SRGB:
            return { 8, 8 };
        default:
            // Every other compressed format is 4x4; an uncompressed one is its own texel, which
            // makes the block arithmetic in sizeOfRegion the same code for both.
            return isBlockCompressed(format) ? glm::uvec2{ 4, 4 } : glm::uvec2{ 1, 1 };
        }
    }

    glm::u32 Image::blockSize(const kor::Image::Format format)
    {
        switch (format)
        {
        // The 8-byte half of the family: three or one channel, no independent alpha.
        case Format::eBC1_RGB_UNORM:   case Format::eBC1_RGB_SRGB:
        case Format::eBC1_RGBA_UNORM:  case Format::eBC1_RGBA_SRGB:
        case Format::eBC4_UNORM:       case Format::eBC4_SNORM:
        case Format::eETC2_RGB8_UNORM: case Format::eETC2_RGB8_SRGB:
        case Format::eEAC_R11_UNORM:   case Format::eEAC_R11_SNORM:
            return 8;
        // Everything else compressed is 16 bytes a block, whatever its block covers.
        case Format::eBC2_UNORM:       case Format::eBC2_SRGB:
        case Format::eBC3_UNORM:       case Format::eBC3_SRGB:
        case Format::eBC5_UNORM:       case Format::eBC5_SNORM:
        case Format::eBC6H_UFLOAT:     case Format::eBC6H_SFLOAT:
        case Format::eBC7_UNORM:       case Format::eBC7_SRGB:
        case Format::eASTC_4x4_UNORM:  case Format::eASTC_4x4_SRGB:
        case Format::eASTC_6x6_UNORM:  case Format::eASTC_6x6_SRGB:
        case Format::eASTC_8x8_UNORM:  case Format::eASTC_8x8_SRGB:
        case Format::eETC2_RGBA8_UNORM:case Format::eETC2_RGBA8_SRGB:
        case Format::eEAC_RG11_UNORM:  case Format::eEAC_RG11_SNORM:
            return 16;
        default:
            // Uncompressed: one texel is the block.
            return channelSize(format) * channelCount(format);
        }
    }

    glm::u64 Image::sizeOfRegion(const kor::Image::Format format, const glm::uvec3 extent,
                                 const glm::u32 layerCount)
    {
        const auto block = blockExtent(format);
        // Round up: a 5-texel row of a 4x4 format still costs two blocks, and a buffer sized for
        // one and a quarter would be short.
        const glm::u64 blocksX = (static_cast<glm::u64>(extent.x) + block.x - 1) / block.x;
        const glm::u64 blocksY = (static_cast<glm::u64>(extent.y) + block.y - 1) / block.y;
        const glm::u64 depth = std::max(1u, extent.z);
        const glm::u64 layers = std::max(1u, layerCount);

        return blocksX * blocksY * depth * layers * blockSize(format);
    }

    Image::Image(const Builder& createInfo) :
        _isPerFrame(createInfo.isPerFrame),
        _type(createInfo.type),
        _format(createInfo.format),
        _extent(createInfo.extent),
        _mipLevels(createInfo.mipLevels),
        _arrayLayers(createInfo.arrayLayers),
        _sampleCount(createInfo.sampleCount),
        _usage(createInfo.usage) {
        if (_mipLevels == 0) {
            _mipLevels = 1 + static_cast<glm::u32>(std::floor(std::log2(std::max(_extent.x, std::max(_extent.y, _extent.z)))));
        }
    }

    bool isDepthStencilFormat(const Image::Format format)
    {
        switch (format)
        {
        case Image::Format::eD16_UNORM:
        case Image::Format::eD24_UNORM_S8_UINT:
        case Image::Format::eD32_SFLOAT:
        case Image::Format::eD32_SFLOAT_S8_UINT:
            return true;
        default:
            return false;
        }
    }

    bool isStencilFormat(Image::Format format)
    {
        switch (format)
        {
        case Image::Format::eD24_UNORM_S8_UINT:
        case Image::Format::eD32_SFLOAT_S8_UINT:
            return true;
        default:
            return false;
        }
    }
} // Koral