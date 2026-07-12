//
// Created by radue on 2/28/2026.
//

#include "swapChain.h"

#include <framebuffer.h>
#include <ranges>
#include <iostream>

#include "context.h"
#include "image.h"
#include "runtime.h"
#include "scheduler.h"
#include "surface.h"
#include "vulkanContext.h"
#include "vk_enum_conversions.h"
#include "window.h"

namespace kor::vk
{
    ::vk::SurfaceFormatKHR SwapChain::ChooseSurfaceFormat(const std::vector<::vk::SurfaceFormatKHR> &availableFormats) {
        for (const auto &availableFormat : availableFormats) {
            // std::cout << "Available surface format: " << ::vk::to_string(availableFormat.format) << ", color space: " << ::vk::to_string(availableFormat.colorSpace) << std::endl;
            // if (availableFormat.format == ::vk::Format::eA2B10G10R10UnormPack32 && availableFormat.colorSpace == ::vk::ColorSpaceKHR::eSrgbNonlinear) {
            if (availableFormat.format == ::vk::Format::eB8G8R8A8Unorm && availableFormat.colorSpace == ::vk::ColorSpaceKHR::eSrgbNonlinear) {
            // if (availableFormat.format == ::vk::Format::eR8G8B8A8Unorm && availableFormat.colorSpace == ::vk::ColorSpaceKHR::eSrgbNonlinear) {
                return availableFormat;
            }
        }
        std::cerr << "Failed to find suitable surface format" << std::endl;
        return availableFormats[0];
    }

    ::vk::PresentModeKHR SwapChain::ChoosePresentMode(const std::vector<::vk::PresentModeKHR> &availablePresentModes) {
        if (!kor::Context::Window().isVSync()) {
            // VSync off: present uncapped. Prefer immediate (may tear); Fifo is the
            // guaranteed-available fallback if the driver lacks an immediate mode.
            for (const auto &availablePresentMode : availablePresentModes) {
                if (availablePresentMode == ::vk::PresentModeKHR::eImmediate) {
                    return availablePresentMode;
                }
            }
            return ::vk::PresentModeKHR::eFifo;
        }

        // VSync on (no tearing): prefer mailbox for lower latency, else Fifo (always available).
        for (const auto &availablePresentMode : availablePresentModes) {
            if (availablePresentMode == ::vk::PresentModeKHR::eMailbox) {
                return availablePresentMode;
            }
        }
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
        _extent(kor::Context::Window().getExtent()),
        _msaa(createInfo.msaa),
        _minImageCount(createInfo.minImageCount),
        _imageCount(createInfo.imageCount),
        _surface(createInfo.surface),
        _presentQueue(Context::Device().requestPresentQueue(_surface))
    {
        for (glm::u32 i = 0; i < _imageCount; i++)
        {
            _renderFinishedSemaphores.push_back(Context::Device()->createSemaphore({}));
        }

        vk::Context::Device()->waitIdle();

        CreateSwapChain();
    }

    void SwapChain::CreateSwapChain() {
        const auto surfaceCapabilities = _surface.get().getCapabilities();

        _surfaceFormat = ChooseSurfaceFormat(_surface.get().getFormats());
        _presentMode = ChoosePresentMode(_surface.get().getPresentModes());
        _extent = ChooseExtent(surfaceCapabilities, _extent);

        const ::vk::SwapchainKHR oldSwapChain = _handle;
        const auto queueFamilyIndices = std::array { _presentQueue.getFamily().getIndex() };

        const auto compositeAlpha = kor::Context::Window().isFramebufferTransparent() ?
#ifdef _WIN32
        ::vk::CompositeAlphaFlagBitsKHR::ePreMultiplied
#elifdef __APPLE__
        ::vk::CompositeAlphaFlagBitsKHR::ePostMultiplied
#else
        ::vk::CompositeAlphaFlagBitsKHR::eOpaque
#endif
        : ::vk::CompositeAlphaFlagBitsKHR::eOpaque;

        const auto createInfo = ::vk::SwapchainCreateInfoKHR()
            .setSurface(*_surface.get())
            .setMinImageCount(_imageCount)
            .setImageFormat(_surfaceFormat.format)
            .setImageColorSpace(_surfaceFormat.colorSpace)
            .setImageExtent({ _extent.x, _extent.y })
            .setImageArrayLayers(1)
            .setImageUsage(::vk::ImageUsageFlagBits::eColorAttachment | ::vk::ImageUsageFlagBits::eTransferDst)
            .setImageSharingMode(::vk::SharingMode::eExclusive)
            .setQueueFamilyIndices(queueFamilyIndices)
            .setPreTransform(surfaceCapabilities.currentTransform)
            .setCompositeAlpha(compositeAlpha)
            .setPresentMode(_presentMode)
            .setOldSwapchain(oldSwapChain)
            .setClipped(true);
        _handle = Context::Device()->createSwapchainKHR(createInfo);

        if (oldSwapChain) {
            Context::Device()->destroySwapchainKHR(oldSwapChain);
        }

        const auto swapChainImageHandles = Context::Device()->getSwapchainImagesKHR(_handle);
        _swapChainImages = Resource<kor::Image>(std::make_unique<kor::vk::Image>(swapChainImageHandles, _extent, getFormat(_surfaceFormat.format), _msaa));

        _depthImages = Image::Builder()
            .setIsPerFrame(true)
            .setExtent(_extent)
            .setFormat(kor::Image::Format::eD32_SFLOAT_S8_UINT)
            .setType(kor::Image::Type::e2D)
            .addUsage(kor::Image::Usage::eDepthStencilAttachment)
            .setMSAA(_msaa)
            .build();

        _swapChainImageViews = kor::ImageView::Builder(_swapChainImages)
            .setViewType(kor::ImageView::Type::e2D)
            .build();
        _depthImageViews = ImageView::Builder(_depthImages)
            .setViewType(kor::ImageView::Type::e2D)
            .build();
    }

    SwapChain::~SwapChain() {
        for (const auto& semaphore : _renderFinishedSemaphores) {
            Context::Device()->destroySemaphore(semaphore);
        }
        if (_handle) {
            Context::Device()->destroySwapchainKHR(_handle);
        }
    }

    void SwapChain::Resize(const glm::uvec2& newSize) {
        _extent = newSize;
        Context::Device()->waitIdle();
        CreateSwapChain();
        kor::Context::DefaultFramebuffer()->Resize(_swapChainImages->getExtent());

    }

    ::vk::Result SwapChain::Acquire(const kor::vk::Frame &frame) {
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

    ::vk::Result SwapChain::Present(const kor::vk::Frame &frame) {
        std::array waitSemaphores = {
        	_renderFinishedSemaphores[_imageIndex]
        };

        std::array swapChains = {
            _handle
        };

        const auto presentInfo = ::vk::PresentInfoKHR()
            .setWaitSemaphores(waitSemaphores)
            .setSwapchains(swapChains)
            .setImageIndices(_imageIndex);
        try {
        	return _presentQueue->presentKHR(presentInfo);
        } catch (const ::vk::OutOfDateKHRError &) {
            return ::vk::Result::eErrorOutOfDateKHR;
        }
    }
}
