//
// Created by radue on 5/11/2026.
//

#pragma once

#include <array>
#include <cassert>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>

#include <glm/glm.hpp>

#include "tlsfAllocator.h"
#include "mesh.h"
#include "meshLayout.h"
#include "buffer.h"
#include "structs.h"

namespace kor
{
    template<typename... Streams>
    class MeshHeap : public Mesh
    {
    public:
        struct Identifier
        {
            glm::u64 offset;
            glm::u64 size;
        };

        // Owning handle to a suballocation inside a MeshHeap. Frees the space back
        // to the heap on destruction, so dropping it never leaks heap capacity.
        // Move-only: copying would let two handles free the same range. The handle
        // does not keep the heap alive — it must not outlive the heap it came from.
        struct Allocation
        {
            Identifier                vertexIdentifier {};
            std::optional<Identifier> indexIdentifier  = std::nullopt;

            Allocation() = default;
            ~Allocation() { reset(); }

            Allocation(const Allocation&)            = delete;
            Allocation& operator=(const Allocation&) = delete;

            Allocation(Allocation&& other) noexcept
                : vertexIdentifier(other.vertexIdentifier)
                , indexIdentifier(other.indexIdentifier)
                , _heap(other._heap)
            {
                other._heap = nullptr;
            }

            Allocation& operator=(Allocation&& other) noexcept
            {
                if (this != &other)
                {
                    reset();
                    vertexIdentifier = other.vertexIdentifier;
                    indexIdentifier  = other.indexIdentifier;
                    _heap            = other._heap;
                    other._heap      = nullptr;
                }
                return *this;
            }

            // Frees the suballocation back to its heap, leaving this handle empty.
            void reset()
            {
                if (_heap)
                {
                    _heap->free(vertexIdentifier, indexIdentifier);
                    _heap = nullptr;
                }
            }

            // True while this handle still owns space in a heap.
            [[nodiscard]] explicit operator bool() const { return _heap != nullptr; }

        private:
            friend class MeshHeap;
            const MeshHeap* _heap = nullptr;
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

            // Request eAccelerationStructureInput (implies device address) so a heap
            // suballocation can directly back a ray-tracing BLAS; the empty-buffer
            // makeBuffer overload otherwise omits it.
            _vertexBuffers.reserve(sizeof...(Streams));
            (_vertexBuffers.emplace_back(
                makeBuffer<Streams>(vertexCapacity,
                    Flags<Buffer::Usage>(Buffer::Usage::eVertex) | Buffer::Usage::eAccelerationStructureInput)), ...);

            if (indexCapacity.has_value()) {
                _indexBuffer = makeBuffer<glm::u32>(*indexCapacity,
                    Flags<Buffer::Usage>(Buffer::Usage::eIndex) | Buffer::Usage::eAccelerationStructureInput);
                _indexType   = ChannelType::eUInt;
            }

            // Record which stream/offset/format carries the position so heap
            // suballocations can be used to build ray-tracing acceleration structures.
            _positionAttribute = FindPositionAttribute<Streams...>();
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

            Allocation allocation;
            allocation.vertexIdentifier = Identifier{ vertAlloc->offset, vertAlloc->size };
            allocation.indexIdentifier  = indexId;
            allocation._heap            = this;
            return allocation;
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

        [[nodiscard]] glm::u64 VertexCapacity() const { return _vertexAllocator.Capacity(); }
        [[nodiscard]] glm::u64 IndexCapacity()  const
        {
            return _indexAllocator ? _indexAllocator->Capacity() : 0;
        }

    private:
        // Returns a suballocation's space to the heap. Const because the allocators
        // are mutable; called by Allocation's destructor through its heap pointer.
        void free(const Identifier& vertexId, const std::optional<Identifier>& indexId) const
        {
            _vertexAllocator.Free({ vertexId.offset, vertexId.size });
            if (indexId.has_value() && _indexAllocator)
                _indexAllocator->Free({ indexId->offset, indexId->size });
        }

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
