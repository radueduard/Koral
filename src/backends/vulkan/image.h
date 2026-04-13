//
// Created by radue on 2/28/2026.
//

#pragma once
#include <vk_mem_alloc.h>
#include <vector>

#include "commandBuffer.h"

#include <image.h>


namespace gfx::vk
{
	class ImageView;

	class Image final : public gfx::Image {
    	friend class gfx::vk::ImageView;
    public:

        explicit Image(const gfx::Image::Builder& builder);
        ~Image() override;

        Image(const Image&) = delete;
        Image& operator=(const Image&) = delete;

    	void Clear(const gfx::vk::CommandBuffer& commandBuffer, const ::vk::ClearValue& clearValue) const;
    	void Clear(const ::vk::ClearValue& clearValue) const;
    	void Resize(const glm::uvec3& extent);

        void GenerateMipmaps() override;
    	std::vector<std::byte> ReadData(glm::u32 mipLevel, glm::u32 arrayLayer) const override;
        void CopyFrom(const gfx::Buffer& buffer, glm::u32 mipLevel, glm::u32 layer) const override;

    	explicit Image(const std::vector<::vk::Image>& surfaceImages, glm::uvec2 extent, Format format, MSAA msaa);

    	::vk::Image operator*() const;
    	VmaAllocation getAllocation() const;
    	[[nodiscard]] ::vk::ImageLayout getImageLayout() const;
		void SetImageLayout(::vk::ImageLayout newLayout, glm::u32 mipLevel, glm::u32 arrayLayer) const;

		::vk::ImageAspectFlags getAspectFlags() const;

    private:
    	std::vector<::vk::Image> _images;
    	std::vector<VmaAllocation> _allocations;
		mutable std::unordered_map<glm::u32, ::vk::ImageLayout> _layouts {};
    };
}
