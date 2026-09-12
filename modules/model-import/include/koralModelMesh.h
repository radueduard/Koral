//
// Created by radue on 29.07.2026.
//

/**
 * @file koralModelMesh.h
 * @brief Where the model importer meets the mesh module: imported geometry, poured into a vertex format.
 *
 * kmdl::Importer reads a model file into plain CPU-side arrays and stops there — it has no opinion
 * about vertex formats. kmesh's formats know their layout but nothing about files. This is the join:
 * given an imported mesh and a format, it takes the attributes the format asks for, fills any the
 * file did not supply with zeroes, and uploads the result.
 *
 * @code
 * auto importer = kmdl::Importer::Load("models/sponza.gltf");
 * const auto scene = importer->LoadScene();
 * for (const auto& mesh : scene.meshes)
 *     meshes.push_back(kmdl::LoadMesh<MyMesh>(mesh));
 * @endcode
 *
 * Neither module depends on the other. This file is the seam, and it belongs to whoever includes it:
 * it is templates only, compiled into the consumer's translation unit, so `koral-model-import` never
 * links `koral-mesh` and `koral-mesh` never hears of Assimp. A project that builds geometry
 * procedurally gets vertex formats without a model reader; one that only wants the CPU-side arrays a
 * file contains gets the reader without vertex formats. Wanting both is what this header is for, and
 * it says so below rather than letting a missing module surface as a mysterious "no such file".
 */

#pragma once

// The compatibility gate. Including this header is a statement that both modules are in play, so say
// plainly which one is missing instead of failing on the include below.
#if !__has_include(<koralMesh.h>)
#  error "koralModelMesh.h bridges the model importer and the mesh module, and the mesh module is not here. Link koral-mesh too (target_link_libraries(<target> PRIVATE Koral::koral-model-import Koral::koral-mesh)), or include <koralModelImport.h> on its own if the imported CPU-side arrays are all you need."
#endif

#include <optional>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include <buffer.h>
#include <context.h>
#include <koralModelImport.h>
#include <resource.h>
#include <task.h>

#include <koralMesh.h>
#include <koralMeshHeap.h>

namespace kmdl
{
    /**
     * @brief Maps a vertex attribute type onto the field of Importer::Mesh that supplies it.
     * @tparam Attr The attribute being sourced.
     *
     * A specialisation answers two questions: whether the imported mesh has this attribute at all,
     * and how to read one vertex's worth of it. The built-in attributes are all specialised here;
     * specialise it for a custom attribute tag to make it loadable from a file too.
     */
    template<typename Attr>
    struct ImporterAttributeTraits
    {
        static bool available(const Importer::Mesh&) { return false; }
        static Attr::ValueType get(const Importer::Mesh&, unsigned int) { return {}; }
    };

    // ---- Position -----------------------------------------------------------
    template<> struct ImporterAttributeTraits<kmesh::Position2> {
        static bool available(const Importer::Mesh& m) { return !m.positions.empty(); }
        static glm::vec2 get(const Importer::Mesh& m, unsigned int v) { return { m.positions[v].x, m.positions[v].y }; }
    };
    template<> struct ImporterAttributeTraits<kmesh::Position> {
        static bool available(const Importer::Mesh& m) { return !m.positions.empty(); }
        static glm::vec3 get(const Importer::Mesh& m, unsigned int v) { return m.positions[v]; }
    };
    template<> struct ImporterAttributeTraits<kmesh::Position4> {
        static bool available(const Importer::Mesh& m) { return !m.positions.empty(); }
        static glm::vec4 get(const Importer::Mesh& m, unsigned int v) { return { m.positions[v], 1.f }; }
    };

    // ---- Normal -------------------------------------------------------------
    template<> struct ImporterAttributeTraits<kmesh::Normal> {
        static bool available(const Importer::Mesh& m) { return m.normals.has_value(); }
        static glm::vec3 get(const Importer::Mesh& m, unsigned int v) { return (*m.normals)[v]; }
    };
    template<> struct ImporterAttributeTraits<kmesh::Normal4> {
        static bool available(const Importer::Mesh& m) { return m.normals.has_value(); }
        static glm::vec4 get(const Importer::Mesh& m, unsigned int v) { return { (*m.normals)[v], 0.f }; }
    };

