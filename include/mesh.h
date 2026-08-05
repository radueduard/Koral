//
// Created by radue on 2/23/2026.
//

#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <vector>

#include "structs.h"
#include "buffer.h"
#include "context.h"
#include "vertexLayout.h"
#include "api.h"

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
     * attribute sits at which offset, and what each one *is* — is a @ref VertexLayout the mesh
     * carries but does not invent: writing vertex formats is the mesh module's business, and this
     * is the description the engine reads them through.
     */
    class KORAL_API Mesh
    {
    public:
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
            for (const auto& vertexBuffer : _vertexBuffers)
            {
                // Convert from the owning Resource (lifetime-tracked) rather than from a
                // raw Buffer&: the latter takes the `unsafe` ResourceRef ctor with an empty
                // lifetime stamp, which the descriptor layer rejects as an invalid buffer.
                vertexBuffers.emplace_back(vertexBuffer);
            }
            return vertexBuffers;
        }

        /** @brief The index buffer, or nullopt if the mesh is drawn non-indexed. */
        [[nodiscard]] std::optional<kor::ResourceRef<const Buffer>> getIndexBuffer() const {
            if (!_indexBuffer.has_value())
                return std::nullopt;
            return _indexBuffer;
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

        glm::u64 _vertexCount{};
        std::vector<kor::Resource<Buffer>> _vertexBuffers = {};

        std::optional<glm::u32> _indexCount = std::nullopt;
        std::optional<kor::Resource<Buffer>> _indexBuffer = std::nullopt;
        std::optional<ChannelType> _indexType = std::nullopt;

        VertexLayout _vertexLayout = {};
        std::optional<VertexInputAttributeDescription> _positionAttribute = std::nullopt;

    public:
        /**
         * Creates a device-local buffer and copies the contents of `data` into it.
         * `T` is deduced from the span, so the const element type does not need to be spelled out.
         *
         * @param data The vertices or indices to upload. Copied during the call.
         * @param usage What the buffer is for — Buffer::Usage::eVertex or eIndex. The transfer and
         *        storage usages a mesh needs are added on top, as is acceleration-structure input
         *        when the device supports ray tracing.
         * @return The buffer, ready to hand to a mesh builder.
         */
        template<typename T>
        static kor::Resource<Buffer> makeBuffer(std::span<const T> data, Flags<Buffer::Usage> usage)
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
                .setDataView(data)
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
