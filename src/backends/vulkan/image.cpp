//
// Created by radue on 2/28/2026.
//

#include "image.h"

#include <image.h>
#include <scheduler.h>

#include <magic_enum/magic_enum.hpp>

#include "buffer.h"
#include "context.h"
#include "device.h"
#include "vulkanContext.h"
#include "vk_enum_conversions.h"

namespace gfx::vk
{
    Image::Image(const Builder &builder) : gfx::Image(builder) {
        auto imageCreateFlags = ::vk::ImageCreateFlags();
        // if (_type == Type::e3D) imageCreateFlags |= ::vk::ImageCreateFlagBits::e2DArrayCompatibleKHR;
        if (_arrayLayers == 6 && _type == Type::e2D) imageCreateFlags |= ::vk::ImageCreateFlagBits::eCubeCompatible;


        const auto type = getVkImageType(this->_type);
        const auto format = getVkFormat(_format);
        const auto usage = getVkUsage(this->_usage);

        auto tiling = ::vk::ImageTiling::eOptimal;
        try {
             (void)Context::Runtime().getPhysicalDevice()->getImageFormatProperties(format, type, ::vk::ImageTiling::eOptimal, usage, imageCreateFlags);
        } catch (const std::runtime_error& err) {
            try {
                tiling = ::vk::ImageTiling::eLinear;
                (void)Context::Runtime().getPhysicalDevice()->getImageFormatProperties(format, type, ::vk::ImageTiling::eLinear, usage, imageCreateFlags);
            } catch (const std::runtime_error& err) {
                throw std::runtime_error("The specified image format is not supported by the physical device!");
            }
        }

        const auto imageCreateInfo = ::vk::ImageCreateInfo()
            .setImageType(type)
            .setFormat(format)
            .setExtent(::vk::Extent3D(_extent.x, _extent.y, _extent.z))
            .setMipLevels(_mipLevels)
            .setArrayLayers(_arrayLayers)
            .setSamples(getVkSampleCount(_msaa))
            .setTiling(tiling)
            .setUsage(usage)
            .setSharingMode(::vk::SharingMode::eExclusive)
            .setInitialLayout(::vk::ImageLayout::eUndefined)
            .setFlags(imageCreateFlags);

        const auto frameCount = _isPerFrame ? gfx::Context::Scheduler().getImageCount() : 1;
        for (uint32_t i = 0; i < frameCount; i++)
        {
            auto [image, allocation] = Context::Allocator().AllocateImage(imageCreateInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);
            _images.push_back(image);
            _allocations.push_back(allocation);
            _layouts.push_back(::vk::ImageLayout::eUndefined);
        }
        TransitionLayout(::vk::ImageLayout::eGeneral);
    }

    Image::~Image() {
        if (!_allocations.empty()) {
            for (size_t i = 0; i < _images.size(); i++) {
                Context::Allocator().FreeImage(_images[i], _allocations[i]);
            }
        }
    }

    void Image::CopyFrom(const gfx::Buffer &buffer, const glm::u32 mipLevel, const glm::u32 layer) const {

        if (!(_usage & Usage::eTransferDst)) {
            throw std::runtime_error("Attempting to copy data to an image that does not have the TransferDst usage flag set!");
        }
        if (_mipLevels <= mipLevel) {
            throw std::runtime_error("Attempting to copy data to a mip level that does not exist!");
        }
        if (_arrayLayers <= layer) {
            throw std::runtime_error("Attempting to copy data to an array layer that does not exist!");
        }

        const auto& vkBuffer = dynamic_cast<const gfx::vk::Buffer&>(buffer);

        Context::Device().runSingleTimeCommand([this, &buffer, layer, mipLevel, &vkBuffer] (const gfx::vk::CommandBuffer& commandBuffer) {

            auto extent = this->_extent;
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

            const auto currentFrame = _isPerFrame ? gfx::Context::Scheduler().getCurrentImageIndex() : 0;
            TransitionLayout(commandBuffer, ::vk::ImageLayout::eTransferDstOptimal);
            commandBuffer->copyBufferToImage(*vkBuffer, _images[currentFrame], ::vk::ImageLayout::eTransferDstOptimal, region);
            TransitionLayout(commandBuffer, ::vk::ImageLayout::eGeneral);
        }, ::vk::QueueFlagBits::eTransfer);
    }

