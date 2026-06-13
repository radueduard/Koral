//
// Created by radue on 2/18/2026.
//

module gfx;
import :buffer;

import std;
import :ogl_buffer;
import :vk_buffer;

namespace gfx
{
    Resource<Buffer> Buffer::RawBuilder::build() const
    {
        switch (Context::GetWindow().getAPI()) {
            case API::eOpenGL:
            return Resource<gfx::Buffer>(std::make_unique<ogl::Buffer>(*this));
            break;
        case API::eVulkan:
            return Resource<gfx::Buffer>(std::make_unique<vk::Buffer>(*this));
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
