//
// Created by radue on 2/28/2026.
//

module;

#include <unordered_set>
#include "vk_enum_conversions.h"

module vk.buffer;

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

	::vk::AccessFlags Buffer::getAccessMask() const {
		const auto currentFrame = _isPerFrame ? gfx::Context::Scheduler().getCurrentImageIndex() : 0;
		return _accessFlags[currentFrame];
	}

	void Buffer::setAccessMask(const ::vk::AccessFlags access) const {
		const auto currentFrame = _isPerFrame ? gfx::Context::Scheduler().getCurrentImageIndex() : 0;
		_accessFlags[currentFrame] = access;
	}

	::vk::Buffer Buffer::operator[](const size_t i) const {
		if (i >= _buffers.size()) {
			throw std::out_of_range("Buffer index out of range!");
		}
		return _buffers[i];
	}

	// !TODO make this run on the render command buffer with barriers instead of having a different command buffer that stalls the queue
	void Buffer::automaticUpdate() {
		const auto currentFrame = gfx::Context::Scheduler().getCurrentImageIndex();

		std::map<::vk::Buffer, std::vector<::vk::BufferCopy>> copyRegionsPerBuffer;

		for (auto&[srcFrameIndex, offset, size, framesToWrite] : _pendingWrites) {
			if (framesToWrite.contains(currentFrame)) {
				auto srcBuffer = _buffers[srcFrameIndex];
				copyRegionsPerBuffer[srcBuffer].push_back(
					::vk::BufferCopy()
						.setSrcOffset(offset)
						.setDstOffset(offset)
						.setSize(size)
				);
				PendingWrite write{
					.srcFrameIndex = srcFrameIndex,
					.offset = offset,
					.byteSize = size,
					.buffersLeftToUpdate = framesToWrite
				};
				write.buffersLeftToUpdate.erase(currentFrame);
				_pendingWrites.insert(write);
			}
		}
		if (copyRegionsPerBuffer.empty()) {
			return;
		}

		auto dstBuffer = _buffers[currentFrame];
		Context::Device().runSingleTimeCommand([dstBuffer, &copyRegionsPerBuffer](const gfx::vk::CommandBuffer& commandBuffer) {
			for (const auto&[srcBuffer, copyRegions] : copyRegionsPerBuffer) {
				commandBuffer->copyBuffer(srcBuffer, dstBuffer, static_cast<uint32_t>(copyRegions.size()), copyRegions.data());
			}
		}, ::vk::QueueFlagBits::eTransfer);

		std::erase_if(_pendingWrites, [](const PendingWrite& write) { return write.buffersLeftToUpdate.empty(); });
	}

	VmaAllocation Buffer::getAllocation() const {
		return _allocations[_isPerFrame ? gfx::Context::Scheduler().getCurrentImageIndex() : 0];
	}
}
