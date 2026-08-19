//
// Created by radue on 2/23/2026.
//

#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <source_location>
#include <span>
#include <vector>

#include "structs.h"
#include "buffer.h"
#include "builder.h"
#include "context.h"
#include "vertexLayout.h"
#include "api.h"
#include "dataRange.h"

namespace kor
{
    /**
     * @brief Geometry ready to draw: the vertex buffers, and optionally an index buffer.
     *
     * A mesh is the pair of "what to draw" that a draw call needs. Bind one and the counts follow
     * from it, so CommandBuffer::DrawMesh takes it as the single argument:
     *
     * @code
     * commandBuffer.DrawMesh(mesh, 1, 0);
     * @endcode
     *
     * The buffers themselves are ordinary device-local Buffers, and makeBuffer() is the shorthand
     * for creating them with the usages a mesh needs. How the vertex data is laid out — which
     * attribute sits at which offset, and what each one *is* — is a @ref VertexLayout: buffers plus
     * a layout describing them is all a mesh is, and @ref Mesh::Builder assembles one from exactly
     * that:
     *
     * @code
     * auto mesh = kor::Mesh::Builder()
     *     .setVertexBuffer(0, vertexBuffer)
     *     .setIndexBuffer(indexBuffer)
     *     .setVertexLayout(kor::VertexLayout {
     *         .bindings = {
     *             kor::VertexInputBindingDescription(0, sizeof(Vertex)),
     *         },
     *         .attributes = {
     *             kor::VertexLayout::Attribute("POSITION", "vertex", 0, offsetof(Vertex, position), kor::ChannelType::eFloat, 3),
     *             kor::VertexLayout::Attribute("COLOR", "vertex", 0, offsetof(Vertex, color), kor::ChannelType::eFloat, 3),
     *         },
     *     })
     *     .build();
     * @endcode
     *
     * The semantics are what let the shader ask for an attribute by name. A shader written against
     * fixed locations wants none of that, and says so:
     *
     * @code
     *         .attributes = {
     *             kor::VertexLayout::Attribute::AtLocation(0, 0, offsetof(Vertex, position), kor::ChannelType::eFloat, 3),
     *             kor::VertexLayout::Attribute::AtLocation(1, 0, offsetof(Vertex, color),    kor::ChannelType::eFloat, 3),
     *         },
     * @endcode
     *
     * Writing the layout out by hand like this is the low-level route, and the one everything else
     * is built on. Describing a vertex *type* once and having the strides, offsets and channel
     * formats computed from it is the mesh module's business (`kmesh::ParamMesh`); it produces the
     * same @ref VertexLayout this takes.
     */
    class KORAL_API Mesh
    {
    public:
        /**
         * @brief Collects the buffers a mesh is made of and the layout describing them.
         *
         * One vertex buffer per binding the layout declares, optionally an index buffer, and the
         * layout itself. The order the three are set in does not matter: nothing is checked until
         * build(), which is what lets the layout be named last.
         *
         * The vertex count is derived — each binding's buffer size divided by that binding's stride
         * — and every binding must agree on it. The index count follows from the index buffer's
         * size and the width of one index the same way.
         */
        struct KORAL_API Builder : ::Builder
        {
            /**
             * @brief Sets the vertex buffer feeding one binding of the layout.
             * @param binding Which binding of the vertex layout this buffer feeds.
             * @param vertexBuffer The data. It must have been created with Buffer::Usage::eVertex,
             *        and its size divided by the binding's stride gives the vertex count.
             *
             * The mesh only *refers* to the buffer, exactly as a framebuffer refers to its
             * attachments: whoever owns it must keep it alive for as long as the mesh is drawn.
             * Hand over an rvalue instead — `std::move(buffer)`, or a makeBuffer() call written in
             * place — and the mesh takes ownership, which is what a mesh built from data nothing
             * else refers to wants.
             */
            Builder& setVertexBuffer(glm::u32 binding, ResourceRef<Buffer> vertexBuffer);

            /** @brief Sets the vertex buffer for one binding and takes ownership of it. */
            Builder& setVertexBuffer(glm::u32 binding, Resource<Buffer>&& vertexBuffer);

            /**
             * @brief Gives the mesh an index buffer, making it drawable with DrawIndexed.
             * @param indexBuffer The indices. Must have been created with Buffer::Usage::eIndex.
             * @param indexType The width of one index — eUByte, eUShort or eUInt. The count follows
             *        from the buffer's size. A signed type of the same width is read as its
             *        unsigned counterpart, since an index is never negative.
             *
             * Referenced rather than owned, on the same terms as setVertexBuffer.
             */
            Builder& setIndexBuffer(ResourceRef<Buffer> indexBuffer, ChannelType indexType = ChannelType::eUInt);

            /** @brief Gives the mesh an index buffer and takes ownership of it. */
            Builder& setIndexBuffer(Resource<Buffer>&& indexBuffer, ChannelType indexType = ChannelType::eUInt);

