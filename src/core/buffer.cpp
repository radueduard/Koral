//
// Created by radue on 2/18/2026.
//

#include "buffer.h"
#include "impl/open_gl/buffer.h"

#include "io/window.h"

namespace gfx
{
    std::unique_ptr<Buffer> Buffer::Builder::build() const
    {
        switch (Context::Window().getAPI()) {
        case API::OpenGL:
            return std::make_unique<ogl::Buffer>(*this);
        case API::Vulkan:
            throw std::runtime_error("Vulkan is not supported yet!");
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
