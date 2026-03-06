//
// Created by radue on 2/28/2026.
//

#pragma once
#include <memory>
#include <vector>

#include <glm/fwd.hpp>

#include "device.h"
#include "imageView.h"

#include "core/image.h"

#include "utils/vk_wrapper.h"

namespace gfx::vk
{
    class Image;
    class Frame;

    class SwapChain final : public gfx::vk::Wrapper<::vk::SwapchainKHR>
    {
    public:
        struct Builder {
            glm::u32 minImageCount;
            glm::u32 imageCount;
            MSAA msaa;

            Builder& setMinImageCount(const glm::u32 minImageCount) { this->minImageCount = minImageCount; return *this; }
            Builder& setImageCount(const glm::u32 imageCount) { this->imageCount = imageCount; return *this; }
            Builder& setMSAA(const gfx::MSAA msaa) { this->msaa = msaa; return *this; }
            std::unique_ptr<SwapChain> build() { return std::make_unique<SwapChain>(*this); }
        };

        explicit SwapChain(const Builder& createInfo);
        ~SwapChain() override;

        [[nodiscard]] const glm::uvec2 &getExtent() const { return _extent; }
        [[nodiscard]] glm::u32 getMinImageCount() const { return _minImageCount; }
        [[nodiscard]] glm::u32 getImageCount() const { return _imageCount; }
        [[nodiscard]] MSAA getMSAA() const { return _msaa; }
        [[nodiscard]] std::vector<std::reference_wrapper<gfx::ImageView>> getSwapChainImageViews() const;
        [[nodiscard]] ::vk::Format getImageFormat() const { return _surfaceFormat.format; }
        [[nodiscard]] glm::u32 getCurrentImageIndex() const { return _imageIndex; }

        void Resize(const glm::uvec2& newSize);
        ::vk::Result Acquire(const gfx::vk::Frame &frame);
        ::vk::Result Present(const gfx::vk::Frame &frame);

    private:
        glm::uvec2 _extent;
        MSAA _msaa = MSAA::e2x;
        glm::u32 _minImageCount = 0;
        glm::u32 _imageCount = 0;
        glm::u32 _imageIndex = 0;
        ::vk::SurfaceFormatKHR _surfaceFormat = {};
        ::vk::PresentModeKHR _presentMode = {};

        const gfx::vk::Queue& _presentQueue;
        std::vector<std::unique_ptr<gfx::vk::Image>> _swapChainImages;
        std::vector<std::unique_ptr<gfx::ImageView>> _swapChainImageViews;

        void CreateSwapChain();

        static ::vk::SurfaceFormatKHR ChooseSurfaceFormat(const std::vector<::vk::SurfaceFormatKHR> &availableFormats);
        static ::vk::PresentModeKHR ChoosePresentMode(const std::vector<::vk::PresentModeKHR> &availablePresentModes);
        static glm::uvec2 ChooseExtent(const ::vk::SurfaceCapabilitiesKHR &capabilities, const glm::uvec2& extent);
    };
}
