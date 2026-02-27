//
// Created by radue on 2/18/2026.
//

#include "buffer.h"
#include "impl/open_gl/resources/buffer.h"

#include "io/window.h"

namespace gfx
{
    std::unique_ptr<Buffer> Buffer::Create(const CreateInfo& createInfo)
    {
        switch (Context::Window().getAPI()) {
        case API::OpenGL:
            return std::make_unique<ogl::Buffer>(createInfo);
        case API::Vulkan:
            throw std::runtime_error("Vulkan is not supported yet!");
        default:
            throw std::runtime_error("Unknown graphics API!");
        }
    }

    Buffer::Buffer(const CreateInfo& createInfo) :
        size(createInfo.size),
        usage(createInfo.usage),
        memoryProperties(createInfo.memoryProperties),
        layout(createInfo.layout) {}
}
