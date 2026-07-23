//
// Created by radue on 2/28/2026.
//

#include "scheduler.h"
#include <framebuffer.h>
#include <surface.h>
#include <log.h>

#include <iostream>

#include "surface.h"
#include "swapChain.h"
#include "vulkanContext.h"

#include "window.h"


namespace kor::vk
{
    namespace
    {
        // A lost device is unrecoverable here: the engine has no path to tear down and rebuild the
        // whole Vulkan context, so every subsequent frame would deadlock or fault. Turn it into a
        // clear, immediate failure instead of the silent freeze it otherwise becomes — a
        // waitForFences on a lost device blocks for the driver's TDR timeout and the window just
        // stops responding with nothing logged.
        [[noreturn]] void reportDeviceLost(const char* where)
        {
            kor::log::error("[vulkan] device lost during {} — the GPU stopped responding (driver "
                            "reset / TDR, or a command the driver refused). Unrecoverable; aborting.",
                            where);
            throw std::runtime_error(std::string("Vulkan device lost during ") + where);
        }
    }

    Frame::Frame(const glm::u32 imageIndex, const Queue& queue) : kor::Frame(imageIndex), _queue(queue)
    {
        _commandBuffer = Context::Device().requestCommandBuffer(_queue, std::hash<std::thread::id>{}(std::this_thread::get_id()));
        _imageAvailable = vk::Context::Device()->createSemaphore({});
        _inFlightFence = Context::Device()->createFence(::vk::FenceCreateInfo().setFlags(::vk::FenceCreateFlagBits::eSignaled));
    }

	Frame::~Frame() {
        Context::Device()->destroySemaphore(_imageAvailable);
        Context::Device()->destroyFence(_inFlightFence);
    }

    void Frame::ResetSemaphore() const {
        vk::Context::Device()->destroySemaphore(_imageAvailable);
        _imageAvailable = vk::Context::Device()->createSemaphore({});
    }

    Scheduler::Scheduler(const Builder& createInfo) : kor::Scheduler(createInfo) {}

    void Scheduler::Initialize()
    {
        _swapChain = kor::vk::SwapChain::Builder(dynamic_cast<const kor::vk::Surface&>(kor::Context::Window().getSurface()))
            .setMinImageCount(_minImageCount)
            .setImageCount(_imageCount)
            .setMSAA(MSAA::eNone)
            .build();

        // Adopt the swapchain's *actual* image count before anything is sized to it. getCurrentImageIndex()
        // returns the driver-acquired image index (0..actualCount-1), and every per-frame resource — the
        // frames created just below, and every user Buffer/Image/ImageView/DescriptorSet built later off
        // getImageCount() — is indexed by it. Leaving _imageCount at the requested value while the driver
        // hands out more images made all of those read out of bounds on other GPUs (the render-loop
        // segfault); keeping the two in step is what makes the acquire index always land in range.
        _imageCount = _swapChain->getImageCount();

        createFrames();
    }

    Scheduler::~Scheduler() {
        Context::Device().queuesWaitIdle();
        _frames.clear();
        _swapChain.reset();
        Context::Device().freeQueues();
    }

    void Scheduler::Draw(const std::function<void(kor::CommandBuffer&)>& renderFunc) const {
        kor::Scheduler::Draw(renderFunc);
        const auto& frame = dynamic_cast<const kor::vk::Frame&>(getCurrentFrame());

        const auto& fence = frame.getInFlightFence();
        // vulkan-hpp throws on error codes rather than returning them, so a lost device surfaces
        // here as an exception, not a non-success Result.
        ::vk::Result waitResult;
        try {
            waitResult = Context::Device()->waitForFences(1, &fence, ::vk::True, UINT64_MAX);
        } catch (const ::vk::DeviceLostError &) {
            reportDeviceLost("fence wait");
        }
        if (waitResult != ::vk::Result::eSuccess) {
            throw std::runtime_error("Failed to wait fence: " + ::vk::to_string(waitResult));
        }

        while (true) {
            auto result = _swapChain->Acquire(frame);
            if (result == ::vk::Result::eErrorDeviceLost) {
                reportDeviceLost("swapchain image acquire");
            }
            if (result == ::vk::Result::eErrorOutOfDateKHR || result == ::vk::Result::eSuboptimalKHR) {
                _started = false;
                _swapChain->Resize(kor::Context::Window().getExtent());
                _started = true;
                // recreate the semaphore
                frame.ResetSemaphore();
                continue;
            }
            break;
        }

        if (const auto result = Context::Device()->resetFences(1, &fence); result != ::vk::Result::eSuccess) {
            throw std::runtime_error("Failed to reset fence: " + ::vk::to_string(result));
        }

        auto& commandBuffer = frame.getCommandBuffer();
        const auto& vkCommandBuffer = dynamic_cast<kor::vk::CommandBuffer&>(commandBuffer);
        commandBuffer.Reset();
        commandBuffer.Begin();
        renderFunc(commandBuffer);
        commandBuffer.End();

        const SubmitInfo submitInfo {
            .commandBuffer = dynamic_cast<const kor::vk::CommandBuffer&>(commandBuffer),
            .waitSemaphores = { frame.getImageAvailableSemaphore() },
            .waitStages = { ::vk::PipelineStageFlagBits::eColorAttachmentOutput },
            .signalSemaphores = { _swapChain->getCurrentRenderFinishedSemaphore() },
            .fence = frame.getInFlightFence()
        };

        vkCommandBuffer.getQueue().Submit(submitInfo);

        const auto presentResult = _swapChain->Present(frame);
        if (presentResult == ::vk::Result::eErrorDeviceLost) {
            reportDeviceLost("present");
        }
        if (presentResult == ::vk::Result::eErrorOutOfDateKHR || presentResult == ::vk::Result::eSuboptimalKHR) {
            _started = false;
            _swapChain->Resize(kor::Context::Window().getExtent());
            _started = true;
            return;
        }

        advanceFrame();
    }

    void Scheduler::createFrames() {
        _frames.reserve(_imageCount);
        const auto& queue = Context::Device().requestQueue(::vk::QueueFlagBits::eGraphics);
        for (uint32_t i = 0; i < _imageCount; i++) {
            _frames.emplace_back(std::make_unique<Frame>(i, queue));
        }
    }

    void Scheduler::WaitIdle() const
    {
        vk::Context::Device().queuesWaitIdle();
    }
}
