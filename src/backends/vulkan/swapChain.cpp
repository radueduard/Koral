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

namespace gfx::vk
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
        _extent(1, 1),
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

        const auto compositeAlpha = gfx::Context::Window().isFramebufferTransparent() ?
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
        _swapChainImages = Resource<gfx::Image>(std::make_unique<gfx::vk::Image>(swapChainImageHandles, _extent, getFormat(_surfaceFormat.format), _msaa));

        _depthImages = Image::Builder()
            .setIsPerFrame(true)
            .setExtent(_extent)
            .setFormat(gfx::Image::Format::eD32_SFLOAT_S8_UINT)
            .setType(gfx::Image::Type::e2D)
            .addUsage(gfx::Image::Usage::eDepthStencilAttachment)
            .setMSAA(_msaa)
            .build();

        _swapChainImageViews = gfx::ImageView::Builder(_swapChainImages)
            .setViewType(gfx::ImageView::Type::e2D)
            .build();
        _depthImageViews = ImageView::Builder(_depthImages)
            .setViewType(gfx::ImageView::Type::e2D)
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
        gfx::Context::DefaultFramebuffer()->Resize(_swapChainImages->getExtent());

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