            /**
             * @brief Describes what the vertex buffers hold: the bindings and their strides, and
             *        what each attribute is.
             *
             * Also decides which attribute a ray-tracing build reads positions from.
             * @see VertexLayout
             */
            Builder& setVertexLayout(VertexLayout layout);

            /** @brief One build attempt. Internal: prefer build(). */
            [[nodiscard]] Result<std::unique_ptr<Mesh>> create() const;

            /**
             * @brief Creates the mesh.
             * @return It as a Resource; poisoned rather than thrown when a binding has no buffer,
             *         the buffers disagree on the vertex count, or a buffer was created without the
             *         usage it is being put to.
             */
            [[nodiscard]] Resource<Mesh> build(std::source_location where = std::source_location::current()) const;

        protected:
            std::vector<ResourceRef<Buffer>> _vertexBuffers {};      ///< Indexed by binding, not by order of setting.
            std::optional<ResourceRef<Buffer>> _indexBuffer = std::nullopt;
            ChannelType _indexType = ChannelType::eUInt;
            VertexLayout _vertexLayout {};

            // Buffers handed over as rvalues. Held by shared_ptr rather than by value because
            // create() is const and may run more than once: every mesh built from this builder
            // shares the same buffers, rather than the first one taking them and the rest getting
            // nothing.
            std::vector<std::shared_ptr<Resource<Buffer>>> _ownedBuffers {};

            /** @brief Validates the configuration and fills @p mesh with it. */
            [[nodiscard]] VoidResult populate(Mesh& mesh) const;
        };

        Mesh() = default;
        virtual ~Mesh() = default;

        /** @brief How many vertices the mesh holds, taken from the vertex buffers' size and stride. */
        [[nodiscard]] glm::u64 getVertexCount() const { return _vertexCount; }

        /** @brief Whether the mesh has an index buffer, and so whether it can be drawn indexed. */
        [[nodiscard]] bool hasIndexBuffer() const { return _indexBuffer.has_value(); }

        /** @brief How many indices the mesh holds, or nullopt if it has no index buffer. */
        [[nodiscard]] std::optional<glm::u32> getIndexCount() const { return _indexCount; }

        /** @brief The width of one index — typically ChannelType::eUShort or eUInt — or nullopt if there is no index buffer. */
        [[nodiscard]] std::optional<ChannelType> getIndexType() const { return _indexType; }

        /**
         * @brief The vertex buffers, in binding order.
         * @return One reference per binding the vertex format declares. Index 0 is binding 0.
         */
        [[nodiscard]] std::vector<kor::ResourceRef<const Buffer>> getVertexBuffers() const
        {
            std::vector<kor::ResourceRef<const Buffer>> vertexBuffers;
            vertexBuffers.reserve(_vertexBuffers.size());
            for (const auto& vertexBuffer : _vertexBuffers)
            {
                // Const-qualify the tracked ref rather than taking one off a raw Buffer&: the
                // latter takes the `unsafe` ResourceRef ctor with an empty lifetime stamp, which
                // the descriptor layer rejects as an invalid buffer.
                vertexBuffers.emplace_back(vertexBuffer);
            }
            return vertexBuffers;
        }

        /** @brief The index buffer, or nullopt if the mesh is drawn non-indexed. */
        [[nodiscard]] std::optional<kor::ResourceRef<const Buffer>> getIndexBuffer() const {
            if (!_indexBuffer.has_value())
                return std::nullopt;
            return kor::ResourceRef<const Buffer>(*_indexBuffer);
        }

        /**
         * @brief How this mesh's vertices are laid out, and what each attribute is.
         *
         * The description a pipeline is matched against: what the mesh holds, by name, with no
         * shader locations in it. @see VertexLayout
         */
        [[nodiscard]] const VertexLayout& getVertexLayout() const { return _vertexLayout; }

        /**
         * @brief The vertex attribute carrying the vertex position, if the mesh declares one.
         * Used as the position source when building a ray-tracing acceleration structure.
         *
         * @return The attribute — its binding, offset and channel format — or nullopt if the mesh
         *         has no vertex layout at all, in which case it cannot be ray traced. A layout that
         *         does not say which attribute is the position is taken at its first.
         *         @see VertexLayout::positionAttribute
         */
        [[nodiscard]] const std::optional<VertexInputAttributeDescription>& getPositionAttribute() const { return _positionAttribute; }

    protected:
        /**
         * @brief Adopts the vertex layout, and with it the position the ray tracer reads.
         *
         * What a vertex format calls once it knows its own shape. The position attribute is derived
         * here rather than passed in, so the two can never disagree.
         */
        void setVertexLayout(VertexLayout layout)
        {
            _vertexLayout = std::move(layout);
            _positionAttribute = _vertexLayout.position();
        }

