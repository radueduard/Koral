//
// Created by radue on 2/28/2026.
//

module;

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>

export module gfx:vk_image;
import :vk_types;

import :vk_commandBuffer;

import std;
import :image;

namespace gfx::vk
{
	export class Image final : public gfx::Image {
    	friend class gfx::vk::ImageView;
    public:

        explicit Image(const gfx::Image::Builder& builder);
        ~Image() override;

        Image(const Image&) = delete;
        Image& operator=(const Image&) = delete;

    	void Clear(const gfx::vk::CommandBuffer& commandBuffer, const ::vk::ClearValue& clearValue) const;
    	void Clear(const ::vk::ClearValue& clearValue) const;

    	void Resize(const glm::uvec3& extent) override;

    	explicit Image(const std::vector<::vk::Image>& surfaceImages, glm::uvec2 extent, Format format, SampleCount sampleCount);

    	::vk::Image operator*() const;
    	VmaAllocation getAllocation() const;
    	[[nodiscard]] ::vk::ImageLayout getImageLayout(glm::u32 mipLevel = 0, glm::u32 arrayLayer = 0) const;
		[[nodiscard]] ::vk::AccessFlags getAccessMask(glm::u32 mipLevel = 0, glm::u32 arrayLayer = 0) const;
		void SetImageLayout(::vk::ImageLayout newLayout, glm::u32 mipLevel = 0, glm::u32 arrayLayer = 0) const;
		void SetAccessMask(::vk::AccessFlags newAccessMask, glm::u32 mipLevel = 0, glm::u32 arrayLayer = 0) const;

		::vk::ImageAspectFlags getAspectFlags() const;

    private:
    	std::vector<::vk::Image> _images;
    	std::vector<VmaAllocation> _allocations;
		mutable std::unordered_map<glm::u32, ::vk::ImageLayout> _layouts {};
		mutable std::unordered_map<glm::u32, ::vk::AccessFlags> _accessMasks {};
    };
}
