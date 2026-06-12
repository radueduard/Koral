//
// Created by radue on 2/28/2026.
//

module;

#include <magic_enum/magic_enum.hpp>
#include "vk_enum_conversions.h"
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <vma/vk_mem_alloc.h>

module vk.image;
import vk.context;
import vk.runtime;
import vk.commandBuffer;
import gfx.context;
import gfx.structs;

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
            .setSamples(getVkSampleCount(_sampleCount))
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
            _allocations.emplace_back(allocation);
            for (uint32_t mipLevel = 0; mipLevel < _mipLevels; mipLevel++) {
                for (uint32_t arrayLayer = 0; arrayLayer < _arrayLayers; arrayLayer++) {
                    glm::u32 key = (i << 24) | (mipLevel << 12) | arrayLayer;
                    _layouts[key] = ::vk::ImageLayout::eUndefined;
                    _accessMasks[key] = ::vk::AccessFlagBits::eNone;
                }
            }
        }
    }

    Image::~Image() {
        if (!_allocations.empty()) {
            for (size_t i = 0; i < _images.size(); i++) {
                Context::Allocator().FreeImage(_images[i], _allocations[i]);
            }
        }
    }

    Image::Image(const std::vector<::vk::Image>& surfaceImages, const glm::uvec2 extent, const Format format, const SampleCount sampleCount)
        : gfx::Image(Builder()
            .setIsPerFrame(true)
            .setType(Type::e2D)
            .setExtent(extent)
            .addUsage(Usage::eTransferDst)
            .addUsage(Usage::eColorAttachment)
            .setArrayLayers(1)
            .setMipLevels(1)
            .setFormat(format)
            .setSampleCount(sampleCount)) {
        _images = surfaceImages;

        int frameIndex = 0;
        for (const auto& _ : surfaceImages) {
            glm::u32 key = (frameIndex << 24) | (0 << 12) | 0;
            _layouts[key] = ::vk::ImageLayout::eUndefined;
            _accessMasks[key] = ::vk::AccessFlagBits::eNone;
            frameIndex++;
        }
    }

    ::vk::ImageLayout Image::getImageLayout(const glm::u32 mipLevel, const glm::u32 arrayLayer) const
    {
        const auto currentFrame = _isPerFrame ? gfx::Context::Scheduler().getCurrentImageIndex() : 0;
        const auto key = (currentFrame << 24) | (mipLevel << 12) | arrayLayer;
        return _layouts[key];
    }

    ::vk::AccessFlags Image::getAccessMask(const glm::u32 mipLevel, const glm::u32 arrayLayer) const {
        const auto currentFrame = _isPerFrame ? gfx::Context::Scheduler().getCurrentImageIndex() : 0;
        const auto key = (currentFrame << 24) | (mipLevel << 12) | arrayLayer;
        return _accessMasks[key];
    }

    void Image::SetImageLayout(const ::vk::ImageLayout newLayout, const glm::u32 mipLevel, const glm::u32 arrayLayer) const {
        const auto currentFrame = _isPerFrame ? gfx::Context::Scheduler().getCurrentImageIndex() : 0;
        if (const uint32_t key = (currentFrame << 24) | (mipLevel << 12) | arrayLayer; _layouts[key] != newLayout) {
            _layouts[key] = newLayout;
        }
    }

    void Image::SetAccessMask(const ::vk::AccessFlags newAccessMask, const glm::u32 mipLevel, const glm::u32 arrayLayer) const {
        const auto currentFrame = _isPerFrame ? gfx::Context::Scheduler().getCurrentImageIndex() : 0;
        if (const auto key = (currentFrame << 24) | (mipLevel << 12) | arrayLayer; _accessMasks[key] != newAccessMask) {
            _accessMasks[key] = newAccessMask;
        }
    }

    ::vk::ImageAspectFlags Image::getAspectFlags() const {
        ::vk::ImageAspectFlags aspectMask = {};
        if (_usage & Usage::eDepthStencilAttachment) {
            aspectMask = ::vk::ImageAspectFlagBits::eDepth;
            if (IsStencilFormat(_format)) {
                aspectMask |= ::vk::ImageAspectFlagBits::eStencil;
            }
        } else {
            aspectMask = ::vk::ImageAspectFlagBits::eColor;
        }
        return aspectMask;
    }

    ::vk::Image Image::operator*() const
    {
        const auto currentFrame = _isPerFrame ? gfx::Context::Scheduler().getCurrentImageIndex() : 0;
        return _images[currentFrame];
    }

    VmaAllocation Image::getAllocation() const
    {
        const auto currentFrame = _isPerFrame ? gfx::Context::Scheduler().getCurrentImageIndex() : 0;
        return _allocations[currentFrame];
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
            .setSamples(getVkSampleCount(_sampleCount))
            .setTiling(::vk::ImageTiling::eOptimal)
            .setUsage(getVkUsage(_usage))
            .setSharingMode(::vk::SharingMode::eExclusive)
            .setInitialLayout(::vk::ImageLayout::eUndefined)
            .setFlags(_arrayLayers == 6 ? ::vk::ImageCreateFlagBits::eCubeCompatible : ::vk::ImageCreateFlags());

        const auto frameIndex = _isPerFrame ? gfx::Context::Scheduler().getCurrentImageIndex() : 0;
        auto [image, allocation] = Context::Allocator().AllocateImage(imageCreateInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);
        _images[frameIndex] = image;
        _allocations[frameIndex] = allocation;

        for (uint32_t mipLevel = 0; mipLevel < _mipLevels; mipLevel++) {
            for (uint32_t arrayLayer = 0; arrayLayer < _arrayLayers; arrayLayer++) {
                glm::u32 key = (frameIndex << 24) | (mipLevel << 12) | arrayLayer;
                _layouts[key] = ::vk::ImageLayout::eUndefined;
                _accessMasks[key] = ::vk::AccessFlagBits::eNone;
            }
        }
    }
}
