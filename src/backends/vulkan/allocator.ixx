//
// Created by radue on 2/28/2026.
//

module;

#include <utility>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>

export module vk.allocator;
import vk.runtime;

namespace gfx::vk
{
    export class Allocator {
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
    	void FlushAllocation(const VmaAllocation allocation, size_t offset = 0, size_t size = VK_WHOLE_SIZE) const;
		void InvalidateAllocation(const VmaAllocation allocation, size_t offset = 0, size_t size = VK_WHOLE_SIZE) const;

		[[nodiscard]] VmaAllocationInfo GetAllocationInfo(const VmaAllocation allocation) const;

	private:
		VmaAllocator _allocator = VK_NULL_HANDLE;
	};
}
