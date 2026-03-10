//
// Created by radue on 2/28/2026.
//

#include "image.h"

#include "core/image.h"

#include "buffer.h"
#include "device.h"
#include "vulkanContext.h"
#include "utils/vk_enum_conversions.h"

namespace gfx::vk
{
    Image::Image(const Builder &builder) : gfx::Image(builder) {
        auto imageCreateFlags = ::vk::ImageCreateFlags();
        if (arrayLayers > 1 && type == Type::e2D) imageCreateFlags |= ::vk::ImageCreateFlagBits::e2DArrayCompatibleKHR;
        if (arrayLayers == 6 && type == Type::e2D) imageCreateFlags |= ::vk::ImageCreateFlagBits::eCubeCompatible;


        const auto imageCreateInfo = ::vk::ImageCreateInfo()
            .setImageType(getVkImageType(type))
            .setFormat(getVkFormat(format))
            .setExtent(::vk::Extent3D(extent.x, extent.y, extent.z))
            .setMipLevels(mipLevels)
            .setArrayLayers(arrayLayers)
            .setSamples(getVkSampleCount(msaa))
            .setTiling(::vk::ImageTiling::eOptimal)
            .setUsage(getVkUsage(usage))
            .setSharingMode(::vk::SharingMode::eExclusive)
            .setInitialLayout(::vk::ImageLayout::eUndefined)
            .setFlags(imageCreateFlags);

        std::tie(_handle, _allocation) = Context::Allocator().AllocateImage(imageCreateInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);

        TransitionLayout(::vk::ImageLayout::eGeneral);
    }

    Image::~Image() {
        if (_handle && _allocation) {
            Context::Allocator().FreeImage(_handle, _allocation);
        }
    }

    void Image::CopyFrom(const gfx::Buffer &buffer, const glm::u32 mipLevel, const glm::u32 layer) const {

        if (!(usage & Usage::eTransferDst)) {
            throw std::runtime_error("Attempting to copy data to an image that does not have the TransferDst usage flag set!");
        }
        if (mipLevels <= mipLevel) {
            throw std::runtime_error("Attempting to copy data to a mip level that does not exist!");
        }
        if (arrayLayers <= layer) {
            throw std::runtime_error("Attempting to copy data to an array layer that does not exist!");
        }

        const auto& vkBuffer = dynamic_cast<const gfx::vk::Buffer&>(buffer);

        Context::Device().runSingleTimeCommand([this, &buffer, layer, mipLevel, &vkBuffer] (const gfx::vk::CommandBuffer& commandBuffer) {
            TransitionLayout(commandBuffer, ::vk::ImageLayout::eTransferDstOptimal);

            auto extent = this->extent;
            for (uint32_t i = 0; i < mipLevel; i++) {
                extent.x = std::max<uint32_t>(1u, extent.x / 2);
                extent.y = std::max<uint32_t>(1u, extent.y / 2);
                extent.z = std::max<uint32_t>(1u, extent.z / 2);
            }

            const auto region = ::vk::BufferImageCopy()
                .setBufferOffset(0)
                .setBufferRowLength(0)
                .setBufferImageHeight(0)
                .setImageSubresource(::vk::ImageSubresourceLayers()
                    .setAspectMask(::vk::ImageAspectFlagBits::eColor)
                    .setMipLevel(mipLevel)
                    .setBaseArrayLayer(layer)
                    .setLayerCount(1))
                .setImageOffset({ 0, 0, 0 })
                .setImageExtent(::vk::Extent3D(extent.x, extent.y, extent.z));
            commandBuffer->copyBufferToImage(*vkBuffer, _handle, ::vk::ImageLayout::eTransferDstOptimal, region);

            TransitionLayout(commandBuffer, ::vk::ImageLayout::eGeneral);
        }, ::vk::QueueFlagBits::eTransfer);
    }

    Image::Image(const ::vk::Image _surfaceImage, glm::uvec2 extent, Format format, MSAA msaa)
        : Image(Builder()
            .setType(Type::e2D)
            .setExtent(extent)
            .addUsage(Usage::eTransferDst)
            .addUsage(Usage::eColorAttachment)
            .setArrayLayers(1)
            .setMipLevels(1)
            .setFormat(format)
            .setMSAA(msaa)) {
        _handle = _surfaceImage;
    }

    ::vk::ImageLayout Image::getImageLayout() const
    {
        return _layout;
    }

