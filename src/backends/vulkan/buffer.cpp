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

	Buffer::Buffer(const RawBuilder& builder) : gfx::Buffer(builder) {
		VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_UNKNOWN;
		VmaAllocationCreateFlags flags = 0;
		switch (_type) {
		case Type::eDeviceLocal:
			memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
			break;
		case Type::eStaging:
			memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
			flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
			break;
		case Type::eReadback:
			memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
			flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
			break;
		case Type::eDynamic:
			memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
			flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
			break;
		}

		const auto bufferInfo = ::vk::BufferCreateInfo()
			.setSize(_size)
			.setUsage(getVkBufferUsageFlags(_usage))
			.setSharingMode(::vk::SharingMode::eExclusive);


		const auto frameCount = _isPerFrame ? gfx::Context::Scheduler().getImageCount() : 1;
		for (int i = 0; i < frameCount; ++i)
		{
			auto [buffer, allocation] = Context::Allocator().AllocateBuffer(bufferInfo, memoryUsage, flags);
			_buffers.push_back(buffer);
			_allocations.emplace_back(allocation);
			_accessFlags.emplace_back(::vk::AccessFlagBits::eNone);
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

	void Buffer::Flush(glm::i64 size, glm::u64 offset) const {
		const auto _allocation = getAllocation();
		Context::Allocator().FlushAllocation(_allocation, offset, size);
	}

	void Buffer::Invalidate(glm::i64 size, glm::u64 offset) const {
		const auto _allocation = getAllocation();
		Context::Allocator().InvalidateAllocation(_allocation, offset, size);
	}

	::vk::Buffer Buffer::operator*() const
	{
		return _buffers[_isPerFrame ? gfx::Context::Scheduler().getCurrentImageIndex() : 0];
	}

	void Buffer::CopyFrom(const gfx::Buffer& srcBuffer, glm::u64 srcOffset, glm::u64 dstOffset, glm::u64 size) const
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

	::vk::Buffer Buffer::operator[](const size_t i) const {
		if (_isPerFrame) {
			const auto currentFrame = gfx::Context::Scheduler().getCurrentImageIndex();
			return _buffers[currentFrame];
		}
		return _buffers[0];
	}

	VmaAllocation Buffer::getAllocation() const {
		return  _allocations[_isPerFrame ? gfx::Context::Scheduler().getCurrentImageIndex() : 0];
	}


}
