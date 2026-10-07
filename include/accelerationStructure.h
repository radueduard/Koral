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
#include <cstdint>
#include <vector>
#include <kmath/matrix.h>


#include "api.h"
#include <source_location>

#include "builder.h"
#include "resource.h"
#include "error.h"

namespace kor
{
    class Mesh;

    /**
     * @brief The spatial index rays are traced against: geometry (BLAS) or placed instances (TLAS).
     *
     * Ray tracing needs the scene organised so a ray can find what it hits without testing every
     * triangle. That is a two-level structure: a bottom-level structure holds the triangles of a
     * mesh, and a top-level structure holds instances, each naming a bottom-level structure and a
     * transform. Building the geometry once and instancing it many times is what makes a scene of
     * repeated objects cheap.
     *
     * @code
     * kor::AccelerationStructure::Builder blasBuilder;
     * auto blas = blasBuilder.AddMesh(mesh).Build();
     *
     * kor::AccelerationStructure::Builder tlasBuilder;
     * auto tlas = tlasBuilder.AddInstance({ .blas = blas, .transform = model }).Build();
     * @endcode
     *
     * The TLAS is what a shader binds, through a Descriptor built from it. Requires a device with
     * ray-tracing support — see Context::SupportsRayTracing(). Structures are built as they are
     * created, not refitted, so a moving instance means rebuilding the TLAS; the BLAS it refers to
     * can stay.
     */
    class KORAL_API AccelerationStructure
    {
    public:
        /** @brief Which level of the two-level structure this is. */
        enum class Type : std::uint8_t {
            eBottomLevel,   ///< Holds geometry built from meshes.
            eTopLevel,      ///< Holds instances referencing bottom-level structures.
        };

        /** @brief A placed instance of a bottom-level structure inside a top-level one. */
        struct KORAL_API Instance
        {
            ResourceRef<const AccelerationStructure> blas;   ///< The geometry being placed. Must be a bottom-level structure.
            kor::Mat4 transform = kor::Mat4(1.0f);           ///< Where it sits in world space.
            kor::u32 instanceCustomIndex = 0;   ///< Available to shaders as gl_InstanceCustomIndexEXT.
            kor::u32 hitGroupIndex = 0;         ///< SBT hit-group record offset for this instance.
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
            kor::u64 firstVertex = 0;       ///< First vertex (element offset) covered by this geometry.
            kor::u64 vertexCount = 0;       ///< Number of vertices; 0 means "the rest of the mesh".
            kor::u64 firstIndex  = 0;       ///< First index (element offset) covered by this geometry.
            kor::u64 indexCount  = 0;       ///< Number of indices; 0 means "all of the mesh's indices".
        };

        struct KORAL_API Builder : kor::Builder
        {
            std::vector<Geometry> geometries = {};  ///< Geometry for a bottom-level structure.
            std::vector<Instance> instances = {};   ///< Instances for a top-level structure.

            /** @brief Add a whole mesh as a geometry of the bottom-level structure. */
            Builder& AddMesh(ResourceRef<const Mesh> mesh);

            /** @brief Add an explicit (sub)range of a mesh as a geometry of the bottom-level structure. */
            Builder& AddGeometry(const Geometry& geometry);

            /**
             * @brief Add a MeshHeap suballocation as a geometry of the bottom-level structure.
             *
             * Templated so this header stays decoupled from MeshHeap: it only reads the
             * vertex/index ranges from the allocation. Pass the heap that produced the
             * allocation together with the `Allocation` handle returned when it was created.
             */
            template<typename Heap>
            Builder& AddMesh(const Resource<Heap>& heap, const typename Heap::Allocation& allocation)
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
                return AddGeometry(geometry);
            }

            /** @brief Add an instance of a bottom-level structure to the top-level structure. */
            Builder& AddInstance(const Instance& instance);

            /** @brief One build attempt. Internal: prefer Build(). */
            [[nodiscard]] Result<std::unique_ptr<AccelerationStructure>> Create() const;
            [[nodiscard]] kor::Resource<AccelerationStructure> Build(std::source_location where = std::source_location::current()) const;
        };

        virtual ~AccelerationStructure();

        AccelerationStructure(const AccelerationStructure&) = delete;
        AccelerationStructure& operator=(const AccelerationStructure&) = delete;

        /** @brief Whether this is a bottom-level or top-level structure. */
        [[nodiscard]] Type StructureType() const { return _type; }

    protected:
        explicit AccelerationStructure(const Builder& createInfo);

        Type _type;
    };
}
