//
// Created by radue on 2/28/2026.
//

#include "swapChain.h"

#include <framebuffer.h>
#include <ranges>
#include <iostream>
#include <tuple>

#include <magic_enum/magic_enum.hpp>

#include "log.h"

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
    std::pair<::vk::SurfaceFormatKHR, kor::Window::Format> SwapChain::ChooseSurfaceFormat(const std::vector<::vk::SurfaceFormatKHR>& availableFormats) const {
        const auto offered = [&](const ::vk::Format format) {
            return std::ranges::find_if(availableFormats, [&](const ::vk::SurfaceFormatKHR& available) {
                return available.format == format && available.colorSpace == ::vk::ColorSpaceKHR::eSrgbNonlinear;
            });
        };
        for (const auto wanted : _formats)
            if (const auto it = offered(getVkFormat(wanted)); it != availableFormats.end()) return {*it, wanted};
        // None of those: whatever the display offers that the engine presents in, said out loud.
        for (const auto& available : availableFormats) {
            if (available.colorSpace != ::vk::ColorSpaceKHR::eSrgbNonlinear) continue;
            if (const auto format = windowFormat(available.format)) {
                kor::log::Warn("[window] the display offers none of the formats asked for; presenting in {}",
                               magic_enum::enum_name(*format));
                return {available, *format};
            }
        }
        throw std::runtime_error("The display offers no format a window can present in (8-bit RGBA or BGRA, sRGB colour space).");
    }

    ::vk::PresentModeKHR SwapChain::ChoosePresentMode(const std::vector<::vk::PresentModeKHR> &availablePresentModes, const bool vsync) {
        if (!vsync) {
            // VSync off: present uncapped. Prefer immediate (may tear); Fifo is the
            // guaranteed-available fallback if the driver lacks an immediate mode.
            for (const auto &availablePresentMode : availablePresentModes) {
                if (availablePresentMode == ::vk::PresentModeKHR::eImmediate) {
                    return availablePresentMode;
                }
            }
            return ::vk::PresentModeKHR::eFifo;
        }

        // VSync on (no tearing): prefer mailbox for lower latency; then the mode that is mailbox in all
        // but name where there is none (NVIDIA on X11 — where Fifo, under XWayland, shows fewer frames
        // than the display has refreshes even of a picture that costs nothing to draw); else Fifo
        // (always available).
        const auto has = [&](const ::vk::PresentModeKHR mode) { return std::ranges::find(availablePresentModes, mode) != availablePresentModes.end(); };
        if (has(::vk::PresentModeKHR::eMailbox)) return ::vk::PresentModeKHR::eMailbox;
        if (has(::vk::PresentModeKHR::eFifoLatestReady) && Context::Device().supportsFifoLatestReady()) return ::vk::PresentModeKHR::eFifoLatestReady;
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
        _extent(createInfo.extent),
        _vsync(createInfo.vsync),
        _transparent(createInfo.transparent),
        _alphaVisual(createInfo.alphaVisual),
        _sampleCount(createInfo.sampleCount),
        _requestedImageCount(createInfo.imageCount),
        _surface(createInfo.surface),
        _presentQueue(Context::Device().requestPresentQueue(_surface))
    {
        _formats = createInfo.formats;
        vk::Context::Device().waitIdle();

        for (glm::u32 i = 0; i < std::max(createInfo.framesInFlight, 1u); ++i)
            _imageAvailable.push_back(Context::Device()->createSemaphore({}));
        CreateSwapChain();
        CreateDepthResources();
    }

    void SwapChain::CreateSwapChain() {
        const auto surfaceCapabilities = _surface.get().getCapabilities();

        std::tie(_surfaceFormat, _windowFormat) = ChooseSurfaceFormat(_surface.get().getFormats());
        _presentMode = ChoosePresentMode(_surface.get().getPresentModes(), _vsync);
        _extent = ChooseExtent(surfaceCapabilities, _extent);

        // Request at least what the surface demands. The requested count is our preferred floor, but the
        // surface can require more (3 is common) — asking for fewer is a spec violation the validation
        // layer flags, so clamp up. maxImageCount == 0 means "no upper bound"; when it is set, stay
        // within it. The driver may still hand out more than requested; that actual count is adopted
        // from getSwapchainImagesKHR below.
        glm::u32 requestedImageCount = std::max(_requestedImageCount, surfaceCapabilities.minImageCount);
        if (surfaceCapabilities.maxImageCount > 0)
            requestedImageCount = std::min(requestedImageCount, surfaceCapabilities.maxImageCount);

        const ::vk::SwapchainKHR oldSwapChain = _handle;
        const auto queueFamilyIndices = std::array { _presentQueue.getFamily().getIndex() };

        // A transparent window composites with the desktop only where the surface says it can: asking
        // for a mode it does not offer is an error, and a window that was meant to be see-through and
        // is opaque instead has to be something its owner can find out (Window::CompositesWithDesktop).
        auto compositeAlpha = ::vk::CompositeAlphaFlagBitsKHR::eOpaque;
        _composites = false;
        if (_transparent) {
            for (const auto wanted : { ::vk::CompositeAlphaFlagBitsKHR::ePreMultiplied, ::vk::CompositeAlphaFlagBitsKHR::ePostMultiplied }) {
                if (!(surfaceCapabilities.supportedCompositeAlpha & wanted)) continue;
                compositeAlpha = wanted;
                _composites = true;
                break;
            }
            if (!_composites && _alphaVisual) {
                // The window's visual is what composites it (X11): the surface's own word for that is
                // "inherit" where it has one, and drivers that only say "opaque" leave the alpha alone.
                if (surfaceCapabilities.supportedCompositeAlpha & ::vk::CompositeAlphaFlagBitsKHR::eInherit)
                    compositeAlpha = ::vk::CompositeAlphaFlagBitsKHR::eInherit;
                _composites = true;
            }
            if (!_composites)
                kor::log::Warn("[window] a transparent window was asked for, but the display composites none ({}); it is opaque",
                               ::vk::to_string(surfaceCapabilities.supportedCompositeAlpha));
        }

        const auto createInfo = ::vk::SwapchainCreateInfoKHR()
            .setSurface(*_surface.get())
            // Request the stable, surface-clamped floor computed above — never the (possibly grown)
            // actual _imageCount, or a driver that hands out one more than asked would ratchet the count
            // up on every Resize.
            .setMinImageCount(requestedImageCount)
            .setImageFormat(_surfaceFormat.format)
            .setImageColorSpace(_surfaceFormat.colorSpace)
            .setImageExtent({ _extent.x, _extent.y })
            .setImageArrayLayers(1)
            // Transfer source too, so what a window shows can be copied out — a screenshot, a test.
            .setImageUsage(::vk::ImageUsageFlagBits::eColorAttachment | ::vk::ImageUsageFlagBits::eTransferDst
                         | ::vk::ImageUsageFlagBits::eTransferSrc)
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

        // The requested count is a floor, not an exact request: the driver is free to allocate more
        // images than asked for, and getSwapchainImagesKHR reports how many it actually made. Adopt
        // that real count as _imageCount from here on, because everything downstream (the scheduler's
        // frame/resource sizing, the per-image semaphores below) has to be sized to the number of
        // images the acquire index can actually reach — not the number we requested.
        _imageCount = static_cast<glm::u32>(swapChainImageHandles.size());

        // The render-finished semaphore is per *swapchain image*: it is signalled by the submit that
        // renders into the acquired image and waited on by that image's present, and indexed by the
        // image index acquireNextImageKHR hands back. The request is only a floor, so a driver
        // may hand out more images than requested (3-4 is common) — sizing this to the requested
        // count instead of getSwapchainImagesKHR's actual count let `_renderFinishedSemaphores[_imageIndex]`
        // read out of bounds on those drivers, which is what crashed the submit/present path on other
        // GPUs while working here. Size to the real image count, and rebuild across Resize.
        for (const auto& semaphore : _renderFinishedSemaphores) {
            Context::Device()->destroySemaphore(semaphore);
        }
        _renderFinishedSemaphores.clear();
        _renderFinishedSemaphores.reserve(swapChainImageHandles.size());
        for (size_t i = 0; i < swapChainImageHandles.size(); i++) {
            _renderFinishedSemaphores.push_back(Context::Device()->createSemaphore({}));
        }

        // Sized alongside them, and cleared here: Resize waits for the device to go idle before
        // recreating, so nothing is in flight and no image has an owner any more.
        _imagesInFlight.assign(swapChainImageHandles.size(), nullptr);

        // Which of them a command means is the one acquired for this frame — never the frame in flight.
        _swapChainImages = Resource<kor::Image>(std::make_unique<kor::vk::Image>(
            swapChainImageHandles, _extent, _windowFormat, _sampleCount,
            [this] { return _imageIndex.load(); }));

        _swapChainImageViews = kor::ImageView::Builder(_swapChainImages)
            .SetViewType(kor::ImageView::Type::e2D)
            .Build();
    }

    void SwapChain::CreateDepthResources() {
        // A per-frame image: one copy per frame in flight, which is what it is indexed by. It is not
        // presented, so it has no reason to follow the swap chain's own images.
        _depthImages = Image::Builder()
            .SetIsPerFrame(true)
            .SetExtent(_extent)
            .SetFormat(kor::Image::Format::eD32_SFLOAT_S8_UINT)
            .SetType(kor::Image::Type::e2D)
            // Projects reach this through the default framebuffer and may sample or blit it, so
            // it keeps the roles the old permissive default gave it rather than just the one
            // the swap chain itself needs.
            .SetUsage(kor::Image::Usage::eDepthStencilAttachment
                    | kor::Image::Usage::eSampled
                    | kor::Image::Usage::eTransferSrc
                    | kor::Image::Usage::eTransferDst)
            .SetSampleCount(_sampleCount)
            .Build();

        _depthImageViews = ImageView::Builder(_depthImages)
            .SetViewType(kor::ImageView::Type::e2D)
            .Build();
    }

    SwapChain::~SwapChain() {
        for (const auto& semaphore : _renderFinishedSemaphores) {
            Context::Device()->destroySemaphore(semaphore);
        }
        for (const auto& semaphore : _imageAvailable) {
            Context::Device()->destroySemaphore(semaphore);
        }
        if (_handle) {
            Context::Device()->destroySwapchainKHR(_handle);
        }
    }

    void SwapChain::Resize(const glm::uvec2& newSize) {
        _extent = newSize;
        Context::Device().waitIdle();
        CreateSwapChain();
        CreateDepthResources();
        // The window's default framebuffer is re-pointed by whoever resized it. @see vk::Scheduler
    }

    void SwapChain::ResetImageAvailable(const glm::u32 slot) {
        Context::Device()->destroySemaphore(_imageAvailable[slot]);
        _imageAvailable[slot] = Context::Device()->createSemaphore({});
    }

    ::vk::Result SwapChain::Acquire(const glm::u32 slot) {
        try {
            const auto result = Context::Device()->acquireNextImageKHR(
                _handle,
                UINT64_MAX,
                _imageAvailable[slot],
                nullptr);
            _imageIndex = result.value;
            return result.result;
        } catch (const ::vk::OutOfDateKHRError &) {
            return ::vk::Result::eErrorOutOfDateKHR;
        } catch (const ::vk::DeviceLostError &) {
            // vulkan-hpp throws on error codes; hand it back as a result so the scheduler can
            // report it once, uniformly, instead of it escaping as an opaque SystemError.
            return ::vk::Result::eErrorDeviceLost;
        }
    }

    void SwapChain::ClaimAcquiredImage(const ::vk::Fence& frameFence) {
        // This is *not* the wait the scheduler has already done. That one was on the current frame
        // slot's own fence, which says nothing about the other frames in flight — and a driver
        // routinely hands out more swapchain images than there are frames, so the frame that last
        // rendered into this image is usually a different one, with a different fence.
        //
        // Without the wait, that frame's submit could still be signalling this image's
        // renderFinished semaphore while we queue a submit that signals it again, which is
        // VUID-vkQueueSubmit-pSignalSemaphores-00067: a binary semaphore must be unsignalled when
        // the operation signalling it executes.
        if (const ::vk::Fence previous = _imagesInFlight[_imageIndex]) {
            const auto result = Context::Device()->waitForFences(1, &previous, ::vk::True, UINT64_MAX);
            if (result != ::vk::Result::eSuccess) {
                throw std::runtime_error("Failed to wait on the fence holding this swapchain image: " + ::vk::to_string(result));
            }
        }
        _imagesInFlight[_imageIndex] = frameFence;
    }
}
