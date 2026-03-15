//
// Created by radue on 2/18/2026.
//

#include "../backends/open_gl/buffer.h"
#include "../backends/vulkan/buffer.h"

#include <buffer.h>
#include <window.h>
#include <framebuffer.h>
#include <surface.h>

namespace gfx
{
    std::unique_ptr<Buffer> Buffer::Builder::build() const
    {
        switch (Context::Window().getAPI()) {
        case API::eOpenGL:
            return std::make_unique<ogl::Buffer>(*this);
        case API::eVulkan:
            return std::make_unique<vk::Buffer>(*this);
        default:
            throw std::runtime_error("Unknown graphics API!");
        }
    }

    Buffer::Buffer(const Builder& createInfo) :
        _isPerFrame(createInfo.isPerFrame),
        _size(createInfo.size),
        _usage(createInfo.usage),
        _memoryProperties(createInfo.memoryProperties),
        _layout(createInfo.layout) {}
}
