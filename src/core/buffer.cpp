//
// Created by radue on 2/18/2026.
//

module;

#include <stdexcept>

module gfx.buffer;
import vk.buffer;
import ogl.buffer;
import gfx.context;
import gfx.window;

import gfx;

namespace gfx
{
    gfx::Resource<Buffer> Buffer::RawBuilder::build() const
    {
        switch (Context::Window().getAPI()) {
            case API::eOpenGL:
            return gfx::Resource<gfx::Buffer>(std::make_unique<ogl::Buffer>(*this));
            break;
        case API::eVulkan:
            return gfx::Resource<gfx::Buffer>(std::make_unique<vk::Buffer>(*this));
            break;
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
