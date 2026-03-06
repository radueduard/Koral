//
// Created by radue on 2/28/2026.
//

#include "swapChain.h"

#include <ranges>

#include "image.h"
#include "runtime.h"
#include "scheduler.h"
#include "vulkanContext.h"
#include "utils/vk_enum_conversions.h"

namespace gfx::vk
{
    ::vk::SurfaceFormatKHR SwapChain::ChooseSurfaceFormat(const std::vector<::vk::SurfaceFormatKHR> &availableFormats) {
        for (const auto &availableFormat : availableFormats) {
            // if (availableFormat.format == ::vk::Format::eA2B10G10R10UnormPack32 && availableFormat.colorSpace == ::vk::ColorSpaceKHR::eSrgbNonlinear) {
            if (availableFormat.format == ::vk::Format::eB8G8R8A8Unorm && availableFormat.colorSpace == ::vk::ColorSpaceKHR::eSrgbNonlinear) {
            // if (availableFormat.format == ::vk::Format::eR8G8B8A8Srgb && availableFormat.colorSpace == ::vk::ColorSpaceKHR::eSrgbNonlinear) {
                return availableFormat;
            }
        }
        std::cerr << "Failed to find suitable surface format" << std::endl;
        return availableFormats[0];
    }

    ::vk::PresentModeKHR SwapChain::ChoosePresentMode(const std::vector<::vk::PresentModeKHR> &availablePresentModes) {
        for (const auto &availablePresentMode : availablePresentModes) {
            if (availablePresentMode == ::vk::PresentModeKHR::eMailbox) {
                return availablePresentMode;
            }
        }
        std::cerr << "Failed to find suitable present mode" << std::endl;
        return ::vk::PresentModeKHR::eFifo;
    }

    glm::uvec2 SwapChain::ChooseExtent(const ::vk::SurfaceCapabilitiesKHR &capabilities, const glm::uvec2& extent) {
        if (capabilities.currentExtent.width != UINT32_MAX) {
            return glm::uvec2(capabilities.currentExtent.width, capabilities.currentExtent.height);
        }
        glm::uvec2 actualExtent = extent;
        actualExtent.x = std::max<glm::u32>(capabilities.minImageExtent.width, std::min<glm::u32>(capabilities.maxImageExtent.width, actualExtent.x));
        actualExtent.y = std::max<glm::u32>(capabilities.minImageExtent.height, std::min<glm::u32>(capabilities.maxImageExtent.height, actualExtent.y));
        return actualExtent;
    }

    SwapChain::SwapChain(const Builder& createInfo) :
        _presentQueue(Context::Device().requestPresentQueue()),
        _msaa(createInfo.msaa),
        _minImageCount(createInfo.minImageCount),
        _imageCount(createInfo.imageCount)
    {
        _extent = glm::uvec2(1, 1);

        vk::Context::Device()->waitIdle();

        CreateSwapChain();
    }

    void SwapChain::CreateSwapChain() {
        const auto& physicalDevice = Context::Runtime().getPhysicalDevice();
        auto surfaceCapabilities = physicalDevice.getSurfaceCapabilities();

        _surfaceFormat = ChooseSurfaceFormat(physicalDevice.getSurfaceFormats());
        _presentMode = ChoosePresentMode(physicalDevice.getSurfacePresentModes());
        _extent = ChooseExtent(surfaceCapabilities, _extent);

        const ::vk::SwapchainKHR oldSwapChain = _handle;
        const auto queueFamilyIndices = std::array { _presentQueue.getFamily().getIndex() };

        const auto createInfo = ::vk::SwapchainCreateInfoKHR()
            .setSurface(Context::Runtime().getSurface())
            .setMinImageCount(_imageCount)
            .setImageFormat(_surfaceFormat.format)
            .setImageColorSpace(_surfaceFormat.colorSpace)
            .setImageExtent({ _extent.x, _extent.y })
            .setImageArrayLayers(1)
            .setImageUsage(::vk::ImageUsageFlagBits::eColorAttachment | ::vk::ImageUsageFlagBits::eTransferDst)
            .setImageSharingMode(::vk::SharingMode::eExclusive)
            .setQueueFamilyIndices(queueFamilyIndices)
            .setPreTransform(surfaceCapabilities.currentTransform)
            .setCompositeAlpha(::vk::CompositeAlphaFlagBitsKHR::eOpaque)
            .setPresentMode(_presentMode)
            .setOldSwapchain(oldSwapChain)
            .setClipped(true);
        _handle = Context::Device()->createSwapchainKHR(createInfo);

        if (oldSwapChain) {
            Context::Device()->destroySwapchainKHR(oldSwapChain);
        }

        const auto swapChainImageHandles = Context::Device()->getSwapchainImagesKHR(_handle);
        _swapChainImages.clear();
        _swapChainImages.reserve(swapChainImageHandles.size());
        for (const auto &image : swapChainImageHandles) {
            const auto& img = _swapChainImages.emplace_back(std::make_unique<gfx::vk::Image>(image, _extent, getFormat(_surfaceFormat.format), _msaa));
            _swapChainImageViews.emplace_back(ImageView::Builder(*img)
                .setViewType(ImageView::Type::e2D)
                .setBaseMipLevel(0)
                .setBaseArrayLayer(0)
                .setMipLevelCount(1)
                .setArrayLayerCount(1)
                .build());
        }
    }

    SwapChain::~SwapChain() {
        Context::Device()->waitIdle();
        if (_handle) {
            Context::Device()->destroySwapchainKHR(_handle);
        }
    }

    std::vector<std::reference_wrapper<gfx::ImageView>> SwapChain::getSwapChainImageViews() const
    {
        std::vector<std::reference_wrapper<gfx::ImageView>> imageViews;
        for (const auto& imageView : _swapChainImageViews) {
            imageViews.emplace_back(*imageView);
        }
        return imageViews;
    }

    void SwapChain::Resize(const glm::uvec2& newSize) {
        _extent = newSize;
        Context::Device()->waitIdle();
        CreateSwapChain();
    }

    ::vk::Result SwapChain::Acquire(const gfx::vk::Frame &frame) {
        try {
            const auto result = Context::Device()->acquireNextImageKHR(
                _handle,
                UINT64_MAX,
                frame.getImageAvailableSemaphore(),
                nullptr);
            _imageIndex = result.value;
            return result.result;
        } catch (const ::vk::OutOfDateKHRError &) {
            return ::vk::Result::eErrorOutOfDateKHR;
        }
    }

    ::vk::Result SwapChain::Present(const gfx::vk::Frame &frame) {
        std::array waitSemaphores = {
        	frame.getReadyToPresentSemaphore()
        };

        std::array swapChains = {
            _handle
        };

        const auto presentInfo = ::vk::PresentInfoKHR()
            .setWaitSemaphores(waitSemaphores)
            .setSwapchains(swapChains)
            .setImageIndices(_imageIndex);

        try {
        	return frame.getQueue()->presentKHR(presentInfo);
        } catch (const ::vk::OutOfDateKHRError &) {
            return ::vk::Result::eErrorOutOfDateKHR;
        }
    }
}
