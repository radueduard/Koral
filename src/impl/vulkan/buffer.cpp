//
// Created by radue on 2/28/2026.
//

#include "buffer.h"

#include "commandBuffer.h"
#include "device.h"

namespace gfx::vk
{
	// static ::vk::DeviceSize GetAlignment(const ::vk::DeviceSize size, const ::vk::DeviceSize alignment) {
	// 	if (alignment > 0) {
	// 		return (size + alignment - 1) & ~(alignment - 1);
	// 	}
	// 	return size;
	// }

	::vk::BufferUsageFlags getVkBufferUsageFlags(const gfx::Flags<Buffer::Usage> usage)
	{
		auto vkUsage = ::vk::BufferUsageFlags();
		if (usage & Buffer::Usage::eVertex)
			vkUsage |= ::vk::BufferUsageFlagBits::eVertexBuffer;
		if (usage & Buffer::Usage::eIndex)
			vkUsage |= ::vk::BufferUsageFlagBits::eIndexBuffer;
		if (usage & Buffer::Usage::eIndirect)
			vkUsage |= ::vk::BufferUsageFlagBits::eIndirectBuffer;
		if (usage & Buffer::Usage::eStorage)
			vkUsage |= ::vk::BufferUsageFlagBits::eStorageBuffer;
		if (usage & Buffer::Usage::eTransferSrc)
			vkUsage |= ::vk::BufferUsageFlagBits::eTransferSrc;
		if (usage & Buffer::Usage::eTransferDst)
			vkUsage |= ::vk::BufferUsageFlagBits::eTransferDst;
		if (usage & Buffer::Usage::eUniform)
			vkUsage |= ::vk::BufferUsageFlagBits::eUniformBuffer;
		if (usage & Buffer::Usage::eTexel)
			vkUsage |= ::vk::BufferUsageFlagBits::eUniformTexelBuffer | ::vk::BufferUsageFlagBits::eStorageTexelBuffer;
		return vkUsage;
	}

	Buffer::Buffer(const Builder& builder) : gfx::Buffer(builder) {
		VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_UNKNOWN;
		if (builder.memoryProperties & MemoryProperty::eDeviceLocal)
			memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
		if (builder.memoryProperties & MemoryProperty::eHostVisible
			|| builder.memoryProperties & MemoryProperty::eHostCoherent
			|| builder.memoryProperties & MemoryProperty::eHostCached)
			memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;

		VmaAllocationCreateFlags flags = 0;
		if (builder.memoryProperties & MemoryProperty::eHostVisible)
			flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
		if (builder.memoryProperties & MemoryProperty::eHostCached)
			flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;


		const auto bufferInfo = ::vk::BufferCreateInfo()
			.setSize(size)
			.setUsage(getVkBufferUsageFlags(usage))
			.setSharingMode(::vk::SharingMode::eExclusive);

		auto [buffer, allocation] = Context::Allocator().AllocateBuffer(bufferInfo, memoryUsage, flags);
		_handle = buffer;
		_allocation = allocation;
	}

	Buffer::~Buffer() {
		Unmap();
		Context::Allocator().FreeBuffer(_handle, _allocation);
	}

	void Buffer::Map() const
	{
		_mappedPtr = vk::Context::Allocator().MapMemory(_allocation);
	}

	void Buffer::Unmap() const
	{
		if (_mappedPtr) {
			Context::Allocator().UnmapMemory(_allocation);
			_mappedPtr = nullptr;
		}
	}

	void Buffer::CopyFrom(const gfx::Buffer& srcBuffer, glm::i64 srcOffset, glm::i64 dstOffset, glm::i64 size) const {
		Context::Device().runSingleTimeCommand([this, srcOffset, dstOffset, &srcBuffer, size](const gfx::vk::CommandBuffer& commandBuffer) {
			const auto& vkBuffer = dynamic_cast<const gfx::vk::Buffer&>(srcBuffer);
			const auto copyRegion = ::vk::BufferCopy()
				.setSrcOffset(srcOffset)
				.setDstOffset(dstOffset)
				.setSize(size);
			commandBuffer->copyBuffer(*vkBuffer, _handle, 1, &copyRegion);
		}, ::vk::QueueFlagBits::eTransfer);
	}

	VmaAllocation Buffer::getAllocation() const {
		return _allocation;
	}


}
