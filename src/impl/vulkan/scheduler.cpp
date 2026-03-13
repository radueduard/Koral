//
// Created by radue on 2/28/2026.
//

#include "scheduler.h"

#include "surface.h"
#include "swapChain.h"
#include "vulkanContext.h"

#include "io/window.h"


namespace gfx::vk
{
    Frame::Frame(const glm::u32 imageIndex, const Queue& queue) : gfx::Frame(imageIndex), _queue(queue)
    {
        _commandBuffer = Context::Device().requestCommandBuffer(_queue, Context::ThreadId());
        _imageAvailable = vk::Context::Device()->createSemaphore({});
        _readyToPresent = vk::Context::Device()->createSemaphore({});
        _inFlightFence = Context::Device()->createFence(::vk::FenceCreateInfo().setFlags(::vk::FenceCreateFlagBits::eSignaled));
    }

	Frame::~Frame() {
        Context::Device()->destroySemaphore(_imageAvailable);
        Context::Device()->destroySemaphore(_readyToPresent);
        Context::Device()->destroyFence(_inFlightFence);
    }

    Scheduler::Scheduler(const Builder& createInfo) : gfx::Scheduler(createInfo) {}

    void Scheduler::Initialize()
    {
        _surface = std::make_unique<Surface>(gfx::Context::Window());
        _swapChain = gfx::vk::SwapChain::Builder(*_surface)
            .setMinImageCount(_minImageCount)
            .setImageCount(_imageCount)
            .setMSAA(MSAA::eNone)
            .build();
        createFrames();
    }

    Scheduler::~Scheduler() {
        Context::Device()->waitIdle();
    }

    void Scheduler::Draw(const std::function<void(gfx::CommandBuffer&)>& renderFunc) const {
        gfx::Scheduler::Draw(renderFunc);
        const auto& frame = dynamic_cast<const gfx::vk::Frame&>(getCurrentFrame());

        const auto& fence = frame.getInFlightFence();
        if (const auto result = Context::Device()->waitForFences(1, &fence, ::vk::True, UINT64_MAX); result != ::vk::Result::eSuccess) {
            throw std::runtime_error("Failed to wait fence: " + ::vk::to_string(result));
        }

        if (const auto result = Context::Device()->resetFences(1, &fence); result != ::vk::Result::eSuccess) {
            throw std::runtime_error("Failed to reset fence: " + ::vk::to_string(result));
        }

        if (const auto result = _swapChain->Acquire(frame);
            result == ::vk::Result::eErrorOutOfDateKHR || result == ::vk::Result::eSuboptimalKHR) {
            _swapChain->Resize(gfx::Context::Window().getExtent());
        }

        auto& commandBuffer = frame.getCommandBuffer();
        const auto& vkCommandBuffer = dynamic_cast<gfx::vk::CommandBuffer&>(commandBuffer);
        commandBuffer.Reset();
        commandBuffer.Begin();
        _swapChain->getImage().TransitionLayout(vkCommandBuffer, ::vk::ImageLayout::eColorAttachmentOptimal);
        renderFunc(commandBuffer);
        _swapChain->getImage().TransitionLayout(vkCommandBuffer, ::vk::ImageLayout::ePresentSrcKHR);
        commandBuffer.End();

        const SubmitInfo submitInfo {
            .commandBuffer = dynamic_cast<const gfx::vk::CommandBuffer&>(commandBuffer),
            .waitSemaphores = { frame.getImageAvailableSemaphore() },
            .waitStages = { ::vk::PipelineStageFlagBits::eColorAttachmentOutput },
            .signalSemaphores = { frame.getReadyToPresentSemaphore() },
            .fence = frame.getInFlightFence()
        };

        const auto& vkFrame = dynamic_cast<const gfx::vk::Frame&>(frame);
        vkFrame.getQueue().Submit(submitInfo);

        if (const auto result = _swapChain->Present(frame);
            result == ::vk::Result::eErrorOutOfDateKHR || result == ::vk::Result::eSuboptimalKHR) {
            _swapChain->Resize(gfx::Context::Window().getExtent());
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
}
