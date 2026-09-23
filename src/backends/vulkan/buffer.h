//
// Created by radue on 2/28/2026.
//

#pragma once
#include <vk_mem_alloc.h>

#include "allocator.h"

#include <buffer.h>

namespace kor::vk
{
	class DescriptorSet;
	class BufferView;

	class Buffer final : public kor::Buffer {
		friend class kor::vk::DescriptorSet;
		// A buffer view names one VkBuffer, and a per-frame buffer is several: the view has to see
		// them all to make one view per copy. @see kor::vk::BufferView
		friend class kor::vk::BufferView;
    public:
        explicit Buffer(const RawBuilder& builder);
		~Buffer() override;

		Buffer(const Buffer &) = delete;
        Buffer &operator=(const Buffer &) = delete;

	protected:
    	void Map() const override;
        void Unmap() const override;
		void Flush(glm::i64 size, glm::u64 offset) const override;
		void Invalidate(glm::i64 size, glm::u64 offset) const override;
		void automaticUpdate() override;

	public:
		::vk::Buffer operator*() const;
        [[nodiscard]] VmaAllocation getAllocation() const;

		/**
		 * @brief GPU device address of this buffer (current frame if per-frame).
		 * Requires the buffer to have been created with Usage::eShaderDeviceAddress.
		 */
		[[nodiscard]] glm::u64 deviceAddress() const override;

		[[nodiscard]] ::vk::AccessFlags getAccessMask() const;
		void setAccessMask(::vk::AccessFlags access) const;

		::vk::Buffer operator[](size_t i) const;

	private:
		std::vector<::vk::Buffer> _buffers {};
		std::vector<VmaAllocation> _allocations {};
		mutable std::vector<::vk::AccessFlags> _accessFlags {};
    };
}