    Image::Image(const std::vector<::vk::Image>& surfaceImages, const glm::uvec2 extent, const Format format, const MSAA msaa)
        : gfx::Image(Builder()
            .setIsPerFrame(true)
            .setType(Type::e2D)
            .setExtent(extent)
            .addUsage(Usage::eTransferDst)
            .addUsage(Usage::eColorAttachment)
            .setArrayLayers(1)
            .setMipLevels(1)
            .setFormat(format)
            .setMSAA(msaa)) {
        _images = surfaceImages;
        for (const auto& _ : surfaceImages) {
            _layouts.push_back(::vk::ImageLayout::eUndefined);
        }
        TransitionLayout(::vk::ImageLayout::ePresentSrcKHR);
    }

    ::vk::ImageLayout Image::getImageLayout() const
    {
        const auto currentFrame = _isPerFrame ? gfx::Context::Scheduler().getCurrentImageIndex() : 0;
        return _layouts[currentFrame];
    }

    ::vk::Image Image::operator*() const
    {
        auto currentFrame = _isPerFrame ? gfx::Context::Scheduler().getCurrentImageIndex() : 0;
        return _images[currentFrame];
    }

    VmaAllocation Image::getAllocation() const
    {
        const auto currentFrame = _isPerFrame ? gfx::Context::Scheduler().getCurrentImageIndex() : 0;
        return _allocations[currentFrame];
    }

    void Image::TransitionLayout(const gfx::vk::CommandBuffer& commandBuffer, const ::vk::ImageLayout newLayout) const {
        if (const auto schedulerStarted = gfx::Context::Scheduler().hasStarted(); !schedulerStarted && _isPerFrame)
        {
            for (size_t i = 0; i < _layouts.size(); i++) {
                if (_layouts[i] != newLayout) {
                    const ::vk::PipelineStageFlags sourceStage = srcLayoutPipelineStageMap.at(_layouts[i]);
                    const ::vk::PipelineStageFlags destinationStage = dstLayoutPipelineStageMap.at(newLayout);
                    const ::vk::AccessFlags srcAccessMask = layoutAccessMap.at(_layouts[i]);
                    const ::vk::AccessFlags dstAccessMask = layoutAccessMap.at(newLayout);

                    Barrier(commandBuffer, srcAccessMask, dstAccessMask, sourceStage, destinationStage, newLayout, i);

                    _layouts[i] = newLayout;
                }
            }
        } else {
            const auto frameIndex = _isPerFrame ? gfx::Context::Scheduler().getCurrentImageIndex() : 0;
            if (_layouts[frameIndex] == newLayout) {
                return;
            }

            const ::vk::PipelineStageFlags sourceStage = srcLayoutPipelineStageMap.at(_layouts[frameIndex]);
            const ::vk::PipelineStageFlags destinationStage = dstLayoutPipelineStageMap.at(newLayout);
            const ::vk::AccessFlags srcAccessMask = layoutAccessMap.at(_layouts[frameIndex]);
            const ::vk::AccessFlags dstAccessMask = layoutAccessMap.at(newLayout);

            Barrier(commandBuffer, srcAccessMask, dstAccessMask, sourceStage, destinationStage, newLayout, frameIndex);

            _layouts[frameIndex] = newLayout;
        }
    }

    void Image::TransitionLayout(const ::vk::ImageLayout newLayout) const {
        // if (const auto _layout = getImageLayout(); _layout == newLayout) {
        //     return;
        // }

        Context::Device().runSingleTimeCommand([this, newLayout] (const gfx::vk::CommandBuffer &commandBuffer) {
            TransitionLayout(commandBuffer, newLayout);
        }, ::vk::QueueFlagBits::eGraphics);
    }