    // ---- UV -----------------------------------------------------------------
    template<> struct ImporterAttributeTraits<kmesh::UV> {
        static bool available(const Importer::Mesh& m) { return m.vertexUVs.contains(0u); }
        static glm::vec2 get(const Importer::Mesh& m, unsigned int v) { return m.vertexUVs.at(0u)[v]; }
    };
    template<std::size_t Channel>
    struct ImporterAttributeTraits<kmesh::IndexedAttribute<kmesh::UV, Channel>> {
        static bool available(const Importer::Mesh& m) { return m.vertexUVs.contains(static_cast<glm::u32>(Channel)); }
        static glm::vec2 get(const Importer::Mesh& m, unsigned int v) { return m.vertexUVs.at(static_cast<glm::u32>(Channel))[v]; }
    };

    // ---- Tangent / Bitangent ------------------------------------------------
    template<> struct ImporterAttributeTraits<kmesh::Tangent> {
        static bool available(const Importer::Mesh& m) { return m.tangents.has_value(); }
        static glm::vec3 get(const Importer::Mesh& m, unsigned int v) { return (*m.tangents)[v]; }
    };
    template<> struct ImporterAttributeTraits<kmesh::Bitangent> {
        static bool available(const Importer::Mesh& m) { return m.bitangents.has_value(); }
        static glm::vec3 get(const Importer::Mesh& m, unsigned int v) { return (*m.bitangents)[v]; }
    };
    template<> struct ImporterAttributeTraits<kmesh::PackedTangent> {
        static bool available(const Importer::Mesh& m) {
            return m.tangents.has_value() && m.bitangents.has_value() && m.normals.has_value();
        }
        static glm::vec4 get(const Importer::Mesh& m, unsigned int v) {
            const glm::vec3& n = (*m.normals)[v];
            const glm::vec3& t = (*m.tangents)[v];
            const glm::vec3& b = (*m.bitangents)[v];
            const float w = glm::dot(glm::cross(n, t), b) < 0.f ? -1.f : 1.f;
            return { t, w };
        }
    };

    // ---- Color --------------------------------------------------------------
    // Note: Importer::Mesh stores colors as vec3 (RGB). Alpha defaults to 1.
    template<> struct ImporterAttributeTraits<kmesh::Color3> {
        static bool available(const Importer::Mesh& m) { return m.vertexColors.contains(0u); }
        static glm::vec3 get(const Importer::Mesh& m, unsigned int v) { return m.vertexColors.at(0u)[v]; }
    };
    template<> struct ImporterAttributeTraits<kmesh::Color> {
        static bool available(const Importer::Mesh& m) { return m.vertexColors.contains(0u); }
        static glm::vec4 get(const Importer::Mesh& m, unsigned int v) { return { m.vertexColors.at(0u)[v], 1.f }; }
    };
    template<std::size_t Channel>
    struct ImporterAttributeTraits<kmesh::IndexedAttribute<kmesh::Color3, Channel>> {
        static bool available(const Importer::Mesh& m) { return m.vertexColors.contains(static_cast<glm::u32>(Channel)); }
        static glm::vec3 get(const Importer::Mesh& m, unsigned int v) { return m.vertexColors.at(static_cast<glm::u32>(Channel))[v]; }
    };
    template<std::size_t Channel>
    struct ImporterAttributeTraits<kmesh::IndexedAttribute<kmesh::Color, Channel>> {
        static bool available(const Importer::Mesh& m) { return m.vertexColors.contains(static_cast<glm::u32>(Channel)); }
        static glm::vec4 get(const Importer::Mesh& m, unsigned int v) { return { m.vertexColors.at(static_cast<glm::u32>(Channel))[v], 1.f }; }
    };

