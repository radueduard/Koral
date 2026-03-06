//
// Created by radue on 2/18/2026.
//

#include "buffer.h"
#include "impl/open_gl/buffer.h"
#include "impl/vulkan/buffer.h"

#include "io/window.h"

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
        size(createInfo.size),
        usage(createInfo.usage),
        memoryProperties(createInfo.memoryProperties),
        layout(createInfo.layout) {}
}
