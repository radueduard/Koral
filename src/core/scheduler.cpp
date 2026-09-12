//
// Created by eduard on 11.03.2026.
//

#include <memory>

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


}
