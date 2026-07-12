//
// Created by radue on 6/23/2026.
//

/**
 * @file accelerationStructure.h
 * @brief Ray-tracing acceleration structure abstraction (BLAS / TLAS).
 *
 * A bottom-level acceleration structure (BLAS) holds geometry, built from one or
 * more meshes' vertex/index buffers. A top-level acceleration structure (TLAS)
 * holds instances, each referencing a BLAS plus a transform; the TLAS is what a
 * ray-tracing shader binds through an `eAccelerationStructure` descriptor.
 */

#pragma once
#include <vector>
#include <glm/mat4x4.hpp>
#include <glm/fwd.hpp>

#include "api.h"
#include "builder.h"
#include "resource.h"
#include "error.h"

namespace kor
{
    class Mesh;

    class KORAL_API AccelerationStructure
    {
    public:
        enum class Type
        {
            eBottomLevel,   ///< Holds geometry built from meshes.
            eTopLevel,      ///< Holds instances referencing bottom-level structures.
        };

        /** @brief A placed instance of a bottom-level structure inside a top-level one. */
        struct KORAL_API Instance
        {
            ResourceRef<const AccelerationStructure> blas;
            glm::mat4 transform = glm::mat4(1.0f);
            glm::u32 instanceCustomIndex = 0;   ///< Available to shaders as gl_InstanceCustomIndexEXT.
            glm::u32 hitGroupIndex = 0;         ///< SBT hit-group record offset for this instance.
        };

        /**
         * @brief A single triangle geometry of a bottom-level structure.
         *
         * Describes a (sub)range of a mesh's vertex and index buffers. A plain mesh
         * uses the whole buffers (counts left at zero); a MeshHeap allocation uses
         * the range covered by its `Allocation` so a single heap can back many BLAS
         * geometries without duplicating buffers.
         */
        struct KORAL_API Geometry
        {
            ResourceRef<const Mesh> mesh;   ///< Source of the vertex/index buffers and position attribute.
            glm::u64 firstVertex = 0;       ///< First vertex (element offset) covered by this geometry.
            glm::u64 vertexCount = 0;       ///< Number of vertices; 0 means "the rest of the mesh".
            glm::u64 firstIndex  = 0;       ///< First index (element offset) covered by this geometry.
            glm::u64 indexCount  = 0;       ///< Number of indices; 0 means "all of the mesh's indices".
        };

        struct KORAL_API Builder : ::Builder
        {
            std::vector<Geometry> geometries = {};  ///< Geometry for a bottom-level structure.
            std::vector<Instance> instances = {};   ///< Instances for a top-level structure.

            /** @brief Add a whole mesh as a geometry of the bottom-level structure. */
            Builder& addMesh(ResourceRef<const Mesh> mesh);

            /** @brief Add an explicit (sub)range of a mesh as a geometry of the bottom-level structure. */
            Builder& addGeometry(const Geometry& geometry);

            /**
             * @brief Add a MeshHeap suballocation as a geometry of the bottom-level structure.
             *
             * Templated so this header stays decoupled from MeshHeap: it only reads the
             * vertex/index ranges from the allocation. Pass the heap that produced the
             * allocation together with the `Allocation` handle returned when it was created.
             */
            template<typename Heap>
            Builder& addMesh(const Resource<Heap>& heap, const typename Heap::Allocation& allocation)
            {
                // Aggregate-init: Geometry::mesh is a ResourceRef with no default ctor,
                // so the struct cannot be default-constructed and then assigned.
                Geometry geometry{
                    .mesh        = ResourceRef<const Mesh>(heap),
                    .firstVertex = allocation.vertexIdentifier.offset,
                    .vertexCount = allocation.vertexIdentifier.size,
                };
                if (allocation.indexIdentifier.has_value())
                {
                    geometry.firstIndex = allocation.indexIdentifier->offset;
                    geometry.indexCount = allocation.indexIdentifier->size;
                }
                return addGeometry(geometry);
            }

            /** @brief Add an instance of a bottom-level structure to the top-level structure. */
            Builder& addInstance(const Instance& instance);

            /** @brief One build attempt. Internal: prefer build(). */
            [[nodiscard]] Result<std::unique_ptr<AccelerationStructure>> create() const;
            [[nodiscard]] kor::Resource<AccelerationStructure> build() const;
        };

        virtual ~AccelerationStructure();

        AccelerationStructure(const AccelerationStructure&) = delete;
        AccelerationStructure& operator=(const AccelerationStructure&) = delete;

        /** @brief Whether this is a bottom-level or top-level structure. */
        [[nodiscard]] Type getType() const { return _type; }

    protected:
        explicit AccelerationStructure(const Builder& createInfo);

        Type _type;
    };
}
