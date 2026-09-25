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

#include <algorithm>
#include <iostream>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

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
        _inFlightFence = Context::Device()->createFence(::vk::FenceCreateInfo().setFlags(::vk::FenceCreateFlagBits::eSignaled));
    }

	Frame::~Frame() {
        Context::Device()->destroyFence(_inFlightFence);
    }

    void Frame::hold(std::vector<std::unique_ptr<kor::CommandBuffer>> commandBuffers, std::vector<Token> tokens) const {
        _executed = std::move(commandBuffers);
        _tokens = std::move(tokens);
    }

    void Frame::release() const {
        // The frame's fence has passed: whatever the executed command buffers timed is readable now.
        for (const auto& commandBuffer : _executed) commandBuffer->DeliverTimings();
        _executed.clear();
        _tokens.clear();
    }

    Scheduler::Scheduler(const Builder& createInfo) : kor::Scheduler(createInfo) {}

    void Scheduler::Initialize()
    {
        // The frames in flight are what was asked for, and stay that: every per-frame resource is sized
        // to them. Each window's swap chain has however many images its driver gives it, which is its
        // own business — its image follows the image it acquired, not the frame.
        CreateFrames();
    }

    void Scheduler::recreateSwapChain(kor::Window& window)
    {
        auto& swapChain = dynamic_cast<kor::vk::Surface&>(*window._surface).swapChain();
        swapChain.Resize(window.Extent());
        // Last, because it attaches the views the step above just replaced. Through the window
        // rather than Context::DefaultFramebuffer(): that one hands out a const ref, because reading
        // the default framebuffer is all a project ever does with it. Resizing it is the engine's
        // own job, and this is the place that owns it.
        window.DefaultFramebuffer()->Resize(swapChain.extent());
    }

    void Scheduler::RetireWindow(std::shared_ptr<kor::Surface> surface, GLFWwindow* window)
    {
        // Out of sight at once; destroyed once the frame being built — which may still present it —
        // has finished on the GPU.
        if (window) glfwHideWindow(window);
        _retired.push_back({std::move(surface), window, FrameCompletion()});
    }

    void Scheduler::destroyRetiredWindows(const bool all)
    {
        if (all) WaitIdle();
        std::erase_if(_retired, [all](Retired& retired) {
            if (!all && !retired.lastUse.Ready()) return false;
            retired.surface.reset();   // the swap chain, then the surface
            if (retired.window) glfwDestroyWindow(retired.window);
            return true;
        });
    }

    Scheduler::~Scheduler() {
        Context::Device().queuesWaitIdle();
        destroyRetiredWindows(true);
        _frames.clear();
        Context::Device().freeQueues();
    }

    void Scheduler::Draw(std::span<kor::Window* const> windows, const std::function<void(kor::CommandBuffer&)>& renderFunc) {
        kor::Scheduler::Draw(windows, renderFunc);
        // On to the next frame in flight at the start, not the end: between frames, CurrentImageIndex()
        // keeps naming the frame just drawn — which is what reading a per-frame resource back after a
        // frame expects to find.
        if (_drawnOnce) AdvanceFrame();
        _drawnOnce = true;
        const auto& frame = dynamic_cast<const kor::vk::Frame&>(CurrentFrame());
        const glm::u32 slot = frame.ImageIndex();

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
        destroyRetiredWindows(false);

        // ---- an image from every window ------------------------------------------------------------
        // Held for the frame: a window a scene closes halfway through it is still presented by it.
        struct Shown {
            std::shared_ptr<kor::Surface> surface;
            kor::vk::SwapChain* swapChain;
        };
        std::vector<Shown> shown;
        for (kor::Window* window : windows) {
            window->_shownThisFrame = false;
            if (window->IsPaused()) continue;
            auto& swapChain = dynamic_cast<kor::vk::Surface&>(*window->_surface).swapChain();
            if (window->HasResized() || swapChain.extent() != window->Extent()) recreateSwapChain(*window);
            // A stale swap chain is rebuilt and asked again; a window that still has no image after
            // that is not shown this frame rather than holding every other window up.
            bool acquired = false;
            for (int attempt = 0; attempt < 3 && !acquired; ++attempt) {
                const auto result = swapChain.Acquire(slot);
                if (result == ::vk::Result::eErrorDeviceLost) reportDeviceLost("swapchain image acquire");
                if (result == ::vk::Result::eSuccess || result == ::vk::Result::eSuboptimalKHR) {
                    // Suboptimal still acquired, and still signals: used this frame, rebuilt next.
                    acquired = true;
                    break;
                }
                _started = false;
                recreateSwapChain(*window);
                _started = true;
                // A failed acquire may leave the semaphore pending: start that slot's over.
                swapChain.ResetImageAvailable(slot);
            }
            if (!acquired) continue;
            window->_shownThisFrame = true;
            shown.push_back({window->_surface, &swapChain});
        }

        // Before the fence is reset, and so before anything is queued against these images: an image
        // just acquired may still be owned by an older frame whose submit has not finished.
        for (const auto& s : shown) s.swapChain->ClaimAcquiredImage(fence);

        if (const auto result = Context::Device()->resetFences(1, &fence); result != ::vk::Result::eSuccess) {
            throw std::runtime_error("Failed to reset fence: " + ::vk::to_string(result));
        }

        auto& commandBuffer = frame.Commands();
        const auto& vkCommandBuffer = dynamic_cast<kor::vk::CommandBuffer&>(commandBuffer);
        commandBuffer.Reset();
        commandBuffer.Begin();
        _buildingFrame = true;
        try {
            renderFunc(commandBuffer);
        } catch (...) {
            _buildingFrame = false;
            throw;
        }

        // After the render callback, which may Execute() work of its own.
        auto pending = TakePending();
        _buildingFrame = false;

        // ---- every window's image, made presentable --------------------------------------------------
        // After everything else this frame — and cleared first when nothing drew into it, so a window
        // never shows what an older frame left in that image.
        if (!shown.empty()) {
            auto present = kor::CommandBuffer::Create(kor::CommandBuffer::Usage::eGraphics);
            present->Begin();
            for (const auto& s : shown) {
                const auto image = s.swapChain->image();
                const auto touches = [&](const std::vector<std::unique_ptr<kor::CommandBuffer>>& list) {
                    return std::ranges::any_of(list, [&](const auto& cb) { return cb && cb->HasTouched(image); });
                };
                if (!touches(pending.before) && !commandBuffer.HasTouched(image) && !touches(pending.after))
                    present->ClearColorImage(image, glm::vec4(0.f, 0.f, 0.f, 1.f));
                present->ImageBarrier({ image, ResourceAccess::ePresent });
            }
            pending.after.push_back(std::move(present));
        }

        SubmitInfo submitInfo { .fence = frame.getInFlightFence() };
        for (const auto& s : shown) {
            submitInfo.waitSemaphores.push_back(s.swapChain->getImageAvailableSemaphore(slot));
            submitInfo.waitValues.push_back(0);
            // First written as a colour attachment, or by a copy or a blit: the wait holds back both.
            submitInfo.waitStages.push_back(::vk::PipelineStageFlagBits::eColorAttachmentOutput | ::vk::PipelineStageFlagBits::eTransfer);
            submitInfo.signalSemaphores.push_back(s.swapChain->getCurrentRenderFinishedSemaphore());
            submitInfo.signalValues.push_back(0);
        }

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

        // ---- every window at once --------------------------------------------------------------------
        // One present per queue that presents them (on any ordinary device, one for all), so the
        // windows show the frame together.
        std::vector<bool> done(shown.size(), false);
        for (std::size_t first = 0; first < shown.size(); ++first) {
            if (done[first]) continue;
            const auto& queue = shown[first].swapChain->getPresentQueue();
            std::vector<std::size_t> group;
            for (std::size_t i = first; i < shown.size(); ++i)
                if (!done[i] && &shown[i].swapChain->getPresentQueue() == &queue) { group.push_back(i); done[i] = true; }

            std::vector<::vk::Semaphore> waits;
            std::vector<::vk::SwapchainKHR> swapChains;
            std::vector<glm::u32> indices;
            for (const auto i : group) {
                waits.push_back(shown[i].swapChain->getCurrentRenderFinishedSemaphore());
                swapChains.push_back(**shown[i].swapChain);
                indices.push_back(shown[i].swapChain->currentImageIndex());
            }
            std::vector<::vk::Result> results(group.size(), ::vk::Result::eSuccess);
            const auto presentInfo = ::vk::PresentInfoKHR()
                .setWaitSemaphores(waits)
                .setSwapchains(swapChains)
                .setImageIndices(indices)
                .setResults(results);
            try {
                const auto lock = Context::Device().lockQueues();
                (void)queue->presentKHR(presentInfo);
            } catch (const ::vk::OutOfDateKHRError &) {
                // Which one is in `results`.
            } catch (const ::vk::DeviceLostError &) {
                reportDeviceLost("present");
            }
            // A stale swap chain is rebuilt when its window next acquires.
            for (std::size_t k = 0; k < group.size(); ++k)
                if (results[k] == ::vk::Result::eErrorDeviceLost) reportDeviceLost("present");
        }
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
