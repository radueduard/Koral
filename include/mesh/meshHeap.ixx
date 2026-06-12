//
// Created by radue on 5/11/2026.
//

module;

#include "tlsfAllocator.h"
#include <glm/glm.hpp>

export module gfx.mesh.heap;
import std;
import gfx.mesh.base;
import gfx.buffer;
import gfx.structs;

namespace gfx
{
    export template<typename... Streams>
    class MeshHeap : public Mesh
    {
    public:
        struct Identifier
        {
            glm::u64 offset;
            glm::u64 size;
        };

        struct Allocation
        {
            Identifier                vertexIdentifier;
            std::optional<Identifier> indexIdentifier;
        };

        // ---------------------------------------------------------------------
        MeshHeap(const glm::u64 vertexCapacity, const std::optional<glm::u64> indexCapacity)
            : _vertexAllocator(vertexCapacity)
            , _indexAllocator(indexCapacity
                ? std::make_optional<TLSFAllocator>(*indexCapacity)
                : std::nullopt)
        {
            _vertexCount = static_cast<glm::u32>(vertexCapacity);
            if (indexCapacity.has_value())
                _indexCount = static_cast<glm::u32>(*indexCapacity);

            _vertexBuffers.reserve(sizeof...(Streams));
            (_vertexBuffers.emplace_back(
                makeBuffer<Streams>(vertexCapacity, Buffer::Usage::eVertex)), ...);

            if (indexCapacity.has_value()) {
                _indexBuffer = makeBuffer<glm::u32>(*indexCapacity, Buffer::Usage::eIndex);
                _indexType   = ChannelType::eUInt;
            }
        }

        ~MeshHeap() override = default;
        MeshHeap(const MeshHeap&)            = delete;
        MeshHeap& operator=(const MeshHeap&) = delete;

        // ---------------------------------------------------------------------
        // AllocateMesh – metadata only, no upload.
        // ---------------------------------------------------------------------
        [[nodiscard]] std::optional<Allocation> AllocateMesh(
            const glm::u64 numVertices,
            const std::optional<glm::u64> numIndices = std::nullopt) const
        {
            auto vertAlloc = _vertexAllocator.Allocate(numVertices);
            if (!vertAlloc)
                return std::nullopt;

            std::optional<Identifier> indexId;
            if (numIndices.has_value())
            {
                if (!_indexAllocator)
                {
                    _vertexAllocator.Free(*vertAlloc);
                    return std::nullopt;
                }
                auto idxAlloc = _indexAllocator->Allocate(*numIndices);
                if (!idxAlloc)
                {
                    _vertexAllocator.Free(*vertAlloc);
                    return std::nullopt;
                }
                indexId = Identifier{ idxAlloc->offset, idxAlloc->size };
            }

            return Allocation{
                Identifier{ vertAlloc->offset, vertAlloc->size },
                indexId
            };
        }

        // ---------------------------------------------------------------------
        // Create – allocates + uploads data, returns the Allocation handle.
        // ---------------------------------------------------------------------
        [[nodiscard]] std::optional<Allocation> Create(
            std::span<const Streams>... streams,
            const std::optional<std::span<const glm::u32>> &indices = std::nullopt) const
        {
            // All streams must agree on vertex count
            const std::array<glm::u64, sizeof...(Streams)> counts = {
                static_cast<glm::u64>(streams.size())...
            };
            const glm::u64 numVertices = counts[0];
            for (const auto c : counts)
            {
                if (c != numVertices)
                    throw std::invalid_argument(
                        "MeshHeap::Create: all vertex streams must have the same element count");
            }

            const std::optional<glm::u64> numIndices =
                indices.has_value()
                    ? std::make_optional(indices->size())
                    : std::nullopt;

            auto alloc = AllocateMesh(numVertices, numIndices);
            if (!alloc)
                return std::nullopt;

            // Upload each vertex stream into its respective buffer
            uploadStreams(alloc->vertexIdentifier.offset, streams...,
                          std::index_sequence_for<Streams...>{});

            // Upload indices
            if (indices.has_value())
            {
                assert(alloc->indexIdentifier.has_value());
                (*_indexBuffer)->Write(
                    std::span(*indices),
                    alloc->indexIdentifier->offset);
            }

            return alloc;
        }

        // ---------------------------------------------------------------------
        // FreeMesh
        // ---------------------------------------------------------------------
        void FreeMesh(const Allocation& alloc)
        {
            _vertexAllocator.Free({ alloc.vertexIdentifier.offset, alloc.vertexIdentifier.size });
            if (alloc.indexIdentifier.has_value() && _indexAllocator)
                _indexAllocator->Free({ alloc.indexIdentifier->offset, alloc.indexIdentifier->size });
        }

        [[nodiscard]] glm::u64 VertexCapacity() const { return _vertexAllocator.Capacity(); }
        [[nodiscard]] glm::u64 IndexCapacity()  const
        {
            return _indexAllocator ? _indexAllocator->Capacity() : 0;
        }

    private:
        mutable TLSFAllocator                _vertexAllocator;
        mutable std::optional<TLSFAllocator> _indexAllocator;

        // Upload each span into _vertexBuffers[I] at elementOffset
        template<std::size_t... I>
        void uploadStreams(
            const glm::u64 elementOffset,
            std::span<const Streams>... streams,
            std::index_sequence<I...>) const
        {
            (_vertexBuffers[I]->Write(streams, elementOffset), ...);
        }
    };
}