    void Image::Barrier(const gfx::vk::CommandBuffer& commandBuffer,
        const ::vk::AccessFlags srcAccessMask, const ::vk::AccessFlags dstAccessMask,
        const ::vk::PipelineStageFlags srcStage, const ::vk::PipelineStageFlags dstStage,
        const ::vk::ImageLayout newLayout, glm::i32 frame
    ) const {
        if (frame == -1) {
            frame = _isPerFrame ? gfx::Context::Scheduler().getCurrentImageIndex() : 0;
        }
        const auto _layout = _layouts[frame];
        const auto _handle = _images[frame];

        ::vk::ImageAspectFlags aspectMask = {};
        if (_usage & Usage::eDepthStencilAttachment) {
            aspectMask = ::vk::ImageAspectFlagBits::eDepth;
            if (IsStencilFormat(_format)) {
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
                .setLevelCount(_mipLevels)
                .setBaseArrayLayer(0)
                .setLayerCount(_arrayLayers))
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
        const auto _layout = getImageLayout();
        const auto _handle = **this;

        ::vk::ImageAspectFlags aspectMask = ::vk::ImageAspectFlagBits::eColor;
        if (IsDepthStencilFormat(_format)) {
            aspectMask = ::vk::ImageAspectFlagBits::eDepth;
            if (IsStencilFormat(_format)) {
                aspectMask |= ::vk::ImageAspectFlagBits::eStencil;
            }
        }

        const auto range = ::vk::ImageSubresourceRange()
            .setAspectMask(aspectMask)
            .setBaseMipLevel(0)
            .setLevelCount(_mipLevels)
            .setBaseArrayLayer(0)
            .setLayerCount(_arrayLayers);

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
        const auto _handle = **this;
        const auto _allocation = getAllocation();
        const auto _layout = getImageLayout();

        if (this->_extent == extent || (extent.x == 0 || extent.y == 0 || extent.z == 0))
            return;

        Context::Allocator().FreeImage(_handle, _allocation);

        this->_extent = extent;
        const auto imageCreateInfo = ::vk::ImageCreateInfo()
            .setImageType(getVkImageType(_type))
            .setFormat(getVkFormat(_format))
            .setExtent(::vk::Extent3D(extent.x, extent.y, extent.z))
            .setMipLevels(_mipLevels)
            .setArrayLayers(_arrayLayers)
            .setSamples(getVkSampleCount(_msaa))
            .setTiling(::vk::ImageTiling::eOptimal)
            .setUsage(getVkUsage(_usage))
            .setSharingMode(::vk::SharingMode::eExclusive)
            .setInitialLayout(::vk::ImageLayout::eUndefined)
            .setFlags(_arrayLayers == 6 ? ::vk::ImageCreateFlagBits::eCubeCompatible : ::vk::ImageCreateFlags());

        const auto frameIndex = _isPerFrame ? gfx::Context::Scheduler().getCurrentImageIndex() : 0;
        auto [image, allocation] = Context::Allocator().AllocateImage(imageCreateInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);
        _images[frameIndex] = image;
        _allocations[frameIndex] = allocation;

        if (_layout != ::vk::ImageLayout::eUndefined) {
            _layouts[frameIndex] = ::vk::ImageLayout::eUndefined;
            TransitionLayout(_layout);
        }
    }

    std::vector<std::byte> Image::ReadData(glm::u32 mipLevel, glm::u32 arrayLayer) const
    {
        if (!(_usage & Usage::eTransferSrc)) {
            throw std::runtime_error("Attempting to read data from an image that does not have the TransferSrc usage flag set!");
        }
        if (_mipLevels <= mipLevel) {
            throw std::runtime_error("Attempting to read data from a mip level that does not exist!");
        }
        if (_arrayLayers <= arrayLayer) {
            throw std::runtime_error("Attempting to read data from an array layer that does not exist!");
        }

        const auto bufferSize = _extent.x * _extent.y * _extent.z * Image::PixelSizeFromImageFormat(_format) * Image::ChannelCountFromImageFormat(_format);

        const auto stagingBuffer = Buffer::Builder()
            .setSize(bufferSize)
            .addUsage(Buffer::Usage::eTexel)
            .addUsage(Buffer::Usage::eTransferDst)
            .addMemoryProperty(Buffer::MemoryProperty::eHostCoherent)
            .addMemoryProperty(Buffer::MemoryProperty::eHostVisible)
            .build();
        const auto& vkStagingBuffer = dynamic_cast<const gfx::vk::Buffer&>(*stagingBuffer);

        Context::Device().runSingleTimeCommand([this, mipLevel, arrayLayer, &vkStagingBuffer] (const gfx::vk::CommandBuffer& commandBuffer) {

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
                .setImageExtent(::vk::Extent3D(_extent.x, _extent.y, _extent.z));

            const auto _handle = **this;
            TransitionLayout(commandBuffer, ::vk::ImageLayout::eTransferSrcOptimal);
            commandBuffer->copyImageToBuffer(_handle, ::vk::ImageLayout::eTransferSrcOptimal, *vkStagingBuffer, region);
            TransitionLayout(commandBuffer, ::vk::ImageLayout::eGeneral);
        }, ::vk::QueueFlagBits::eTransfer);

        stagingBuffer->Map();
        auto data = stagingBuffer->Read(0, bufferSize);
        stagingBuffer->Unmap();
        return std::vector(data.begin(), data.end());
    }

    void Image::GenerateMipmaps()
    {
        if (_mipLevels == 1) {
            return;
        }

        gfx::vk::Context::Device().runSingleTimeCommand([this] (const gfx::vk::CommandBuffer &commandBuffer) {
            auto mipWidth = static_cast<glm::i32>(_extent.x);
            auto mipHeight = static_cast<glm::i32>(_extent.y);

            const auto _handle = **this;
            TransitionLayout(commandBuffer, ::vk::ImageLayout::eTransferDstOptimal);

            auto barrier = ::vk::ImageMemoryBarrier();
            for (uint32_t i = 1; i < _mipLevels; i++) {
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
                        .setLayerCount(_arrayLayers));

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
                        .setLayerCount(_arrayLayers))
                    .setSrcOffsets({
                        ::vk::Offset3D(0, 0, 0),
                        ::vk::Offset3D(mipWidth, mipHeight, 1)
                    })
                    .setDstSubresource(::vk::ImageSubresourceLayers()
                        .setAspectMask(::vk::ImageAspectFlagBits::eColor)
                        .setMipLevel(i)
                        .setBaseArrayLayer(0)
                        .setLayerCount(_arrayLayers))
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
                    .setNewLayout(::vk::ImageLayout::eGeneral)
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
                        .setLayerCount(_arrayLayers));

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
                .setNewLayout(::vk::ImageLayout::eGeneral)
                .setSrcQueueFamilyIndex(::vk::QueueFamilyIgnored)
                .setDstQueueFamilyIndex(::vk::QueueFamilyIgnored)
                .setImage(_handle)
                .setSrcAccessMask(::vk::AccessFlagBits::eTransferRead)
                .setDstAccessMask(::vk::AccessFlagBits::eShaderRead)
                .setSubresourceRange(::vk::ImageSubresourceRange()
                    .setAspectMask(::vk::ImageAspectFlagBits::eColor)
                    .setBaseMipLevel(_mipLevels - 1)
                    .setLevelCount(1)
                    .setBaseArrayLayer(0)
                    .setLayerCount(_arrayLayers));

            commandBuffer->pipelineBarrier(
                ::vk::PipelineStageFlagBits::eTransfer,
                ::vk::PipelineStageFlagBits::eFragmentShader,
                ::vk::DependencyFlags(),
                nullptr,
                nullptr,
                barrier);
        }, ::vk::QueueFlagBits::eGraphics);

        const auto frameIndex = _isPerFrame ? gfx::Context::Scheduler().getCurrentImageIndex() : 0;
        _layouts[frameIndex] = ::vk::ImageLayout::eGeneral;
    }
}
