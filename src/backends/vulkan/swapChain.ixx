//
// Created by radue on 2/28/2026.
//

module;

#include "vk_wrapper.h"
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>

export module gfx:vk_swapChain;
import :vk_types;

import :window;
import :image;
import :imageView;
import resource;
import :vk_enum_conversions;

namespace gfx::vk
{
    export class SwapChain final : public Wrapper<::vk::SwapchainKHR>
    {
    public:
        struct Builder {
            explicit Builder(const Surface& surface) : surface(surface) {}

            std::reference_wrapper<const Surface> surface;
            glm::u32 minImageCount = 2;
            glm::u32 imageCount = 2;
            SampleCount sampleCount = SampleCount::e1;

            Builder& setMinImageCount(const glm::u32 minImageCount) { this->minImageCount = minImageCount; return *this; }
            Builder& setImageCount(const glm::u32 imageCount) { this->imageCount = imageCount; return *this; }
            Builder& setSampleCount(const gfx::SampleCount sampleCount) { this->sampleCount = sampleCount; return *this; }
            std::unique_ptr<SwapChain> build() { return std::make_unique<SwapChain>(*this); }
        };

        explicit SwapChain(const Builder& createInfo);
        ~SwapChain() override;

        [[nodiscard]] const glm::uvec2 &getExtent() const { return _extent; }
        [[nodiscard]] glm::u32 getMinImageCount() const { return _minImageCount; }
        [[nodiscard]] glm::u32 getImageCount() const { return _imageCount; }
        [[nodiscard]] ::vk::SampleCountFlagBits getMSAA() const { return getVkSampleCount(_sampleCount); }

        [[nodiscard]] ResourceRef<const gfx::Image> getImage() const { return _swapChainImages; }
        [[nodiscard]] ResourceRef<const gfx::Image> getDepthImage() const { return _depthImages; }

        [[nodiscard]] ResourceRef<const gfx::ImageView> getSwapChainImageViews() const { return _swapChainImageViews; }
        [[nodiscard]] ResourceRef<const gfx::ImageView> getDepthImageViews() const { return _depthImageViews; }

        [[nodiscard]] ::vk::Format getImageFormat() const { return _surfaceFormat.format; }
        [[nodiscard]] glm::u32 getCurrentImageIndex() const { return _imageIndex; }

    	[[nodiscard]] ::vk::Semaphore getCurrentRenderFinishedSemaphore() const { return _renderFinishedSemaphores[_imageIndex]; }

        void Resize(const glm::uvec2& newSize);
        ::vk::Result Acquire(const gfx::vk::Frame &frame);
        ::vk::Result Present(const gfx::vk::Frame &frame);

    private:
        glm::uvec2 _extent;
        SampleCount _sampleCount;
        glm::u32 _minImageCount = 0;
        glm::u32 _imageCount = 0;
        glm::u32 _imageIndex = 0;

        std::reference_wrapper<const Surface> _surface;
        ::vk::SurfaceFormatKHR _surfaceFormat = {};
        ::vk::PresentModeKHR _presentMode = {};

        const gfx::vk::Queue& _presentQueue;
        Resource<gfx::Image> _swapChainImages;
        Resource<gfx::Image> _depthImages;
        std::vector<::vk::Semaphore> _renderFinishedSemaphores;

        Resource<gfx::ImageView> _swapChainImageViews;
        Resource<gfx::ImageView> _depthImageViews;

        void CreateSwapChain();

        static ::vk::SurfaceFormatKHR ChooseSurfaceFormat(const std::vector<::vk::SurfaceFormatKHR> &availableFormats);
        static ::vk::PresentModeKHR ChoosePresentMode(const std::vector<::vk::PresentModeKHR> &availablePresentModes);
        static glm::uvec2 ChooseExtent(const ::vk::SurfaceCapabilitiesKHR &capabilities, const glm::uvec2& extent);
    };
}