        /**
         * @brief Takes ownership of @p buffer and appends it as the next binding's vertex buffer.
         * @return The reference the mesh will bind, so the caller can go on writing through it.
         *
         * For a mesh that creates its own storage — one built from vertex data, or a heap that
         * suballocates out of buffers it owns. A mesh built from buffers someone else owns holds
         * plain references instead. @see Builder::setVertexBuffer
         */
        kor::ResourceRef<Buffer> adoptVertexBuffer(kor::Resource<Buffer> buffer)
        {
            auto owned = std::make_shared<kor::Resource<Buffer>>(std::move(buffer));
            auto ref = kor::ResourceRef<Buffer>(*owned);
            _ownedBuffers.push_back(std::move(owned));
            _vertexBuffers.push_back(ref);
            return ref;
        }

        /** @brief Takes ownership of @p buffer and makes it the index buffer. @see adoptVertexBuffer */
        kor::ResourceRef<Buffer> adoptIndexBuffer(kor::Resource<Buffer> buffer, const ChannelType indexType)
        {
            auto owned = std::make_shared<kor::Resource<Buffer>>(std::move(buffer));
            auto ref = kor::ResourceRef<Buffer>(*owned);
            _ownedBuffers.push_back(std::move(owned));
            _indexBuffer = ref;
            _indexType = indexType;
            return ref;
        }

        glm::u64 _vertexCount{};

        /// What the mesh binds, one per binding of its layout. References, so that several meshes
        /// can be carved out of one set of buffers; _ownedBuffers holds the ones the mesh itself
        /// keeps alive.
        std::vector<kor::ResourceRef<Buffer>> _vertexBuffers = {};

        std::optional<glm::u32> _indexCount = std::nullopt;
        std::optional<kor::ResourceRef<Buffer>> _indexBuffer = std::nullopt;
        std::optional<ChannelType> _indexType = std::nullopt;

        /// The buffers this mesh owns, keeping them alive for as long as it is drawn. Shared
        /// because a builder can hand the same buffers to more than one mesh.
        std::vector<std::shared_ptr<kor::Resource<Buffer>>> _ownedBuffers = {};

        VertexLayout _vertexLayout = {};
        std::optional<VertexInputAttributeDescription> _positionAttribute = std::nullopt;

    public:
        /**
         * Creates a device-local buffer and copies the contents of `data` into it.
         * `T` is deduced from the range's element type, so nothing has to be spelled out.
         *
         * @param data The vertices or indices to upload, as any range — a vector, an array, a
         *        span. Copied during the call.
         * @param usage What the buffer is for — Buffer::Usage::eVertex or eIndex. The transfer and
         *        storage usages a mesh needs are added on top, as is acceleration-structure input
         *        when the device supports ray tracing.
         * @return The buffer, ready to hand to a mesh builder.
         */
        template<typename R, typename T = std::remove_cvref_t<std::ranges::range_value_t<R>>>
            requires RangeOf<R, T>
        static kor::Resource<Buffer> makeBuffer(R&& data, Flags<Buffer::Usage> usage)
        {
            // Keep final buffers transfer-capable as requested, and usable as ray-tracing
            // acceleration structure build input (implies device address) -- but only when the
            // device actually supports ray tracing (see Context::SupportsRayTracing): not every
            // GPU does, and requesting a buffer usage tied to an extension that was never enabled
            // is itself a Vulkan validation error.
            auto finalUsage = usage
                | Buffer::Usage::eTransferDst
                | Buffer::Usage::eTransferSrc
                | Buffer::Usage::eStorage;
            if (kor::Context::SupportsRayTracing()) {
                finalUsage |= Buffer::Usage::eAccelerationStructureInput;
            }

            return Buffer::Builder<T>()
                .setData(std::forward<R>(data))
                .setUsage(finalUsage)
                .setType(Buffer::Type::eDeviceLocal)
                .build();
        }

        /**
         * Creates an empty device-local buffer sized for `instanceCount` elements of `T`.
         * The caller is expected to fill it later. `T` must be specified explicitly.
         *
         * @param instanceCount How many elements of T the buffer must hold.
         * @param usage What the buffer is for; the transfer and storage usages are added on top.
         * @return The buffer, its contents undefined until something writes them — a compute shader
         *         generating geometry, for instance.
         */
        template<typename T>
        static kor::Resource<Buffer> makeBuffer(glm::u64 instanceCount, Flags<Buffer::Usage> usage)
        {
            const auto finalUsage = usage
                | Buffer::Usage::eTransferDst
                | Buffer::Usage::eTransferSrc
                | Buffer::Usage::eStorage;

            return Buffer::Builder<T>()
                .setInstanceCount(static_cast<glm::i64>(instanceCount))
                .setUsage(finalUsage)
                .setType(Buffer::Type::eDeviceLocal)
                .build();
        }
    };
}
