//
// Created by radue on 2/28/2026.
//

#pragma once
#include <vma/vk_mem_alloc.h>

#include "allocator.h"
#include "vulkanContext.h"

#include "core/buffer.h"

#include "utils/vk_wrapper.h"

namespace gfx::vk
{
	class Buffer final : public gfx::Buffer, public Wrapper<::vk::Buffer> {
    public:
        explicit Buffer(const Builder& builder);
		~Buffer() override;

		Buffer(const Buffer &) = delete;
        Buffer &operator=(const Buffer &) = delete;


    	void Map() const override;
        void Unmap() const override;


        [[nodiscard]] VmaAllocation getAllocation() const;
        void CopyFrom(const gfx::Buffer& srcBuffer, glm::i64 srcOffset, glm::i64 dstOffset, glm::i64 size) const override;

    private:
    	VmaAllocation _allocation = VK_NULL_HANDLE;
    };
}
