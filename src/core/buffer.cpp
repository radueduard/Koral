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
        gfx::Resource<Buffer> buffer = nullptr;
        switch (Context::Window().getAPI()) {
            case API::eOpenGL:
            buffer = gfx::MakeResource<ogl::Buffer>(*this);
            break;
        case API::eVulkan:
            buffer = gfx::MakeResource<vk::Buffer>(*this);
            break;
        default:
            throw std::runtime_error("Unknown graphics API!");
        }
        Context::Repository().addRef(ResourceRef(buffer));
        return buffer;
    }

    Buffer::Buffer(const RawBuilder& createInfo) :
        _isPerFrame(createInfo.isPerFrame),
        _size(createInfo.size),
        _usage(createInfo.usage),
        _type(createInfo.type) {}
}
