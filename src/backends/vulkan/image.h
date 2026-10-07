//
// Created by radue on 2/28/2026.
//

#pragma once
#include <vk_mem_alloc.h>
#include <vector>

#include "commandBuffer.h"

#include <image.h>
#include <window.h>


namespace kor::vk
{
	class ImageView;

	class Image final : public kor::Image {
    	friend class kor::vk::ImageView;
    public:
        // Whether the physical device can hold this format in these roles, from
        // vkGetPhysicalDeviceFormatProperties' optimal-tiling features. Static: the question is
        // about the device, and is asked before any image exists. @see kor::Image::IsFormatSupported
        static bool isFormatSupported(kor::Image::Format format, Flags<kor::Image::Usage> usage);

        explicit Image(const kor::Image::Builder& builder);
        ~Image() override;

        Image(const Image&) = delete;
        Image& operator=(const Image&) = delete;

    	void Clear(const kor::vk::CommandBuffer& commandBuffer, const ::vk::ClearValue& clearValue) const;
    	void Clear(const ::vk::ClearValue& clearValue) const;

    	void DoResize(const kor::UVec3& extent) override;

    	/** @brief A swap chain's images, in @p format; @p acquired says which one the frame uses. */
    	explicit Image(const std::vector<::vk::Image>& surfaceImages, kor::UVec2 extent, kor::Window::Format format, SampleCount msaa,
    	               std::function<kor::u32()> acquired);

    	/** @brief The Vulkan format it really is: for a swap chain's, not always PixelFormat()'s. */
    	[[nodiscard]] ::vk::Format getFormat() const { return _vkFormat; }

    	::vk::Image operator*() const;
    	VmaAllocation getAllocation() const;
    	[[nodiscard]] ::vk::ImageLayout getImageLayout(kor::u32 mipLevel = 0, kor::u32 arrayLayer = 0) const;
		[[nodiscard]] ::vk::AccessFlags getAccessMask(kor::u32 mipLevel = 0, kor::u32 arrayLayer = 0) const;
		void SetImageLayout(::vk::ImageLayout newLayout, kor::u32 mipLevel = 0, kor::u32 arrayLayer = 0) const;
		void SetAccessMask(::vk::AccessFlags newAccessMask, kor::u32 mipLevel = 0, kor::u32 arrayLayer = 0) const;

		::vk::ImageAspectFlags getAspectFlags() const;

    private:
    	::vk::Format _vkFormat;
    	std::vector<::vk::Image> _images;
    	std::vector<VmaAllocation> _allocations;
		mutable std::unordered_map<kor::u32, ::vk::ImageLayout> _layouts {};
		mutable std::unordered_map<kor::u32, ::vk::AccessFlags> _accessMasks {};
    };
}