    // ---- Bones --------------------------------------------------------------
    // boneData = pair<vector<vec4> weights, vector<uvec4> ids>
    template<> struct ImporterAttributeTraits<kmesh::BoneWeights> {
        static bool available(const Importer::Mesh& m) { return m.boneData.has_value(); }
        static glm::vec4 get(const Importer::Mesh& m, unsigned int v) { return m.boneData->first[v]; }
    };
    template<> struct ImporterAttributeTraits<kmesh::BoneIds> {
        static bool available(const Importer::Mesh& m) { return m.boneData.has_value(); }
        static glm::ivec4 get(const Importer::Mesh& m, unsigned int v) { return static_cast<glm::ivec4>(m.boneData->second[v]); }
    };

    // =============================================================================
    // Internal helpers
    // =============================================================================
    namespace importer_detail
    {
        template<typename StreamT>
        std::vector<StreamT> buildVertices(const Importer::Mesh& mesh)
        {
            std::vector<StreamT> vertices(mesh.positions.size());
            for (unsigned int v = 0; v < static_cast<unsigned int>(vertices.size()); ++v)
            {
                [&]<std::size_t... I>(std::index_sequence<I...>)
                {
                    ([&]<std::size_t Idx>()
                    {
                        using Attr   = std::tuple_element_t<Idx, typename StreamT::Attributes>;
                        using Traits = ImporterAttributeTraits<Attr>;
                        if (Traits::available(mesh))
                            vertices[v].template get<Idx>() = Traits::get(mesh, v);
                    }.template operator()<I>(), ...);
                }(std::make_index_sequence<StreamT::kAttributeCount>{});
            }
            return vertices;
        }

        template<typename StreamT>
        kor::Resource<kor::Buffer> uploadVertexBuffer(const std::vector<StreamT>& vertices)
        {
            return kor::Buffer::Builder<StreamT>()
                .setDataView(vertices)
                .addUsage(kor::Buffer::Usage::eVertex)
                .addUsage(kor::Buffer::Usage::eStorage)
                // Needed so the buffer's GPU address can be queried — both for
                // ray-tracing acceleration structure builds and for buffer_reference
                // access in shaders.
                .addUsage(kor::Buffer::Usage::eShaderDeviceAddress)
                // Not every device supports ray tracing (see Context::SupportsRayTracing) — asking
                // for a buffer usage tied to an extension that was never enabled is itself a
                // Vulkan validation error, so this is skipped (eNone is a no-op) rather than
                // requested unconditionally.
                .addUsage(kor::Context::SupportsRayTracing()
                    ? kor::Buffer::Usage::eAccelerationStructureInput
                    : kor::Buffer::Usage::eNone)
                .setType(kor::Buffer::Type::eDeviceLocal)
                .build();
        }

        inline kor::Resource<kor::Buffer> uploadIndexBuffer(const std::vector<glm::u32>& indices)
        {
            return kor::Buffer::Builder<glm::u32>()
                .setDataView(indices)
                .addUsage(kor::Buffer::Usage::eIndex)
                .addUsage(kor::Buffer::Usage::eStorage)
                // See uploadVertexBuffer: address-queryable for acceleration structure
                // builds and buffer_reference access.
                .addUsage(kor::Buffer::Usage::eShaderDeviceAddress)
                .addUsage(kor::Context::SupportsRayTracing()
                    ? kor::Buffer::Usage::eAccelerationStructureInput
                    : kor::Buffer::Usage::eNone)
                .setType(kor::Buffer::Type::eDeviceLocal)
                .build();
        }
    } // namespace importer_detail

