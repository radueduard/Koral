//
// Created by radue on 2/18/2026.
//

#include "../backends/open_gl/buffer.h"
#include "../backends/vulkan/buffer.h"

#include <buffer.h>
#include <window.h>
#include <framebuffer.h>
#include <surface.h>

namespace kor
{
    kor::Result<std::unique_ptr<Buffer>> Buffer::RawBuilder::create() const
    {
        beginAttempt();

        if (_usage & Usage::eUniform && _size > 0xFFFF) {
            addError(ErrorCode::eUniformBufferTooLarge,
                     std::format("Uniform buffer size of {} bytes exceeds the maximum allowed size of 65536 bytes!", _size));
        }

        // Logs all warnings/errors with call sites; returns the first error as a kor::Error.
        if (auto v = validate(); !v) return std::unexpected(v.error());

        const auto api = Context::activeAPI();
        if (api != API::eOpenGL && api != API::eVulkan) {
            return fail(ErrorCode::eUnknownApi, "Unknown graphics API!");
        }

        // Backend allocation/creation may throw; convert any escape into a kor::Error.
        return guard(ErrorCode::eBackend, [&]() -> std::unique_ptr<Buffer> {
            return (api == API::eVulkan)
                ? kor::MakeBackendPtr<Buffer, vk::Buffer>(*this)
                : kor::MakeBackendPtr<Buffer, ogl::Buffer>(*this);
        });
    }

    kor::Resource<Buffer> Buffer::RawBuilder::build() const
    {
        auto buffer = materialize<Buffer>(*this, "Buffer");
        Context::Repository().addRef(ResourceRef<const Buffer>(buffer));
        return buffer;
    }

    Buffer::Buffer(const RawBuilder& createInfo) :
        _isPerFrame(createInfo._isPerFrame),
        _size(createInfo._size),
        _usage(createInfo._usage),
        _type(createInfo._type) {}
}
