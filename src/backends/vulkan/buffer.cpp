//
// Created by radue on 2/28/2026.
//

#include "buffer.h"

#include <unordered_set>

#include "commandBuffer.h"
#include "device.h"
#include <scheduler.h>

#include "context.h"
#include "vk_enum_conversions.h"
#include "vulkanContext.h"

namespace kor::vk
{
	// static ::vk::DeviceSize GetAlignment(const ::vk::DeviceSize size, const ::vk::DeviceSize alignment) {
	// 	if (alignment > 0) {
	// 		return (size + alignment - 1) & ~(alignment - 1);
	// 	}
	// 	return size;
	// }

	Buffer::Buffer(const RawBuilder& builder) : kor::Buffer(builder) {
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
			// PREFER_HOST + RANDOM is what keeps this in cached system memory, which is the whole
			// point of the type: the CPU may read it back as cheaply as it writes it. See
			// eDeviceDynamic below for the other half of that trade.
			memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
			flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
			break;
		case Type::eDeviceDynamic:
			// AUTO (not PREFER_HOST) with SEQUENTIAL_WRITE is precisely the combination VMA reads
			// as "direct GPU access, CPU sequential write", which makes it prefer a
			// DEVICE_LOCAL | HOST_VISIBLE memory type — the resizable BAR heap, where the GPU runs
			// at full device speed and the CPU still writes straight in.
			//
			// Both halves matter. SEQUENTIAL_WRITE alone with PREFER_HOST is explicitly steered
			// away from device memory, and AUTO alone with RANDOM takes VMA's "always CPU memory"
			// branch, because RANDOM asks for HOST_CACHED and BAR memory is uncached. Changing one
			// without the other silently does nothing.
			//
			// Where no such memory type exists — no resizable BAR, or an integrated GPU where the
			// distinction is meaningless — VMA falls back to host memory on its own, so this stays
			// safe to ask for everywhere. The cost is that the memory is write-combined: see the
			// warning on Read().
			memoryUsage = VMA_MEMORY_USAGE_AUTO;
			flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
			break;
		}

		const auto bufferInfo = ::vk::BufferCreateInfo()
			.setSize(_size)
			.setUsage(getVkBufferUsageFlags(_usage))
			.setSharingMode(::vk::SharingMode::eExclusive);


		const auto frameCount = _isPerFrame ? kor::Context::Scheduler().imageCount() : 1;
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
		return _buffers[_isPerFrame ? kor::Context::Scheduler().currentImageIndex() : 0];
	}

	glm::u64 Buffer::deviceAddress() const
	{
		return static_cast<glm::u64>(
			Context::Device()->getBufferAddress(::vk::BufferDeviceAddressInfo().setBuffer(**this)));
	}

	::vk::AccessFlags Buffer::getAccessMask() const {
		const auto currentFrame = _isPerFrame ? kor::Context::Scheduler().currentImageIndex() : 0;
		return _accessFlags[currentFrame];
	}

	void Buffer::setAccessMask(const ::vk::AccessFlags access) const {
		const auto currentFrame = _isPerFrame ? kor::Context::Scheduler().currentImageIndex() : 0;
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
		const auto currentFrame = kor::Context::Scheduler().currentImageIndex();

		std::map<::vk::Buffer, std::vector<::vk::BufferCopy>> copyRegionsPerBuffer;

		// What this frame has to receive, kept as well as the per-buffer copy regions: the host path
		// below needs the source frame of each, which a vk::BufferCopy does not carry.
		struct FrameWrite { glm::u32 srcFrameIndex; glm::u64 offset; glm::u64 byteSize; };
		std::vector<FrameWrite> _pendingWritesForThisFrame;

		std::unordered_set<PendingWrite, PendingWrite::Hash> remaining;
		for (auto write : _pendingWrites) {
			if (write.buffersLeftToUpdate.contains(currentFrame)) {
				_pendingWritesForThisFrame.push_back({ write.srcFrameIndex, write.offset, write.byteSize });
				copyRegionsPerBuffer[_buffers[write.srcFrameIndex]].push_back(
					::vk::BufferCopy()
						.setSrcOffset(write.offset)
						.setDstOffset(write.offset)
						.setSize(write.byteSize)
				);
				write.buffersLeftToUpdate.erase(currentFrame);
			}
			if (!write.buffersLeftToUpdate.empty()) {
				remaining.insert(std::move(write));
			}
		}
		_pendingWrites = std::move(remaining);

		if (copyRegionsPerBuffer.empty()) {
			return;
		}

		// Host-visible memory is propagated on the *host*, and that is worth a paragraph.
		//
		// The copy is only ever made into the copy belonging to the frame now being recorded, and that
		// frame's fence was waited on before recording began — so nothing is reading it and the CPU may
		// simply write it. Doing it through the GPU instead meant a submit *and*
		// `queue->waitIdle()` (runSingleTimeCommand waits by default): a full stall of the queue, once
		// per written buffer, on every frame after one was written. A camera writes its uniform block
		// on every frame it moves, so moving the camera stalled the queue every frame — which is both
		// slower than the rest of the frame put together and uneven enough to see.
		if (isHostVisible()) {
			auto& allocator = Context::Allocator();
			auto* destination = static_cast<std::byte*>(allocator.MapMemory(_allocations[currentFrame]));

			for (const auto& write : _pendingWritesForThisFrame) {
				auto* source = static_cast<std::byte*>(allocator.MapMemory(_allocations[write.srcFrameIndex]));
				std::memcpy(destination + write.offset, source + write.offset, write.byteSize);
				allocator.UnmapMemory(_allocations[write.srcFrameIndex]);
			}

			allocator.FlushAllocation(_allocations[currentFrame], 0, VK_WHOLE_SIZE);
			allocator.UnmapMemory(_allocations[currentFrame]);
			return;
		}

		// Device-local memory has no host mapping, so it still costs a copy on the queue.
		auto dstBuffer = _buffers[currentFrame];
		Context::Device().runSingleTimeCommand([dstBuffer, &copyRegionsPerBuffer](const kor::vk::CommandBuffer& commandBuffer) {
			for (const auto&[srcBuffer, copyRegions] : copyRegionsPerBuffer) {
				commandBuffer->copyBuffer(srcBuffer, dstBuffer, static_cast<uint32_t>(copyRegions.size()), copyRegions.data());
			}
		}, ::vk::QueueFlagBits::eTransfer);
	}

	VmaAllocation Buffer::getAllocation() const {
		return  _allocations[_isPerFrame ? kor::Context::Scheduler().currentImageIndex() : 0];
	}
}
