//
// Created by radue on 12.09.2026.
//

#include "../backends/open_gl/bufferView.h"
#include "../backends/vulkan/bufferView.h"

#include <bufferView.h>
#include <buffer.h>
#include <context.h>

namespace kor
{
    namespace
    {
        /** @brief Bytes one texel of @p format occupies. */
        glm::u64 texelSize(const Image::Format format)
        {
            return Image::sizeOfRegion(format, glm::uvec3{ 1, 1, 1 });
        }

        /**
         * @brief The builder's range, with 0 meaning "the rest of the buffer".
         *
         * Shared by create()'s validation and the constructor so the value the view reports is the
         * value that was checked. Only meaningful once the buffer is known usable.
         */
        glm::i64 resolveRange(const BufferView::Builder& builder)
        {
            if (builder.range != 0) return builder.range;
            return static_cast<glm::i64>(builder.buffer->size()) - builder.offset;
        }
    }

    BufferView::Builder::Builder(kor::ResourceRef<const Buffer> buffer) : buffer(std::move(buffer)) {}

    kor::Result<std::unique_ptr<BufferView>> BufferView::Builder::create() const
    {
        beginAttempt();
        adopt(buffer, "buffer view's buffer");

        if (!format) {
            addError(ErrorCode::eInvalidArgument,
                     "A buffer view has no format. Formatted access is the whole of what a view "
                     "adds over binding the buffer itself, so setFormat() is not optional.");
        }

        // Everything below reads through the buffer, so stop here if adopt() found it unusable.
        if (auto v = validate(); !v) return std::unexpected(v.error());

        // The formatted read is a different hardware path from a storage-buffer read, and it has to
        // be asked for when the buffer is created — nothing here can add it after the fact. Name the
        // flag, because the driver's own complaint is about usage bits and does not.
        if (!(buffer->usage() & Buffer::Usage::eTexel)) {
            addError(ErrorCode::eInvalidArgument,
                     std::format("Buffer '{}' cannot be viewed as texels: it was created without "
                                 "Buffer::Usage::eTexel. Add .setUsage(Buffer::Usage::eTexel) where "
                                 "the buffer is built.",
                                 buffer.name().empty() ? "<unnamed>" : buffer.name()));
        }

        if (Image::isBlockCompressed(*format)) {
            addError(ErrorCode::eInvalidArgument,
                     "A buffer view cannot use a block-compressed format: a texel buffer is a flat "
                     "array of texels, and a compressed format has no single-texel size.");
        }
        if (isDepthStencilFormat(*format)) {
            addError(ErrorCode::eInvalidArgument,
                     "A buffer view cannot use a depth or stencil format; those exist only as images.");
        }

        if (offset < 0) {
            addError(ErrorCode::eInvalidArgument,
                     std::format("A buffer view's offset cannot be negative (got {}).", offset));
        }
        if (range < 0) {
            addError(ErrorCode::eInvalidArgument,
                     std::format("A buffer view's range cannot be negative (got {}). Leave it at 0 "
                                 "to cover the rest of the buffer.", range));
        }

        if (auto v = validate(); !v) return std::unexpected(v.error());

        const auto bufferSize = static_cast<glm::i64>(buffer->size());
        const glm::i64 resolved = resolveRange(*this);

        if (resolved <= 0 || offset + resolved > bufferSize) {
            addError(ErrorCode::eInvalidArgument,
                     std::format("A buffer view of {} bytes at offset {} does not fit in a buffer "
                                 "of {} bytes.", resolved, offset, bufferSize));
        }

        // Both are hard requirements of the API underneath: a texel buffer is addressed in texels,
        // so a view that starts or ends part-way through one has no meaning.
        const auto texel = static_cast<glm::i64>(texelSize(*format));
        if (texel > 0 && offset % texel != 0) {
            addError(ErrorCode::eInvalidArgument,
                     std::format("A buffer view's offset must be a whole number of texels: {} is "
                                 "not a multiple of the {}-byte texel size.", offset, texel));
        }
        if (texel > 0 && resolved % texel != 0) {
            addError(ErrorCode::eInvalidArgument,
                     std::format("A buffer view's range must be a whole number of texels: {} is "
                                 "not a multiple of the {}-byte texel size.", resolved, texel));
        }

        if (auto v = validate(); !v) return std::unexpected(v.error());

        const auto api = Context::activeAPI();
        if (api != API::eOpenGL && api != API::eVulkan)
            return fail(ErrorCode::eUnknownApi, "Unknown graphics API!");

        return guard(ErrorCode::eBackend, [&]() -> std::unique_ptr<BufferView> {
            return (api == API::eVulkan)
                ? kor::MakeBackendPtr<BufferView, vk::BufferView>(*this)
                : kor::MakeBackendPtr<BufferView, ogl::BufferView>(*this);
        });
    }

    kor::Resource<BufferView> BufferView::Builder::build(const std::source_location where) const
    {
        return materialize<BufferView>(*this, "BufferView", where);
    }

    BufferView::BufferView(const Builder& createInfo) :
        _buffer(createInfo.buffer),
        _format(*createInfo.format),
        _offset(createInfo.offset),
        _range(resolveRange(createInfo)),
        _texelCount(static_cast<glm::u64>(_range) / texelSize(_format)),
        _isPerFrame(_buffer->isPerFrame()) {}
}
