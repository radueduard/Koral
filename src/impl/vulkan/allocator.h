//
// Created by radue on 2/28/2026.
//

#pragma once

#include <vma/vk_mem_alloc.h>

#include "runtime.h"

namespace gfx::vk
{
    class Allocator {
	public:
		Allocator();

		~Allocator();

		Allocator(const Allocator&) = delete;
		Allocator& operator=(const Allocator&) = delete;

		[[nodiscard]] std::pair<::vk::Buffer, VmaAllocation> AllocateBuffer (
			const ::vk::BufferCreateInfo& bufferInfo,
			const VmaMemoryUsage usage,
			const VmaAllocationCreateFlags flags = 0
		) const;

		void FreeBuffer(const ::vk::Buffer buffer, const VmaAllocation allocation) const;

		[[nodiscard]] std::pair<::vk::Image, VmaAllocation> AllocateImage(
			const ::vk::ImageCreateInfo& imageInfo,
			const VmaMemoryUsage usage,
			const VmaAllocationCreateFlags flags = 0
		) const;

		void FreeImage(const ::vk::Image image, const VmaAllocation allocation) const;

		void* MapMemory(const VmaAllocation allocation) const;

		void UnmapMemory(const VmaAllocation allocation) const;

		[[nodiscard]] VmaAllocationInfo GetAllocationInfo(const VmaAllocation allocation) const;

	private:
		VmaAllocator _allocator = VK_NULL_HANDLE;
	};
}
