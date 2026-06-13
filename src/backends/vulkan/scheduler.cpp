//
// Created by radue on 2/28/2026.
//

module;

#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>

module gfx;
import :vk_scheduler;
import :vk_device;
import :vk_context;
import :vk_surface;
import :vk_swapChain;
import :vk_commandBuffer;

import :scheduler;
import :commandBuffer;
import :context;
import :window;

namespace gfx::vk
{
    Frame::Frame(const glm::u32 imageIndex, const Queue& queue) : gfx::Frame(imageIndex), _queue(queue)
    {
        setCommandBuffer(Context::Device().requestCommandBuffer(_queue, std::hash<std::thread::id>{}(std::this_thread::get_id())));
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

    Scheduler::Scheduler(const Builder& createInfo) : gfx::Scheduler(createInfo) {}

    void Scheduler::Initialize()
    {
        _swapChain = gfx::vk::SwapChain::Builder(dynamic_cast<const gfx::vk::Surface&>(gfx::Context::GetWindow().getSurface()))
            .setMinImageCount(_minImageCount)
            .setImageCount(_imageCount)
            .setSampleCount(SampleCount::e1)
            .build();
        createFrames();
    }

    Scheduler::~Scheduler() {
        Context::Device().queuesWaitIdle();
        _frames.clear();
        _swapChain.reset();
        Context::Device().freeQueues();
    }

    void Scheduler::Draw(const std::function<void(gfx::CommandBuffer&)>& renderFunc) const {
        gfx::Scheduler::Draw(renderFunc);
        const auto& frame = dynamic_cast<const gfx::vk::Frame&>(getCurrentFrame());

        const auto& fence = frame.getInFlightFence();
        if (const auto result = Context::Device()->waitForFences(1, &fence, ::vk::True, UINT64_MAX); result != ::vk::Result::eSuccess) {
            throw std::runtime_error("Failed to wait fence: " + ::vk::to_string(result));
        }

        // Wayland (and HiDPI in general) doesn't always surface size mismatches as
        // VK_SUBOPTIMAL/OUT_OF_DATE — the compositor will happily scale a stale
        // buffer instead. Compare against the live framebuffer size so the swapchain
        // tracks fractional-scale events that arrive after window creation.
        if (const auto windowExtent = gfx::Context::GetWindow().getExtent();
            windowExtent.x > 0 && windowExtent.y > 0 && windowExtent != _swapChain->getExtent()) {
            _started = false;
            _swapChain->Resize(windowExtent);
            _started = true;
        }

        while (true) {
            auto result = _swapChain->Acquire(frame);
            if (result == ::vk::Result::eErrorOutOfDateKHR || result == ::vk::Result::eSuboptimalKHR) {
                _started = false;
                _swapChain->Resize(gfx::Context::GetWindow().getExtent());
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
        const auto& vkCommandBuffer = dynamic_cast<gfx::vk::CommandBuffer&>(commandBuffer);
        commandBuffer.Reset();
        commandBuffer.Begin();
        renderFunc(commandBuffer);
        commandBuffer.End();

        const SubmitInfo submitInfo {
            .commandBuffer = dynamic_cast<const gfx::vk::CommandBuffer&>(commandBuffer),
            .waitSemaphores = { frame.getImageAvailableSemaphore() },
            .waitStages = { ::vk::PipelineStageFlagBits::eColorAttachmentOutput },
            .signalSemaphores = { _swapChain->getCurrentRenderFinishedSemaphore() },
            .fence = frame.getInFlightFence()
        };

        vkCommandBuffer.getQueue().Submit(submitInfo);

        if (const auto result = _swapChain->Present(frame);
            result == ::vk::Result::eErrorOutOfDateKHR || result == ::vk::Result::eSuboptimalKHR) {
            _started = false;
            _swapChain->Resize(gfx::Context::GetWindow().getExtent());
            _started = true;
            return;
        }

        advanceFrame();
    }

    glm::u32 Scheduler::getCurrentImageIndex() const { return _swapChain->getCurrentImageIndex(); }

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
