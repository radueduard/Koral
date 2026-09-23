//
// Created by eduard on 11.03.2026.
//

#include <memory>

#include <log.h>

#include <scheduler.h>
#include <commandBuffer.h>
#include <window.h>
#include <framebuffer.h>
#include <surface.h>

#include "../backends/open_gl/scheduler.h"
#include "../backends/vulkan/scheduler.h"

namespace kor
{
    Frame::Frame(const glm::u32 imageIndex) : _imageIndex(imageIndex)
    {
        _commandBuffer = CommandBuffer::Create(CommandBuffer::Usage::eGraphics);
    }

    std::unique_ptr<Scheduler> Scheduler::Builder::build() const
    {
        switch (Context::activeAPI()) {
        case API::eOpenGL:
            return std::make_unique<ogl::Scheduler>(*this);
        case API::eVulkan:
            return std::make_unique<vk::Scheduler>(*this);
        default:
            throw std::runtime_error("Unknown graphics API!");
        }
    }

    Scheduler::Scheduler(const Builder& createInfo) :
        _imageCount(createInfo.imageCount) {}

    Token Scheduler::Execute(std::unique_ptr<CommandBuffer> commandBuffer, const Placement placement)
    {
        if (!commandBuffer) {
            log::error("[scheduler] Execute was handed no command buffer");
            return {};
        }
        if (!commandBuffer->isRecording()) {
            log::error("[scheduler] Execute needs a command buffer that has been begun and not ended: "
                       "the frame ends it, so its barriers are resolved in the order it runs. "
                       "Drop the End() call before handing it over.");
            return {};
        }
        std::lock_guard lock(_pendingMutex);
        (placement == Placement::eBeforeFrame ? _pending.before : _pending.after).push_back(std::move(commandBuffer));
        return _frameTimeline.at(_frameNumber);
    }

    void Scheduler::WaitFor(const Token& token)
    {
        if (token.ready()) return;
        std::lock_guard lock(_pendingMutex);
        _pending.waits.push_back(token);
    }

    Token Scheduler::frameCompletion()
    {
        std::lock_guard lock(_pendingMutex);
        return _frameTimeline.at(_frameNumber);
    }

    Scheduler::Pending Scheduler::takePending()
    {
        std::lock_guard lock(_pendingMutex);
        Pending taken = std::move(_pending);
        _pending = {};
        taken.completion = _frameTimeline.at(_frameNumber++);
        return taken;
    }


}
