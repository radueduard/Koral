//
// Created by radue on 2/28/2026.
//

#pragma once
#include <memory>
#include <vector>

#include <glm/fwd.hpp>

#include "device.h"
#include "imageView.h"

#include <image.h>

#include "vk_enum_conversions.h"
#include "vk_wrapper.h"

namespace kor::vk
{
    class Surface;
    class Image;
    class Frame;

    class SwapChain final : public kor::vk::Wrapper<::vk::SwapchainKHR>
    {
    public:
        struct Builder {
            explicit Builder(const Surface& surface) : surface(surface) {}

            std::reference_wrapper<const Surface> surface;
            glm::u32 minImageCount = 2;
            glm::u32 imageCount = 2;
            MSAA msaa = MSAA::eNone;

            Builder& setMinImageCount(const glm::u32 minImageCount) { this->minImageCount = minImageCount; return *this; }
            Builder& setImageCount(const glm::u32 imageCount) { this->imageCount = imageCount; return *this; }
            Builder& setMSAA(const kor::MSAA msaa) { this->msaa = msaa; return *this; }
            std::unique_ptr<SwapChain> build() { return std::make_unique<SwapChain>(*this); }
        };

        explicit SwapChain(const Builder& createInfo);
        ~SwapChain() override;

        [[nodiscard]] const glm::uvec2 &getExtent() const { return _extent; }
        [[nodiscard]] glm::u32 getMinImageCount() const { return _minImageCount; }
        [[nodiscard]] glm::u32 getImageCount() const { return _imageCount; }
        [[nodiscard]] ::vk::SampleCountFlagBits getMSAA() const { return getVkSampleCount(_msaa); }

        [[nodiscard]] kor::ResourceRef<const kor::Image> getImage() const { return _swapChainImages; }
        [[nodiscard]] kor::ResourceRef<const kor::Image> getDepthImage() const { return _depthImages; }

        [[nodiscard]] std::reference_wrapper<const kor::ImageView> getSwapChainImageViews() const { return *_swapChainImageViews; }
        [[nodiscard]] std::reference_wrapper<const kor::ImageView> getDepthImageViews() const { return *_depthImageViews; }

        [[nodiscard]] ::vk::Format getImageFormat() const { return _surfaceFormat.format; }
        [[nodiscard]] glm::u32 getCurrentImageIndex() const { return _imageIndex; }

    	[[nodiscard]] ::vk::Semaphore getCurrentRenderFinishedSemaphore() const { return _renderFinishedSemaphores[_imageIndex]; }

        void Resize(const glm::uvec2& newSize);
        ::vk::Result Acquire(const kor::vk::Frame &frame);
        ::vk::Result Present(const kor::vk::Frame &frame);

    private:
        glm::uvec2 _extent;
        MSAA _msaa = MSAA::e2x;
        glm::u32 _minImageCount = 0;
        glm::u32 _imageCount = 0;
        glm::u32 _imageIndex = 0;

        std::reference_wrapper<const Surface> _surface;
        ::vk::SurfaceFormatKHR _surfaceFormat = {};
        ::vk::PresentModeKHR _presentMode = {};

        const kor::vk::Queue& _presentQueue;
        kor::Resource<kor::Image> _swapChainImages;
        kor::Resource<kor::Image> _depthImages;
        std::vector<::vk::Semaphore> _renderFinishedSemaphores;

        kor::Resource<kor::ImageView> _swapChainImageViews;
        kor::Resource<kor::ImageView> _depthImageViews;

        void CreateSwapChain();

        static ::vk::SurfaceFormatKHR ChooseSurfaceFormat(const std::vector<::vk::SurfaceFormatKHR> &availableFormats);
        static ::vk::PresentModeKHR ChoosePresentMode(const std::vector<::vk::PresentModeKHR> &availablePresentModes);
        static glm::uvec2 ChooseExtent(const ::vk::SurfaceCapabilitiesKHR &capabilities, const glm::uvec2& extent);
    };
}
