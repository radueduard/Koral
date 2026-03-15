//
// Created by eduard on 11.03.2026.
//

#include <scheduler.h>
#include <comandBuffer.h>
#include <window.h>
#include <framebuffer.h>
#include <surface.h>

#include "../backends/open_gl/scheduler.h"
#include "../backends/vulkan/scheduler.h"

namespace gfx
{
    Frame::Frame(const glm::u32 imageIndex) : _imageIndex(imageIndex)
    {
        _commandBuffer = CommandBuffer::Create(CommandBuffer::Usage::eGraphics);
    }

    Scheduler* Scheduler::Builder::build() const
    {
        switch (Context::Window().getAPI()) {
        case API::eOpenGL:
            return new ogl::Scheduler(*this);
            case API::eVulkan:
            return new vk::Scheduler(*this);
        default:
            throw std::runtime_error("Unknown graphics API!");
        }
    }

    Scheduler::Scheduler(const Builder& createInfo) :
        _minImageCount(createInfo.minImageCount),
        _imageCount(createInfo.imageCount) {}


}
