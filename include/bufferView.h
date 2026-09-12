//
// Created by radue on 12.09.2026.
//

#pragma once
#include <memory>
#include <optional>
#include <source_location>

#include <glm/fwd.hpp>

#include "api.h"

#include "builder.h"
#include "error.h"
#include "image.h"
#include "resource.h"

namespace kor
{
    class Buffer;

    /**
     * @brief A window onto a Buffer, read as an array of formatted texels.
     *
     * The counterpart to ImageView, for the other kind of storage. A buffer on its own is bytes,
     * and a shader that binds one has to declare a struct saying what those bytes are. A view says
     * it instead: give the bytes a kor::Image::Format and the shader fetches them the way it
     * fetches from a texture — `texelFetch` on a `samplerBuffer`, or an `imageBuffer` it can also
     * write — without describing the layout at all.
     *
     * @code
     * auto view = kor::BufferView::Builder(particles)
     *     .setFormat(kor::Image::Format::eRGBA32_SFLOAT)
     *     .build();
     *
     * auto set = kor::DescriptorSet::Builder(pipeline, 0).write("source", view).build();
     * @endcode
     *
     * The buffer must have been created with Buffer::Usage::eTexel — the formatted read is a
     * different hardware path from a storage-buffer read, and it has to be asked for up front. The
     * view keeps a reference to its buffer, so the buffer outlives it, and follows a per-frame
     * buffer into having one instance per frame in flight.
     *
     * As far as synchronisation is concerned this is still the buffer: a barrier names the buffer,
     * not the view, and the command buffer resolves a texel fetch against the bytes underneath.
     */
    class KORAL_API BufferView
    {
    public:
        /** @brief Describes the view to create over a buffer. */
        struct KORAL_API Builder : kor::Builder
        {
            kor::ResourceRef<const Buffer> buffer;          ///< The buffer being viewed.
            std::optional<Image::Format> format;            ///< How its bytes are read. Required.
            glm::i64 offset = 0;                            ///< First byte the view covers.
            /// Bytes covered, or 0 for the rest of the buffer. Zero rather than kor::WholeSize
            /// because this is signed and a zero-byte view means nothing anyway — unlike a count
            /// of elements, where 0 is a real answer and needs to be distinguishable.
            glm::i64 range = 0;

            /** @param buffer The buffer to view. It must outlive the view, and must carry Buffer::Usage::eTexel. */
            explicit Builder(kor::ResourceRef<const Buffer> buffer);

            /**
             * @brief Sets what one texel is — the whole point of the view, and not optional.
             *
             * Must be a format the device can read from a texel buffer, which rules out the
             * block-compressed ones and the depth/stencil ones.
             */
            Builder& setFormat(const Image::Format format)
            {
                this->format = format;
                return *this;
            }

            /** @brief Sets the first byte of the buffer the view covers. Must be a whole number of texels in. */
            Builder& setOffset(const glm::i64 offset)
            {
                this->offset = offset;
                return *this;
            }

            /**
             * @brief Sets how many bytes the view covers, starting at the offset.
             *
             * Left at 0 it is the rest of the buffer, which is what a view of a whole buffer wants.
             * Either way it has to come out a whole number of texels.
             */
            Builder& setRange(const glm::i64 range)
            {
                this->range = range;
                return *this;
            }

            /** @brief One build attempt. Internal: prefer build(). */
            [[nodiscard]] Result<std::unique_ptr<BufferView>> create() const;

            /** @brief Creates the view. Poisoned rather than thrown if the buffer cannot be read as texels. */
            [[nodiscard]] kor::Resource<BufferView> build(std::source_location where = std::source_location::current()) const;
        };

        virtual ~BufferView() = default;

        /** @brief The buffer this is a view of. Barriers and hazards name this, not the view. */
        [[nodiscard]] kor::ResourceRef<const Buffer> buffer() const { return _buffer; }
        /** @brief What one texel is. */
        [[nodiscard]] Image::Format format() const { return _format; }
        /** @brief First byte of the buffer the view covers. */
        [[nodiscard]] glm::i64 offset() const { return _offset; }
        /** @brief How many bytes the view covers. Resolved: a builder range of 0 reads back as the rest of the buffer. */
        [[nodiscard]] glm::i64 range() const { return _range; }
        /** @brief How many texels that range holds. */
        [[nodiscard]] glm::u64 texelCount() const { return _texelCount; }

        /** @brief Whether the view follows a per-frame buffer, and so has one instance per frame in flight. */
        [[nodiscard]] bool isPerFrame() const { return _isPerFrame; }

    protected:
        explicit BufferView(const Builder& createInfo);

        kor::ResourceRef<const Buffer> _buffer;
        Image::Format _format;
        glm::i64 _offset;
        glm::i64 _range;
        glm::u64 _texelCount;
        bool _isPerFrame = false;
    };
}
