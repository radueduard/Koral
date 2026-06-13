//
// Created by radue on 2/28/2026.
//

module;

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>

module gfx;
import :vk_allocator;
import :vk_context;
import :vk_device;

import std;

namespace gfx::vk
{
    Allocator::Allocator()
    {
        const VmaAllocatorCreateInfo allocatorInfo = {
            .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
            .physicalDevice = *vk::Context::Runtime().getPhysicalDevice(),
            .device = *vk::Context::Device(),
            .preferredLargeHeapBlockSize = 0,
            .pAllocationCallbacks = nullptr,
            .pDeviceMemoryCallbacks = nullptr,
            .pHeapSizeLimit = nullptr,
            .pVulkanFunctions = nullptr,
            .instance = Context::Runtime().getInstance(),
            .vulkanApiVersion = VK_API_VERSION_1_3,
            .pTypeExternalMemoryHandleTypes = nullptr
        };

        if (const auto result = vmaCreateAllocator(&allocatorInfo, &_allocator); result != VK_SUCCESS) {
            std::cerr << "Failed to create VMA allocator: " << ::vk::to_string(static_cast<::vk::Result>(result)) << std::endl;
            throw std::runtime_error("Failed to create VMA allocator");
        }
    }

    Allocator::~Allocator()
    {
        if (_allocator != VK_NULL_HANDLE) {
            vmaDestroyAllocator(_allocator);
        }
    }

    std::pair<::vk::Buffer, VmaAllocation> Allocator::AllocateBuffer(const ::vk::BufferCreateInfo& bufferInfo,
        const VmaMemoryUsage usage, const VmaAllocationCreateFlags flags) const
    {
        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = usage;
        allocInfo.flags = flags;

        VkBuffer buffer;
        VmaAllocation allocation;
        if (const auto result = vmaCreateBuffer(
            _allocator,
            reinterpret_cast<const VkBufferCreateInfo*>(&bufferInfo),
            &allocInfo,
            &buffer,
            &allocation,
            nullptr
        ); result != VK_SUCCESS) {
            std::cerr << "Failed to create buffer: " << ::vk::to_string(static_cast<::vk::Result>(result)) << std::endl;
            throw std::runtime_error("Failed to allocate buffer");
        }


        return { buffer, allocation };
    }

    void Allocator::FreeBuffer(const ::vk::Buffer buffer, const VmaAllocation allocation) const
    {
        vmaDestroyBuffer(_allocator, buffer, allocation);
    }

    std::pair<::vk::Image, VmaAllocation> Allocator::AllocateImage(const ::vk::ImageCreateInfo& imageInfo,
        const VmaMemoryUsage usage, const VmaAllocationCreateFlags flags) const
    {
        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = usage;
        allocInfo.flags = flags;

        VkImage image;
        VmaAllocation allocation;
        if (const auto result = vmaCreateImage(
            _allocator,
            reinterpret_cast<const VkImageCreateInfo*>(&imageInfo),
            &allocInfo,
            &image,
            &allocation,
            nullptr
        ); result != VK_SUCCESS) {
            std::cerr << "Failed to create image: " << ::vk::to_string(static_cast<::vk::Result>(result)) << std::endl;
            throw std::runtime_error("Failed to allocate image");
        }

        return { image, allocation };
    }

    void Allocator::FreeImage(const ::vk::Image image, const VmaAllocation allocation) const
    {
        vmaDestroyImage(_allocator, image, allocation);
    }

    void* Allocator::MapMemory(const VmaAllocation allocation) const
    {
        void* data;
        if (const auto result = vmaMapMemory(_allocator, allocation, &data); result != VK_SUCCESS) {
            std::cerr << "Failed to map memory: " << ::vk::to_string(static_cast<::vk::Result>(result)) << std::endl;
            throw std::runtime_error("Failed to map memory");
        }
        return data;
    }

    void Allocator::UnmapMemory(const VmaAllocation allocation) const
    {
        vmaUnmapMemory(_allocator, allocation);
    }

    void Allocator::FlushAllocation(const VmaAllocation allocation, size_t offset, size_t size) const {
        if (const auto result = vmaFlushAllocation(_allocator, allocation, offset, size); result != VK_SUCCESS) {
            std::cerr << "Failed to flush allocation: " << ::vk::to_string(static_cast<::vk::Result>(result)) << std::endl;
            throw std::runtime_error("Failed to flush allocation");
        }
    }

    void Allocator::InvalidateAllocation(const VmaAllocation allocation, size_t offset, size_t size) const {
        if (const auto result = vmaInvalidateAllocation(_allocator, allocation, offset, size); result != VK_SUCCESS) {
            std::cerr << "Failed to invalidate allocation: " << ::vk::to_string(static_cast<::vk::Result>(result)) << std::endl;
            throw std::runtime_error("Failed to invalidate allocation");
        }
    }


    VmaAllocationInfo Allocator::GetAllocationInfo(const VmaAllocation allocation) const
    {
        VmaAllocationInfo allocInfo;
        vmaGetAllocationInfo(_allocator, allocation, &allocInfo);
        return allocInfo;
    }
}
