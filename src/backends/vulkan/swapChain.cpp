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

    kor::UVec2 SwapChain::ChooseExtent(const ::vk::SurfaceCapabilitiesKHR &capabilities, const kor::UVec2& extent) {
        if (capabilities.currentExtent.width != UINT32_MAX) {
            return kor::UVec2(capabilities.currentExtent.width, capabilities.currentExtent.height);
        }
        kor::UVec2 actualExtent = extent;
        actualExtent.x = std::max<kor::u32>(capabilities.minImageExtent.width, std::min<kor::u32>(capabilities.maxImageExtent.width, actualExtent.x));
        actualExtent.y = std::max<kor::u32>(capabilities.minImageExtent.height, std::min<kor::u32>(capabilities.maxImageExtent.height, actualExtent.y));
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

        for (kor::u32 i = 0; i < std::max(createInfo.framesInFlight, 1u); ++i)
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
        //
        // And one more than it demands, at least. With exactly the minimum, every image is either on screen or
        // waiting to be: there is none to draw the next frame into until the compositor gives one back, and
        // acquiring waits for that — which paces a program at the compositor's rate whatever its present mode,
        // vsync off included (125 frames a second on a 240 Hz display, of a picture that takes a fifth of a
        // millisecond to draw). The spare image is what lets mailbox and immediate run as fast as they are drawn.
        kor::u32 requestedImageCount = std::max(_requestedImageCount, surfaceCapabilities.minImageCount + 1);
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
        _imageCount = static_cast<kor::u32>(swapChainImageHandles.size());

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

    void SwapChain::DestroyPresented() {
        for (const auto& [fence, semaphore] : _presented) {
            Context::Device()->destroyFence(fence);
            Context::Device()->destroySemaphore(semaphore);
        }
        for (const auto& fence : _freeFences) Context::Device()->destroyFence(fence);
        for (const auto& semaphore : _freeSemaphores) Context::Device()->destroySemaphore(semaphore);
        // A semaphore claimed for a present that never came is in neither list.
        if (_renderFinished && std::ranges::find(_renderFinishedSemaphores, _renderFinished) == _renderFinishedSemaphores.end())
            Context::Device()->destroySemaphore(_renderFinished);
        _renderFinished = nullptr;
        _presented.clear();
        _freeFences.clear();
        _freeSemaphores.clear();
    }

    SwapChain::~SwapChain() {
        // A present's fence may never signal if the present failed, so not waited for one by one.
        if (!_presented.empty()) Context::Device().waitIdle();
        DestroyPresented();
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

    void SwapChain::Resize(const kor::UVec2& newSize) {
        _extent = newSize;
        Context::Device().waitIdle();
        // Presents to the old swap chain that failed signal no fence: idle is the only word on them.
        DestroyPresented();
        CreateSwapChain();
        CreateDepthResources();
        // The window's default framebuffer is re-pointed by whoever resized it. @see vk::Scheduler
    }

    void SwapChain::ResetImageAvailable(const kor::u32 slot) {
        Context::Device()->destroySemaphore(_imageAvailable[slot]);
        _imageAvailable[slot] = Context::Device()->createSemaphore({});
    }

    ::vk::Result SwapChain::Acquire(const kor::u32 slot) {
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
        if (Context::Device().supportsPresentFences()) {
            // A semaphore no present still waits for, rather than the image's own. A driver hands
            // back an image whose present has not happened yet — in immediate mode, routinely the one
            // the frame before drew — and the image's own semaphore is then still to be waited for by
            // that present; waiting for that frame instead, as below, serialises CPU and GPU.
            std::erase_if(_presented, [this](const Presented& presented) {
                if (Context::Device()->getFenceStatus(presented.fence) != ::vk::Result::eSuccess) return false;
                _freeFences.push_back(presented.fence);
                _freeSemaphores.push_back(presented.semaphore);
                return true;
            });
            if (!_freeFences.empty())
                (void)Context::Device()->resetFences(static_cast<kor::u32>(_freeFences.size()), _freeFences.data());
            if (_freeSemaphores.empty()) {
                _renderFinished = Context::Device()->createSemaphore({});
            } else {
                _renderFinished = _freeSemaphores.back();
                _freeSemaphores.pop_back();
            }
            return;
        }

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
        _renderFinished = _renderFinishedSemaphores[_imageIndex];
    }

    ::vk::Fence SwapChain::PresentFence() {
        ::vk::Fence fence;
        if (_freeFences.empty()) {
            fence = Context::Device()->createFence({});
        } else {
            fence = _freeFences.back();
            _freeFences.pop_back();
        }
        _presented.push_back({fence, _renderFinished});
        _renderFinished = nullptr;   // the present's now
        return fence;
    }
}
