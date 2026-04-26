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
    gfx::Resource<Buffer> Buffer::RawBuilder::build() const
    {
        switch (Context::Window().getAPI()) {
            case API::eOpenGL:
            return gfx::MakeResource<ogl::Buffer>(*this);
        case API::eVulkan:
            return gfx::MakeResource<vk::Buffer>(*this);
        default:
            throw std::runtime_error("Unknown graphics API!");
        }
    }

    Buffer::Buffer(const RawBuilder& createInfo) :
        _isPerFrame(createInfo.isPerFrame),
        _size(createInfo.size),
        _usage(createInfo.usage),
        _type(createInfo.type) {}
}