    /**
     * @brief Builds a standalone GPU mesh from imported geometry.
     * @tparam MeshT The target mesh type, a ParamMesh<...>. Specify it explicitly.
     * @param mesh The imported geometry, from kor::Importer::LoadScene or GetMesh.
     * @return The GPU mesh, with its own buffers.
     */
    template<typename MeshT>
    kor::Resource<MeshT> LoadMesh(const Importer::Mesh& mesh)
    {
        typename MeshT::Builder builder;

        [&]<std::size_t... I>(std::index_sequence<I...>)
        {
            (builder.SetVertexBuffer(
                static_cast<glm::u32>(I),
                importer_detail::uploadVertexBuffer(
                    importer_detail::buildVertices<
                        std::tuple_element_t<I, typename MeshT::Streams>
                    >(mesh)
                )
            ), ...);
        }(std::make_index_sequence<std::tuple_size_v<typename MeshT::Streams>>{});

        if (mesh.indices.has_value())
            builder.SetIndexBuffer(
                importer_detail::uploadIndexBuffer(*mesh.indices),
                kor::ChannelType::eUInt
            );

        return builder.Build();
    }

    /**
     * @brief Uploads imported geometry into a shared heap instead of its own buffers.
     * @param mesh The imported geometry.
     * @param heap The heap to suballocate from; the vertex streams are deduced from its type.
     * @return The allocation handle, or nullopt when the heap has no room. Freeing the handle
     *         returns the space.
     */
    template<typename... Streams>
    std::optional<typename kmesh::MeshHeap<Streams...>::Allocation>
    LoadMeshIntoHeap(const Importer::Mesh& mesh, const kmesh::MeshHeap<Streams...>& heap)
    {
        std::tuple<std::vector<Streams>...> streamsTuple{
            importer_detail::buildVertices<Streams>(mesh)...
        };

        std::optional<std::span<const glm::u32>> indexSpan;
        if (mesh.indices.has_value())
            indexSpan = std::span<const glm::u32>(*mesh.indices);

        return [&]<std::size_t... I>(std::index_sequence<I...>)
        {
            return heap.Create(
                std::span<const Streams>(std::get<I>(streamsTuple))...,
                indexSpan
            );
        }(std::index_sequence_for<Streams...>{});
    }

    /**
     * @brief Uploads imported geometry into a heap without blocking the frame.
     * @param mesh The imported geometry.
     * @param heap The heap to suballocate from.
     * @return A task yielding the allocation handle, or nullopt when the heap has no room.
     */
    template<typename... Streams>
    kor::Task<std::optional<typename kmesh::MeshHeap<Streams...>::Allocation>>
    LoadMeshIntoHeapAsync(const Importer::Mesh& mesh, const kmesh::MeshHeap<Streams...>& heap)
    {
        kor::Context::SwitchToBackgroundThread();

        std::tuple<std::vector<Streams>...> streamsTuple{
            importer_detail::buildVertices<Streams>(mesh)...
        };

        std::optional<std::span<const glm::u32>> indexSpan;
        if (mesh.indices.has_value())
            indexSpan = std::span<const glm::u32>(*mesh.indices);

        kor::Context::SwitchToMainThread();
        auto allocation = [&]<std::size_t... I>(std::index_sequence<I...>)
        {
            return heap.Create(
                std::span<const Streams>(std::get<I>(streamsTuple))...,
                indexSpan
            );
        }(std::index_sequence_for<Streams...>{});
        co_return std::move(allocation);
    }

    /** @brief Uploads into a heap held by reference. @see LoadMeshIntoHeap */
    template<typename... Streams>
    std::optional<typename kmesh::MeshHeap<Streams...>::Allocation>
    LoadMeshIntoHeap(const Importer::Mesh& mesh, kor::ResourceRef<const kmesh::MeshHeap<Streams...>> heap)
    { return LoadMeshIntoHeap(mesh, *heap); }

    /** @brief Uploads into a heap held by reference, without blocking. @see LoadMeshIntoHeapAsync */
    template<typename... Streams>
    kor::Task<std::optional<typename kmesh::MeshHeap<Streams...>::Allocation>>
    LoadMeshIntoHeapAsync(const Importer::Mesh& mesh, kor::Resource<const kmesh::MeshHeap<Streams...>>& heap)
    { return LoadMeshIntoHeapAsync(mesh, *heap); }
} // namespace kmdl