    void Image::TransitionLayout(const gfx::vk::CommandBuffer& commandBuffer, const ::vk::ImageLayout newLayout) const {
        if (_layout == newLayout) {
            return;
        }

        const ::vk::PipelineStageFlags sourceStage = layoutPipelineStageMap.at(_layout);
        const ::vk::PipelineStageFlags destinationStage = layoutPipelineStageMap.at(newLayout);
        const ::vk::AccessFlags srcAccessMask = layoutAccessMap.at(_layout);
        const ::vk::AccessFlags dstAccessMask = layoutAccessMap.at(newLayout);

        Barrier(commandBuffer, srcAccessMask, dstAccessMask, sourceStage, destinationStage, newLayout);

        _layout = newLayout;
    }

    void Image::TransitionLayout(const ::vk::ImageLayout newLayout) const {
        if (_layout == newLayout) {
            return;
        }

        Context::Device().runSingleTimeCommand([this, newLayout] (const gfx::vk::CommandBuffer &commandBuffer) {
            TransitionLayout(commandBuffer, newLayout);
        }, ::vk::QueueFlagBits::eGraphics);
    }

    void Image::Barrier(const gfx::vk::CommandBuffer& commandBuffer,
        const ::vk::AccessFlags srcAccessMask, const ::vk::AccessFlags dstAccessMask,
        const ::vk::PipelineStageFlags srcStage, const ::vk::PipelineStageFlags dstStage,
        const ::vk::ImageLayout newLayout
    ) const {

        ::vk::ImageAspectFlags aspectMask = {};
        if (usage & Usage::eDepthStencilAttachment) {
            aspectMask = ::vk::ImageAspectFlagBits::eDepth;
            if (IsStencilFormat(format)) {
                aspectMask |= ::vk::ImageAspectFlagBits::eStencil;
            }
        } else {
            aspectMask = ::vk::ImageAspectFlagBits::eColor;
        }

        const auto imageBarrier = ::vk::ImageMemoryBarrier()
            .setOldLayout(_layout)
            .setNewLayout(newLayout)
            .setImage(_handle)
            .setSubresourceRange(::vk::ImageSubresourceRange()
                .setAspectMask(aspectMask)
                .setBaseMipLevel(0)
                .setLevelCount(mipLevels)
                .setBaseArrayLayer(0)
                .setLayerCount(arrayLayers))
            .setSrcAccessMask(srcAccessMask)
            .setDstAccessMask(dstAccessMask);

        commandBuffer->pipelineBarrier(
            srcStage,
            dstStage,
            ::vk::DependencyFlags(),
            nullptr,
            nullptr,
            imageBarrier);
    }

    void Image::Clear(const gfx::vk::CommandBuffer& commandBuffer, const ::vk::ClearValue& clearValue) const {
        ::vk::ImageAspectFlags aspectMask = ::vk::ImageAspectFlagBits::eColor;
        if (IsDepthStencilFormat(format)) {
            aspectMask = ::vk::ImageAspectFlagBits::eDepth;
            if (IsStencilFormat(format)) {
                aspectMask |= ::vk::ImageAspectFlagBits::eStencil;
            }
        }

        const auto range = ::vk::ImageSubresourceRange()
            .setAspectMask(aspectMask)
            .setBaseMipLevel(0)
            .setLevelCount(mipLevels)
            .setBaseArrayLayer(0)
            .setLayerCount(arrayLayers);

        if (aspectMask & ::vk::ImageAspectFlagBits::eDepth) {
            commandBuffer->clearDepthStencilImage(
                _handle,
                _layout,
                clearValue.depthStencil,
                range);
            return;
        }
        commandBuffer->clearColorImage(
            _handle,
            _layout,
            clearValue.color,
            range);
    }

    void Image::Clear(const ::vk::ClearValue& clearValue) const {
        Context::Device().runSingleTimeCommand([this, clearValue](const gfx::vk::CommandBuffer& commandBuffer) {
            Clear(commandBuffer, clearValue);
        }, ::vk::QueueFlagBits::eGraphics);
    }

