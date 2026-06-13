//
// Created by radue on 2/28/2026.
//

module;

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>

export module gfx:vk_buffer;
import :vk_types;

import :buffer;

namespace gfx::vk
{
	class DescriptorSet;

	export class Buffer final : public gfx::Buffer {
		friend class gfx::vk::DescriptorSet;
    public:
        explicit Buffer(const RawBuilder& builder);
		~Buffer() override;

		Buffer(const Buffer &) = delete;
        Buffer &operator=(const Buffer &) = delete;

    	void Map() const override;
        void Unmap() const override;
		void Flush(glm::i64 size, glm::u64 offset) const override;
		void Invalidate(glm::i64 size, glm::u64 offset) const override;

		::vk::Buffer operator*() const;
        [[nodiscard]] VmaAllocation getAllocation() const;

		[[nodiscard]] ::vk::AccessFlags getAccessMask() const;
		void setAccessMask(::vk::AccessFlags access) const;

		::vk::Buffer operator[](size_t i) const;
    private:
		void automaticUpdate() override;

		std::vector<::vk::Buffer> _buffers {};
		std::vector<VmaAllocation> _allocations {};
		mutable std::vector<::vk::AccessFlags> _accessFlags {};
    };
}
