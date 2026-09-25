//
// Created by eduard on 11.03.2026.
//

#include <algorithm>
#include <memory>

#include <log.h>

#include <scheduler.h>
#include <commandBuffer.h>
#include <window.h>
#include <framebuffer.h>
#include <surface.h>

#include <GLFW/glfw3.h>

#include "../backends/vulkan/scheduler.h"

namespace kor
{
    Frame::Frame(const glm::u32 imageIndex) : _imageIndex(imageIndex)
    {
        _commandBuffer = CommandBuffer::Create(CommandBuffer::Usage::eGraphics);
    }

    Resource<Scheduler> Scheduler::Builder::Build() const
    {
        switch (Context::ActiveAPI()) {
        case API::eVulkan:
            return Resource<Scheduler>(std::make_unique<vk::Scheduler>(*this), "Scheduler");
        }
        return Resource<Scheduler>::Failed(
            Error{.code = ErrorCode::eUnknownApi, .message = "No scheduler exists for the active graphics API."}, "Scheduler");
    }

    void Scheduler::RetireWindow(std::shared_ptr<Surface> surface, GLFWwindow* window)
    {
        WaitIdle();
        surface.reset();
        if (window) glfwDestroyWindow(window);
    }

    Scheduler::Scheduler(const Builder& createInfo) :
        _imageCount(createInfo.imageCount) {}

    Token Scheduler::Execute(std::unique_ptr<CommandBuffer> commandBuffer, ExecuteInfo info)
    {
        if (!commandBuffer) {
            log::Error("[scheduler] Execute was handed no command buffer");
            return {};
        }
        if (!commandBuffer->IsRecording()) {
            log::Error("[scheduler] Execute needs a command buffer that has been begun and not ended: "
                       "the frame ends it, so its barriers are resolved in the order it runs. "
                       "Drop the End() call before handing it over.");
            return {};
        }
        const std::uint32_t queue = QueueOf(*commandBuffer);
        std::erase_if(info.after, [](const Token& token) { return token.Ready(); });
        std::lock_guard lock(_pendingMutex);
        const Token done = _executeTimelines[{queue, info.placement}].Next();
        (info.placement == Placement::eBeforeFrame ? _pending.before : _pending.after).push_back(Executed{
            .commandBuffer = std::move(commandBuffer),
            .after = std::move(info.after),
            .group = info.group,
            .done = done,
            .queue = queue,
        });
        return done;
    }

    bool Scheduler::QueuedWorkTouches(const ResourceRef<const Image>& image)
    {
        std::lock_guard lock(_pendingMutex);
        const auto touches = [&](const std::vector<Executed>& list) {
            return std::ranges::any_of(list, [&](const Executed& e) { return e.commandBuffer && e.commandBuffer->HasTouched(image); });
        };
        return touches(_pending.before) || touches(_pending.after);
    }

    void Scheduler::WaitFor(const Token& token)
    {
        if (token.Ready()) return;
        std::lock_guard lock(_pendingMutex);
        _pending.waits.push_back(token);
    }

    Token Scheduler::FrameCompletion()
    {
        std::lock_guard lock(_pendingMutex);
        return _frameTimeline.At(_frameNumber);
    }

    Scheduler::Pending Scheduler::TakePending()
    {
        std::lock_guard lock(_pendingMutex);
        Pending taken = std::move(_pending);
        _pending = {};
        taken.previousCompletion = _frameTimeline.At(_frameNumber - 1);
        taken.completion = _frameTimeline.At(_frameNumber++);
        return taken;
    }


}