    void Image::Resize(const glm::uvec3 &extent) {
        if (this->extent == extent || (extent.x == 0 || extent.y == 0 || extent.z == 0))
            return;

        Context::Allocator().FreeImage(_handle, _allocation);

        this->extent = extent;
        const auto imageCreateInfo = ::vk::ImageCreateInfo()
            .setImageType(getVkImageType(type))
            .setFormat(getVkFormat(format))
            .setExtent(::vk::Extent3D(extent.x, extent.y, extent.z))
            .setMipLevels(mipLevels)
            .setArrayLayers(arrayLayers)
            .setSamples(getVkSampleCount(msaa))
            .setTiling(::vk::ImageTiling::eOptimal)
            .setUsage(getVkUsage(usage))
            .setSharingMode(::vk::SharingMode::eExclusive)
            .setInitialLayout(::vk::ImageLayout::eUndefined)
            .setFlags(arrayLayers == 6 ? ::vk::ImageCreateFlagBits::eCubeCompatible : ::vk::ImageCreateFlags());

        std::tie(_handle, _allocation) = Context::Allocator().AllocateImage(imageCreateInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);

        if (_layout != ::vk::ImageLayout::eUndefined) {
            const auto layout = _layout;
            _layout = ::vk::ImageLayout::eUndefined;
            TransitionLayout(layout);
        }
    }

    std::vector<std::byte> Image::ReadData(glm::u32 mipLevel, glm::u32 arrayLayer) const
    {
        if (!(usage & Usage::eTransferSrc)) {
            throw std::runtime_error("Attempting to read data from an image that does not have the TransferSrc usage flag set!");
        }
        if (mipLevels <= mipLevel) {
            throw std::runtime_error("Attempting to read data from a mip level that does not exist!");
        }
        if (arrayLayers <= arrayLayer) {
            throw std::runtime_error("Attempting to read data from an array layer that does not exist!");
        }

        const auto bufferSize = extent.x * extent.y * extent.z * Image::PixelSizeFromImageFormat(format) * Image::ChannelCountFromImageFormat(format);

        const auto stagingBuffer = Buffer::Builder()
            .setSize(bufferSize)
            .addUsage(Buffer::Usage::eTexel)
            .addUsage(Buffer::Usage::eTransferDst)
            .addMemoryProperty(Buffer::MemoryProperty::eHostCoherent)
            .addMemoryProperty(Buffer::MemoryProperty::eHostVisible)
            .build();
        const auto& vkStagingBuffer = dynamic_cast<const gfx::vk::Buffer&>(*stagingBuffer);

        Context::Device().runSingleTimeCommand([this, &stagingBuffer, mipLevel, arrayLayer, &vkStagingBuffer] (const gfx::vk::CommandBuffer& commandBuffer) {
            TransitionLayout(commandBuffer, ::vk::ImageLayout::eTransferSrcOptimal);

            const auto region = ::vk::BufferImageCopy()
                .setBufferOffset(0)
                .setBufferRowLength(0)
                .setBufferImageHeight(0)
                .setImageSubresource(::vk::ImageSubresourceLayers()
                    .setAspectMask(::vk::ImageAspectFlagBits::eColor)
                    .setMipLevel(mipLevel)
                    .setBaseArrayLayer(arrayLayer)
                    .setLayerCount(1))
                .setImageOffset({ 0, 0, 0 })
                .setImageExtent(::vk::Extent3D(extent.x, extent.y, extent.z));
            commandBuffer->copyImageToBuffer(_handle, ::vk::ImageLayout::eTransferSrcOptimal, *vkStagingBuffer, region);

            TransitionLayout(commandBuffer, ::vk::ImageLayout::eGeneral);
        }, ::vk::QueueFlagBits::eTransfer);

        stagingBuffer->Map();
        auto data = stagingBuffer->Read(0, bufferSize);
        stagingBuffer->Unmap();
        return std::vector<std::byte>(data.begin(), data.end());
    }

