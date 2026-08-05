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

#include <context.h>
#include <mesh.h>
#include <buffer.h>
#include <structs.h>

#include "koralMesh.h"
#include "koralTlsfAllocator.h"

namespace kmesh
{
    /**
     * @brief Many meshes packed into one set of GPU buffers, suballocated in O(1).
     * @tparam Streams The vertex attribute types, one per stream — e.g. MeshHeap<glm::vec3, glm::vec2>
     *         for positions and texture coordinates in separate buffers.
     *
     * A heap allocates its buffers once and hands out ranges within them, so hundreds of meshes
     * share one vertex buffer and one index buffer. That is what makes them drawable without
     * rebinding between them: bind the heap once and draw each mesh as a range, with
     * CommandBuffer::DrawSubMesh or an indirect draw built on the GPU.
     *
     * @code
     * kmesh::MeshHeap<glm::vec3, glm::vec2> heap(vertexCapacity, indexCapacity);
     * auto allocation = heap.Create(positions, uvs, indices);
     * @endcode
     *
     * Space is managed by a TLSFAllocator, so allocating and freeing are constant-time and freed
     * ranges merge back together. The heap is itself a kor::Mesh, so it binds like one.
     */
    template<typename... Streams>
    class MeshHeap : public kor::Mesh
    {
    public:
        /** @brief A range within one of the heap's buffers, in elements. */
        struct Identifier
        {
            glm::u64 offset;    ///< First element of the range.
            glm::u64 size;      ///< How many elements it covers.
        };

        /**
         * @brief An owning handle to one mesh's space inside a heap.
         *
         * Frees the space back to the heap when it is destroyed, so dropping it never leaks heap
         * capacity. Move-only: copying would let two handles free the same range.
         *
         * @warning The handle does not keep the heap alive. It must not outlive the heap it came from.
         */
        struct Allocation
        {
            Identifier                vertexIdentifier {};                  ///< The vertex range, shared by every stream.
            std::optional<Identifier> indexIdentifier  = std::nullopt;      ///< The index range, if the mesh is indexed.

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

            /** @brief Frees the suballocation back to its heap, leaving this handle empty. */
            void reset()
            {
                if (_heap)
                {
                    _heap->free(vertexIdentifier, indexIdentifier);
                    _heap = nullptr;
                }
            }

            /** @brief Whether this handle still owns space in a heap. */
            [[nodiscard]] explicit operator bool() const { return _heap != nullptr; }

        private:
            friend class MeshHeap;
            const MeshHeap* _heap = nullptr;
        };

        /**
         * @brief Allocates the heap's buffers.
         * @param vertexCapacity How many vertices it can hold in total, across every mesh.
         * @param indexCapacity How many indices it can hold, or nullopt for a heap of non-indexed
         *        meshes.
         *
         * The buffers are device-local and sized once; a heap does not grow, so size it for the
         * scene it will hold.
         */
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
            // makeBuffer overload otherwise omits it. Only when the device actually supports ray
            // tracing, though — not every GPU does (see Context::SupportsRayTracing), and asking
            // for a buffer usage tied to an extension that was never enabled is itself a Vulkan
            // validation error, on every mesh buffer this heap ever allocates.
            const auto rtInputUsage = kor::Context::SupportsRayTracing()
                ? kor::Flags<kor::Buffer::Usage>(kor::Buffer::Usage::eAccelerationStructureInput)
                : kor::Flags<kor::Buffer::Usage>{};

            _vertexBuffers.reserve(sizeof...(Streams));
            (_vertexBuffers.emplace_back(
                makeBuffer<Streams>(vertexCapacity,
                    kor::Flags<kor::Buffer::Usage>(kor::Buffer::Usage::eVertex) | rtInputUsage)), ...);

            if (indexCapacity.has_value()) {
                _indexBuffer = makeBuffer<glm::u32>(*indexCapacity,
                    kor::Flags<kor::Buffer::Usage>(kor::Buffer::Usage::eIndex) | rtInputUsage);
                _indexType   = kor::ChannelType::eUInt;
            }

            // The heap's own vertex layout, which is what a pipeline drawing out of it is matched
            // against — and which says where the position sits, so a suballocation can back a
            // ray-tracing acceleration structure.
            setVertexLayout(MakeVertexLayout<Streams...>());
        }

        ~MeshHeap() override = default;
        MeshHeap(const MeshHeap&)            = delete;
        MeshHeap& operator=(const MeshHeap&) = delete;

        /**
         * @brief Reserves space for a mesh without uploading anything.
         * @param numVertices How many vertices to reserve, in every stream.
         * @param numIndices How many indices to reserve, or nullopt for a non-indexed mesh.
         * @return The handle, or nullopt when the heap has no room. Asking for indices from a heap
         *         built without an index buffer also returns nullopt, leaving nothing reserved.
         *
         * For geometry a shader will generate. Use Create() to reserve and upload in one step.
         */
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

        /**
         * @brief Reserves space for a mesh and uploads it.
         * @param streams One span per vertex stream, in the order the template parameters declare
         *        them. All must hold the same number of elements.
         * @param indices The index data, or nullopt for a non-indexed mesh.
         * @return The handle, or nullopt when the heap has no room.
         * @throws std::invalid_argument if the streams disagree on vertex count.
         */
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

        /** @brief How many vertices the heap can hold in total. */
        [[nodiscard]] glm::u64 VertexCapacity() const { return _vertexAllocator.Capacity(); }

        /** @brief How many indices the heap can hold, or 0 if it has no index buffer. */
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
