//
// Created by radue on 2/28/2026.
//

#pragma once
#include <vk_mem_alloc.h>

#include "allocator.h"

#include <buffer.h>

namespace gfx::vk
{
	class DescriptorSet;

	class Buffer final : public gfx::Buffer {
		friend class gfx::vk::DescriptorSet;
    public:
        explicit Buffer(const Builder& builder);
		~Buffer() override;

		Buffer(const Buffer &) = delete;
        Buffer &operator=(const Buffer &) = delete;


    	void Map() const override;
        void Unmap() const override;

		::vk::Buffer operator*() const;
        [[nodiscard]] VmaAllocation getAllocation() const;

        void CopyFrom(const gfx::Buffer& srcBuffer, glm::i64 srcOffset, glm::i64 dstOffset, glm::i64 size) const override;

		[[nodiscard]] ::vk::AccessFlags getAccessMask() const;
		void setAccessMask(::vk::AccessFlags access) const;

    private:
		::vk::Buffer operator[](const int i) const { return _buffers[i]; }

		std::vector<::vk::Buffer> _buffers {};
		std::vector<VmaAllocation> _allocations {};
		mutable std::vector<::vk::AccessFlags> _accessFlags {};
    };
}