    void Image::GenerateMipmaps() const
    {
        if (mipLevels == 1) {
            return;
        }

        gfx::vk::Context::Device().runSingleTimeCommand([this] (const gfx::vk::CommandBuffer &commandBuffer) {
            auto mipWidth = static_cast<glm::i32>(extent.x);
            auto mipHeight = static_cast<glm::i32>(extent.y);

            auto barrier = ::vk::ImageMemoryBarrier();
            for (uint32_t i = 1; i < mipLevels; i++) {
                barrier = ::vk::ImageMemoryBarrier()
                    .setOldLayout(::vk::ImageLayout::eTransferDstOptimal)
                    .setNewLayout(::vk::ImageLayout::eTransferSrcOptimal)
                    .setSrcQueueFamilyIndex(::vk::QueueFamilyIgnored)
                    .setDstQueueFamilyIndex(::vk::QueueFamilyIgnored)
                    .setImage(_handle)
                    .setSrcAccessMask(::vk::AccessFlagBits::eTransferWrite)
                    .setDstAccessMask(::vk::AccessFlagBits::eTransferRead)
                    .setSubresourceRange(::vk::ImageSubresourceRange()
                        .setAspectMask(::vk::ImageAspectFlagBits::eColor)
                        .setBaseMipLevel(i - 1)
                        .setLevelCount(1)
                        .setBaseArrayLayer(0)
                        .setLayerCount(arrayLayers));

                commandBuffer->pipelineBarrier(
                    ::vk::PipelineStageFlagBits::eTransfer,
                    ::vk::PipelineStageFlagBits::eTransfer,
                    ::vk::DependencyFlags(),
                    nullptr,
                    nullptr,
                    barrier);

                const auto imageBlit = ::vk::ImageBlit()
                    .setSrcSubresource(::vk::ImageSubresourceLayers()
                        .setAspectMask(::vk::ImageAspectFlagBits::eColor)
                        .setMipLevel(i - 1)
                        .setBaseArrayLayer(0)
                        .setLayerCount(arrayLayers))
                    .setSrcOffsets({
                        ::vk::Offset3D(0, 0, 0),
                        ::vk::Offset3D(mipWidth, mipHeight, 1)
                    })
                    .setDstSubresource(::vk::ImageSubresourceLayers()
                        .setAspectMask(::vk::ImageAspectFlagBits::eColor)
                        .setMipLevel(i)
                        .setBaseArrayLayer(0)
                        .setLayerCount(arrayLayers))
                    .setDstOffsets({
                        ::vk::Offset3D(0, 0, 0),
                        ::vk::Offset3D(
                            std::max<uint32_t>(1u, mipWidth / 2),
                            std::max<uint32_t>(1u, mipHeight / 2),
                            1)
                    });

                commandBuffer->blitImage(
                    _handle,
                    ::vk::ImageLayout::eTransferSrcOptimal,
                    _handle,
                    ::vk::ImageLayout::eTransferDstOptimal,
                    imageBlit,
                    ::vk::Filter::eLinear);

                barrier = ::vk::ImageMemoryBarrier()
                    .setOldLayout(::vk::ImageLayout::eTransferSrcOptimal)
                    .setNewLayout(::vk::ImageLayout::eShaderReadOnlyOptimal)
                    .setSrcQueueFamilyIndex(::vk::QueueFamilyIgnored)
                    .setDstQueueFamilyIndex(::vk::QueueFamilyIgnored)
                    .setImage(_handle)
                    .setSrcAccessMask(::vk::AccessFlagBits::eTransferRead)
                    .setDstAccessMask(::vk::AccessFlagBits::eShaderRead)
                    .setSubresourceRange(::vk::ImageSubresourceRange()
                        .setAspectMask(::vk::ImageAspectFlagBits::eColor)
                        .setBaseMipLevel(i - 1)
                        .setLevelCount(1)
                        .setBaseArrayLayer(0)
                        .setLayerCount(arrayLayers));

                commandBuffer->pipelineBarrier(
                    ::vk::PipelineStageFlagBits::eTransfer,
                    ::vk::PipelineStageFlagBits::eAllGraphics,
                    ::vk::DependencyFlags(),
                    nullptr,
                    nullptr,
                    barrier);

                if (mipWidth > 1) {
                    mipWidth /= 2;
                }
                if (mipHeight > 1) {
                    mipHeight /= 2;
                }
            }

            barrier = ::vk::ImageMemoryBarrier()
                .setOldLayout(::vk::ImageLayout::eTransferDstOptimal)
                .setNewLayout(::vk::ImageLayout::eShaderReadOnlyOptimal)
                .setSrcQueueFamilyIndex(::vk::QueueFamilyIgnored)
                .setDstQueueFamilyIndex(::vk::QueueFamilyIgnored)
                .setImage(_handle)
                .setSrcAccessMask(::vk::AccessFlagBits::eTransferRead)
                .setDstAccessMask(::vk::AccessFlagBits::eShaderRead)
                .setSubresourceRange(::vk::ImageSubresourceRange()
                    .setAspectMask(::vk::ImageAspectFlagBits::eColor)
                    .setBaseMipLevel(mipLevels - 1)
                    .setLevelCount(1)
                    .setBaseArrayLayer(0)
                    .setLayerCount(arrayLayers));

            commandBuffer->pipelineBarrier(
                ::vk::PipelineStageFlagBits::eTransfer,
                ::vk::PipelineStageFlagBits::eFragmentShader,
                ::vk::DependencyFlags(),
                nullptr,
                nullptr,
                barrier);
        }, ::vk::QueueFlagBits::eGraphics);

        // TransitionLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
        _layout = ::vk::ImageLayout::eShaderReadOnlyOptimal;
    }
}
