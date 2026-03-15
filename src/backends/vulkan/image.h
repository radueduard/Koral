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

		void TransitionLayout(const gfx::vk::CommandBuffer& commandBuffer, ::vk::ImageLayout newLayout) const;
		void TransitionLayout(::vk::ImageLayout newLayout) const;

    	void Barrier(
    		const gfx::vk::CommandBuffer& commandBuffer,
    		::vk::AccessFlags srcAccessMask,
    		::vk::AccessFlags dstAccessMask,
    		::vk::PipelineStageFlags srcStage,
    		::vk::PipelineStageFlags dstStage,
    		::vk::ImageLayout newLayout,
    		glm::i32 frame = -1
    	) const;

    	void Clear(const gfx::vk::CommandBuffer& commandBuffer, const ::vk::ClearValue& clearValue) const;
    	void Clear(const ::vk::ClearValue& clearValue) const;
        void GenerateMipmaps() const;
    	void Resize(const glm::uvec3& extent);

    	std::vector<std::byte> ReadData(glm::u32 mipLevel, glm::u32 arrayLayer) const override;
        void CopyFrom(const gfx::Buffer& buffer, glm::u32 mipLevel, glm::u32 layer) const override;

    	explicit Image(const std::vector<::vk::Image>& surfaceImages, glm::uvec2 extent, Format format, MSAA msaa);

    	::vk::Image operator*() const;
    	VmaAllocation getAllocation() const;
    	[[nodiscard]] ::vk::ImageLayout getImageLayout() const;

    private:
    	std::vector<::vk::Image> _images;
    	std::vector<VmaAllocation> _allocations;
    	mutable std::vector<::vk::ImageLayout> _layouts {};

    	inline static std::unordered_map<::vk::ImageLayout, ::vk::AccessFlags> layoutAccessMap = {
			{ ::vk::ImageLayout::eUndefined, ::vk::AccessFlagBits::eNone },
			{ ::vk::ImageLayout::eGeneral, ::vk::AccessFlagBits::eMemoryRead | ::vk::AccessFlagBits::eMemoryWrite },
			{ ::vk::ImageLayout::eColorAttachmentOptimal, ::vk::AccessFlagBits::eColorAttachmentRead | ::vk::AccessFlagBits::eColorAttachmentWrite },
			{ ::vk::ImageLayout::eDepthStencilAttachmentOptimal, ::vk::AccessFlagBits::eDepthStencilAttachmentRead | ::vk::AccessFlagBits::eDepthStencilAttachmentWrite },
			{ ::vk::ImageLayout::eShaderReadOnlyOptimal, ::vk::AccessFlagBits::eShaderRead },
			{ ::vk::ImageLayout::eTransferSrcOptimal, ::vk::AccessFlagBits::eTransferRead },
			{ ::vk::ImageLayout::eTransferDstOptimal, ::vk::AccessFlagBits::eTransferWrite },
			{ ::vk::ImageLayout::ePresentSrcKHR, ::vk::AccessFlagBits::eNone },
		};

    	inline static std::unordered_map<::vk::ImageLayout, ::vk::PipelineStageFlags> srcLayoutPipelineStageMap = {
			{ ::vk::ImageLayout::eUndefined, ::vk::PipelineStageFlagBits::eTopOfPipe },
			{ ::vk::ImageLayout::eGeneral, ::vk::PipelineStageFlagBits::eAllCommands },
			{ ::vk::ImageLayout::eColorAttachmentOptimal, ::vk::PipelineStageFlagBits::eColorAttachmentOutput },
			{ ::vk::ImageLayout::eDepthStencilAttachmentOptimal, ::vk::PipelineStageFlagBits::eLateFragmentTests },
			{ ::vk::ImageLayout::eShaderReadOnlyOptimal, ::vk::PipelineStageFlagBits::eFragmentShader },
			{ ::vk::ImageLayout::eTransferSrcOptimal, ::vk::PipelineStageFlagBits::eTransfer },
			{ ::vk::ImageLayout::eTransferDstOptimal, ::vk::PipelineStageFlagBits::eTransfer },
			{ ::vk::ImageLayout::ePresentSrcKHR, ::vk::PipelineStageFlagBits::eBottomOfPipe },
		};

		inline static std::unordered_map<::vk::ImageLayout, ::vk::PipelineStageFlags> dstLayoutPipelineStageMap = {
			{ ::vk::ImageLayout::eUndefined, ::vk::PipelineStageFlagBits::eTopOfPipe },
			{ ::vk::ImageLayout::eGeneral, ::vk::PipelineStageFlagBits::eAllCommands },
			{ ::vk::ImageLayout::eColorAttachmentOptimal, ::vk::PipelineStageFlagBits::eColorAttachmentOutput },
			{ ::vk::ImageLayout::eDepthStencilAttachmentOptimal, ::vk::PipelineStageFlagBits::eEarlyFragmentTests },
			{ ::vk::ImageLayout::eShaderReadOnlyOptimal, ::vk::PipelineStageFlagBits::eFragmentShader },
			{ ::vk::ImageLayout::eTransferSrcOptimal, ::vk::PipelineStageFlagBits::eTransfer },
			{ ::vk::ImageLayout::eTransferDstOptimal, ::vk::PipelineStageFlagBits::eTransfer },
			{ ::vk::ImageLayout::ePresentSrcKHR, ::vk::PipelineStageFlagBits::eBottomOfPipe },
		};

    	inline static std::unordered_map<::vk::ImageUsageFlagBits, ::vk::ImageAspectFlagBits> usageAspectMap = {
			{ ::vk::ImageUsageFlagBits::eColorAttachment, ::vk::ImageAspectFlagBits::eColor },
			{ ::vk::ImageUsageFlagBits::eDepthStencilAttachment, ::vk::ImageAspectFlagBits::eDepth },
			{ ::vk::ImageUsageFlagBits::eSampled, ::vk::ImageAspectFlagBits::eColor },
			{ ::vk::ImageUsageFlagBits::eStorage, ::vk::ImageAspectFlagBits::eColor },
			{ ::vk::ImageUsageFlagBits::eInputAttachment, ::vk::ImageAspectFlagBits::eColor },
		};
    };
}
