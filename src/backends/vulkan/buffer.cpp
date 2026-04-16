//
// Created by radue on 2/28/2026.
//

#include "buffer.h"

#include "commandBuffer.h"
#include "device.h"
#include <scheduler.h>

#include "context.h"
#include "vk_enum_conversions.h"
#include "vulkanContext.h"

namespace gfx::vk
{
	// static ::vk::DeviceSize GetAlignment(const ::vk::DeviceSize size, const ::vk::DeviceSize alignment) {
	// 	if (alignment > 0) {
	// 		return (size + alignment - 1) & ~(alignment - 1);
	// 	}
	// 	return size;
	// }

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
			.setSize(_size)
			.setUsage(getVkBufferUsageFlags(_usage))
			.setSharingMode(::vk::SharingMode::eExclusive);


		const auto frameCount = _isPerFrame ? gfx::Context::Scheduler().getImageCount() : 1;
		for (int i = 0; i < frameCount; ++i)
		{
			auto [buffer, allocation] = Context::Allocator().AllocateBuffer(bufferInfo, memoryUsage, flags);
			_buffers.push_back(buffer);
			_allocations.push_back(allocation);
			_accessFlags.push_back(::vk::AccessFlagBits::eNone);
		}
	}

	Buffer::~Buffer() {
		Unmap();
		for (int i = 0; i < _buffers.size(); ++i)
		{
			Context::Allocator().FreeBuffer(_buffers[i], _allocations[i]);
		}
	}

	void Buffer::Map() const
	{
		const auto _allocation = getAllocation();
		_mappedPtr = vk::Context::Allocator().MapMemory(_allocation);
	}

	void Buffer::Unmap() const
	{
		const auto _allocation = getAllocation();
		if (_mappedPtr) {
			Context::Allocator().UnmapMemory(_allocation);
			_mappedPtr = nullptr;
		}
	}

	::vk::Buffer Buffer::operator*() const
	{
		return _buffers[_isPerFrame ? gfx::Context::Scheduler().getCurrentImageIndex() : 0];
	}

	void Buffer::CopyFrom(const gfx::Buffer& srcBuffer, glm::i64 srcOffset, glm::i64 dstOffset, glm::i64 size) const
	{
		Context::Device().runSingleTimeCommand([this, srcOffset, dstOffset, &srcBuffer, size](const gfx::vk::CommandBuffer& commandBuffer) {
			const auto frameCount = _isPerFrame ? gfx::Context::Scheduler().getImageCount() : 1;
			const auto& vkBuffer = dynamic_cast<const gfx::vk::Buffer&>(srcBuffer);
			for (int i = 0; i < frameCount; ++i) {
				const auto copyRegion = ::vk::BufferCopy()
					.setSrcOffset(srcOffset)
					.setDstOffset(dstOffset)
					.setSize(size);
				commandBuffer->copyBuffer(*vkBuffer, _buffers[i], 1, &copyRegion);
			}
		}, ::vk::QueueFlagBits::eTransfer);
	}

	::vk::AccessFlags Buffer::getAccessMask() const {
		const auto currentFrame = _isPerFrame ? gfx::Context::Scheduler().getCurrentImageIndex() : 0;
		return _accessFlags[currentFrame];
	}

	void Buffer::setAccessMask(const ::vk::AccessFlags access) const {
		const auto currentFrame = _isPerFrame ? gfx::Context::Scheduler().getCurrentImageIndex() : 0;
		_accessFlags[currentFrame] = access;
	}

	VmaAllocation Buffer::getAllocation() const {
		return  _allocations[_isPerFrame ? gfx::Context::Scheduler().getCurrentImageIndex() : 0];
	}


}
