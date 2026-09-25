//
// Created by radue on 2/28/2026.
//

#include "scheduler.h"
#include "commandBuffer.h"
#include "log.h"
#include "timeline.h"
#include "../../core/tokenState.h"
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
            kor::log::Error("[vulkan] device lost during {} — the GPU stopped responding (driver "
                            "reset / TDR, or a command the driver refused). Unrecoverable; aborting.",
                            where);
            throw std::runtime_error(std::string("Vulkan device lost during ") + where);
        }
    }

    Frame::Frame(const glm::u32 imageIndex, const Queue& queue) : kor::Frame(imageIndex), _queue(queue)
    {
        _commandBuffer = Context::Device().requestCommandBuffer(_queue);
        _imageAvailable = vk::Context::Device()->createSemaphore({});
        _inFlightFence = Context::Device()->createFence(::vk::FenceCreateInfo().setFlags(::vk::FenceCreateFlagBits::eSignaled));
    }

	Frame::~Frame() {
        Context::Device()->destroySemaphore(_imageAvailable);
        Context::Device()->destroyFence(_inFlightFence);
    }

    void Frame::hold(std::vector<std::unique_ptr<kor::CommandBuffer>> commandBuffers, std::vector<Token> tokens) const {
        _executed = std::move(commandBuffers);
        _tokens = std::move(tokens);
    }

    void Frame::release() const {
        _executed.clear();
        _tokens.clear();
    }

    void Frame::ResetSemaphore() const {
        vk::Context::Device()->destroySemaphore(_imageAvailable);
        _imageAvailable = vk::Context::Device()->createSemaphore({});
    }

    Scheduler::Scheduler(const Builder& createInfo) : kor::Scheduler(createInfo) {}

    void Scheduler::Initialize()
    {
        _swapChain = kor::vk::SwapChain::Builder(dynamic_cast<const kor::vk::Surface&>(kor::Context::Window().RenderSurface()))
            .setImageCount(_imageCount)
            .setSampleCount(SampleCount::e1)
            .build();

        adoptSwapChainSizing();

        CreateFrames();
    }

    void Scheduler::adoptSwapChainSizing()
    {
        // Adopt the swapchain's *actual* image count before anything is sized to it.
        // CurrentImageIndex() returns the driver-acquired image index (0..actualCount-1), and
        // every per-frame resource — the frames, the swap chain's own depth target, and every user
        // Buffer/Image/ImageView/DescriptorSet built later off ImageCount() — is indexed by it.
        // Leaving _imageCount at the requested value while the driver hands out more images made all
        // of those read out of bounds on other GPUs (the render-loop segfault); keeping the two in
        // step is what makes the acquire index always land in range.
        _imageCount = _swapChain->imageCount();

        // Only now, because it allocates one copy per _imageCount. This is why it is not built
        // inside CreateSwapChain: that runs from the SwapChain constructor, before the line above
        // has ever executed, so it would be sized by whatever was *requested*.
        _swapChain->CreateDepthResources();
    }

    void Scheduler::recreateSwapChain(const glm::uvec2& extent)
    {
        _swapChain->Resize(extent);
        // A resize can land on a different image count than the one in force, so re-adopt rather
        // than assuming Initialize's answer still holds.
        adoptSwapChainSizing();

        // Last, because it attaches the views the two steps above just replaced. Through the window
        // rather than Context::DefaultFramebuffer(): that one hands out a const ref, because reading
        // the default framebuffer is all a project ever does with it. Resizing it is the engine's
        // own job, and this is the place that owns it.
        kor::Context::Window().DefaultFramebuffer()->Resize(_swapChain->extent());
    }

    Scheduler::~Scheduler() {
        Context::Device().queuesWaitIdle();
        _frames.clear();
        _swapChain.reset();
        Context::Device().freeQueues();
    }

    void Scheduler::Draw(const std::function<void(kor::CommandBuffer&)>& renderFunc) {
        kor::Scheduler::Draw(renderFunc);
        const auto& frame = dynamic_cast<const kor::vk::Frame&>(CurrentFrame());

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
        // The last submission from this frame is done, and with it everything handed to Execute().
        frame.release();
        // And whatever was destroyed while the GPU might still have been using it, now that it
        // no longer is. Once a frame is the natural cadence for the deferred-deletion queue.
        detail::collectRetired();

        while (true) {
            auto result = _swapChain->Acquire(frame);
            if (result == ::vk::Result::eErrorDeviceLost) {
                reportDeviceLost("swapchain image acquire");
            }
            if (result == ::vk::Result::eErrorOutOfDateKHR || result == ::vk::Result::eSuboptimalKHR) {
                _started = false;
                recreateSwapChain(kor::Context::Window().Extent());
                _started = true;
                // recreate the semaphore
                frame.ResetSemaphore();
                continue;
            }
            break;
        }

        // Before the fence is reset, and so before anything is queued against this image: the image
        // we just acquired may still be owned by an older frame whose submit has not finished.
        _swapChain->ClaimAcquiredImage(fence);

        if (const auto result = Context::Device()->resetFences(1, &fence); result != ::vk::Result::eSuccess) {
            throw std::runtime_error("Failed to reset fence: " + ::vk::to_string(result));
        }

        auto& commandBuffer = frame.Commands();
        const auto& vkCommandBuffer = dynamic_cast<kor::vk::CommandBuffer&>(commandBuffer);
        commandBuffer.Reset();
        commandBuffer.Begin();
        renderFunc(commandBuffer);

        // After the render callback, which may Execute() work of its own.
        auto pending = TakePending();

        SubmitInfo submitInfo {
            .waitSemaphores = { frame.getImageAvailableSemaphore() },
            .waitValues = { 0 },
            .waitStages = { ::vk::PipelineStageFlagBits::eColorAttachmentOutput },
            .signalSemaphores = { _swapChain->getCurrentRenderFinishedSemaphore() },
            .signalValues = { 0 },
            .fence = frame.getInFlightFence()
        };

        // Ended here, in exactly the order they run — before-work, the frame, after-work — because
        // each End() resolves its barriers against where the one before it left every resource.
        std::vector<std::unique_ptr<kor::CommandBuffer>> executed;
        const auto endAndAppend = [&](std::vector<std::unique_ptr<kor::CommandBuffer>>& list) {
            for (auto& external : list) {
                const auto& vkExternal = dynamic_cast<const kor::vk::CommandBuffer&>(*external);
                if (vkExternal.getQueue().getIdentifier() != vkCommandBuffer.getQueue().getIdentifier()) {
                    // Barriers do not reach across queues; running it here would be unsynchronised.
                    kor::log::Error("[scheduler] a command buffer handed to Execute() belongs to a "
                                    "different queue than the frame's and was not run. Submit it on "
                                    "its own with a token, and WaitFor() that token instead.");
                    external->Reset();
                } else {
                    external->End();
                    submitInfo.commandBuffers.push_back(*vkExternal);
                }
                executed.push_back(std::move(external));
            }
        };
        endAndAppend(pending.before);
        commandBuffer.End();
        submitInfo.commandBuffers.push_back(*vkCommandBuffer);
        endAndAppend(pending.after);

        std::vector<Token> tokens;
        auto& reactor = Context::Tokens();
        for (const auto& token : pending.waits) {
            if (token.Ready()) continue;
            const auto [semaphore, value] = reactor.resolve(token);
            submitInfo.waitSemaphores.push_back(semaphore);
            submitInfo.waitValues.push_back(value);
            // Nothing says which stage needs it, so nothing may start early.
            submitInfo.waitStages.push_back(::vk::PipelineStageFlagBits::eAllCommands);
            tokens.push_back(token);
        }
        {
            const auto [semaphore, value] = reactor.resolve(pending.completion);
            submitInfo.signalSemaphores.push_back(semaphore);
            submitInfo.signalValues.push_back(value);
            tokens.push_back(pending.completion);
        }
        frame.hold(std::move(executed), std::move(tokens));

        vkCommandBuffer.getQueue().Submit(submitInfo);
        TokenReactor::noteSubmittedSignal(pending.completion);

        // The frame is submitted and the GPU will hold it until its WaitFor() tokens arrive — but a
        // present may not depend on a signal nobody has submitted yet. A token the CPU still has to
        // signal therefore holds up the *present*, here; one a submission already on a queue will
        // signal does not.
        for (const auto& token : pending.waits) {
            if (!TokenReactor::signalIsOnItsWay(token)) token.Wait();
        }

        const auto presentResult = _swapChain->Present(frame);
        if (presentResult == ::vk::Result::eErrorDeviceLost) {
            reportDeviceLost("present");
        }
        if (presentResult == ::vk::Result::eErrorOutOfDateKHR || presentResult == ::vk::Result::eSuboptimalKHR) {
            _started = false;
            recreateSwapChain(kor::Context::Window().Extent());
            _started = true;
            return;
        }

        AdvanceFrame();
    }

    void Scheduler::CreateFrames() {
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
