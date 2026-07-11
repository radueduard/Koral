//
// Created by radue on 19.06.2026.
//

#pragma once

#include <optional>
#include <span>
#include <vector>

#include <assimp/scene.h>

#include "meshLayout.h"
#include "meshHeap.h"
#include "buffer.h"
#include "resource.h"


namespace gfx
{
    template<typename Attr>
    struct AssimpAttributeTraits
    {
        static bool available(const aiMesh&) { return false; }
        static typename Attr::ValueType get(const aiMesh&, unsigned int /*v*/)
        {
            return typename Attr::ValueType{};
        }
    };

    // ---- Position -------------------------------------------------------
    template<> struct AssimpAttributeTraits<Position2> {
        static bool available(const aiMesh& m) { return m.mVertices != nullptr; }
        static glm::vec2 get(const aiMesh& m, unsigned int v) {
            return { m.mVertices[v].x, m.mVertices[v].y };
        }
    };
    template<> struct AssimpAttributeTraits<Position> {
        static bool available(const aiMesh& m) { return m.mVertices != nullptr; }
        static glm::vec3 get(const aiMesh& m, unsigned int v) {
            return { m.mVertices[v].x, m.mVertices[v].y, m.mVertices[v].z };
        }
    };
    template<> struct AssimpAttributeTraits<Position4> {
        static bool available(const aiMesh& m) { return m.mVertices != nullptr; }
        static glm::vec4 get(const aiMesh& m, unsigned int v) {
            return { m.mVertices[v].x, m.mVertices[v].y, m.mVertices[v].z, 1.f };
        }
    };

    // ---- Normal ---------------------------------------------------------
    template<> struct AssimpAttributeTraits<Normal> {
        static bool available(const aiMesh& m) { return m.HasNormals(); }
        static glm::vec3 get(const aiMesh& m, unsigned int v) {
            return { m.mNormals[v].x, m.mNormals[v].y, m.mNormals[v].z };
        }
    };
    template<> struct AssimpAttributeTraits<Normal4> {
        static bool available(const aiMesh& m) { return m.HasNormals(); }
        static glm::vec4 get(const aiMesh& m, unsigned int v) {
            return { m.mNormals[v].x, m.mNormals[v].y, m.mNormals[v].z, 0.f };
        }
    };

    // ---- UV -------------------------------------------------------------
    template<> struct AssimpAttributeTraits<UV> {
        static bool available(const aiMesh& m) { return m.HasTextureCoords(0); }
        static glm::vec2 get(const aiMesh& m, unsigned int v) {
            return { m.mTextureCoords[0][v].x, m.mTextureCoords[0][v].y };
        }
    };
    template<> struct AssimpAttributeTraits<UV3> {
        static bool available(const aiMesh& m) { return m.HasTextureCoords(0); }
        static glm::vec3 get(const aiMesh& m, unsigned int v) {
            return { m.mTextureCoords[0][v].x, m.mTextureCoords[0][v].y, m.mTextureCoords[0][v].z };
        }
    };

    // ---- IndexedAttribute<UV, N>  (multi-UV-channel support) ------------
    template<std::size_t Channel>
    struct AssimpAttributeTraits<IndexedAttribute<UV, Channel>> {
        static bool available(const aiMesh& m) { return m.HasTextureCoords(static_cast<unsigned int>(Channel)); }
        static glm::vec2 get(const aiMesh& m, unsigned int v) {
            return { m.mTextureCoords[Channel][v].x, m.mTextureCoords[Channel][v].y };
        }
    };
    template<std::size_t Channel>
    struct AssimpAttributeTraits<IndexedAttribute<UV3, Channel>> {
        static bool available(const aiMesh& m) { return m.HasTextureCoords(static_cast<unsigned int>(Channel)); }
        static glm::vec3 get(const aiMesh& m, unsigned int v) {
            return { m.mTextureCoords[Channel][v].x, m.mTextureCoords[Channel][v].y, m.mTextureCoords[Channel][v].z };
        }
    };
    template<std::size_t Channel>
    struct AssimpAttributeTraits<IndexedAttribute<Color3, Channel>> {
        static bool available(const aiMesh& m) { return m.HasVertexColors(static_cast<unsigned int>(Channel)); }
        static glm::vec3 get(const aiMesh& m, unsigned int v) {
            return { m.mColors[Channel][v].r, m.mColors[Channel][v].g, m.mColors[Channel][v].b };
        }
    };
    template<std::size_t Channel>
    struct AssimpAttributeTraits<IndexedAttribute<Color, Channel>> {
        static bool available(const aiMesh& m) { return m.HasVertexColors(static_cast<unsigned int>(Channel)); }
        static glm::vec4 get(const aiMesh& m, unsigned int v) {
            return { m.mColors[Channel][v].r, m.mColors[Channel][v].g, m.mColors[Channel][v].b, m.mColors[Channel][v].a };
        }
    };

    // ---- Tangent / Bitangent --------------------------------------------
    template<> struct AssimpAttributeTraits<Tangent> {
        static bool available(const aiMesh& m) { return m.HasTangentsAndBitangents(); }
        static glm::vec3 get(const aiMesh& m, unsigned int v) {
            return { m.mTangents[v].x, m.mTangents[v].y, m.mTangents[v].z };
        }
    };
    template<> struct AssimpAttributeTraits<PackedTangent> {
        static bool available(const aiMesh& m) { return m.HasTangentsAndBitangents(); }
        static glm::vec4 get(const aiMesh& m, unsigned int v) {
            const glm::vec3 n = { m.mNormals[v].x,    m.mNormals[v].y,    m.mNormals[v].z   };
            const glm::vec3 t = { m.mTangents[v].x,   m.mTangents[v].y,   m.mTangents[v].z  };
            const glm::vec3 b = { m.mBitangents[v].x, m.mBitangents[v].y, m.mBitangents[v].z};
            const float w = glm::dot(glm::cross(n, t), b) < 0.f ? -1.f : 1.f;
            return { t.x, t.y, t.z, w };
        }
    };
    template<> struct AssimpAttributeTraits<Bitangent> {
        static bool available(const aiMesh& m) { return m.HasTangentsAndBitangents(); }
        static glm::vec3 get(const aiMesh& m, unsigned int v) {
            return { m.mBitangents[v].x, m.mBitangents[v].y, m.mBitangents[v].z };
        }
    };

    // ---- Color ----------------------------------------------------------
    template<> struct AssimpAttributeTraits<Color3> {
        static bool available(const aiMesh& m) { return m.HasVertexColors(0); }
        static glm::vec3 get(const aiMesh& m, unsigned int v) {
            return { m.mColors[0][v].r, m.mColors[0][v].g, m.mColors[0][v].b };
        }
    };
    template<> struct AssimpAttributeTraits<Color> {
        static bool available(const aiMesh& m) { return m.HasVertexColors(0); }
        static glm::vec4 get(const aiMesh& m, unsigned int v) {
            return { m.mColors[0][v].r, m.mColors[0][v].g, m.mColors[0][v].b, m.mColors[0][v].a };
        }
    };

    // ---- Bones ----------------------------------------------------------
    // BoneIds / BoneWeights are not per-vertex arrays in assimp — they're
    // accumulated from bone weight lists. They are handled separately;
    // the traits here just mark them as unavailable for the generic loop.
    template<> struct AssimpAttributeTraits<BoneIds> {
        static bool available(const aiMesh&) { return false; }
        static glm::ivec4 get(const aiMesh&, unsigned int) { return {}; }
    };
    template<> struct AssimpAttributeTraits<BoneWeights> {
        static bool available(const aiMesh&) { return false; }
        static glm::vec4 get(const aiMesh&, unsigned int) { return {}; }
    };

    namespace detail
    {
        // Build a CPU-side vertex array for StreamT from an aiMesh.
        template<typename StreamT>
        std::vector<StreamT> buildVertices(const aiMesh& mesh)
        {
            std::vector<StreamT> vertices(mesh.mNumVertices);
            for (unsigned int v = 0; v < mesh.mNumVertices; ++v)
            {
                [&]<std::size_t... I>(std::index_sequence<I...>)
                {
                    ([&]<std::size_t Idx>()
                    {
                        using Attr   = std::tuple_element_t<Idx, typename StreamT::Attributes>;
                        using Traits = AssimpAttributeTraits<Attr>;
                        if (Traits::available(mesh))
                            vertices[v].template get<Idx>() = Traits::get(mesh, v);
                    }.template operator()<I>(), ...);
                }(std::make_index_sequence<StreamT::kAttributeCount>{});
            }
            return vertices;
        }

        // Build a flat u32 index list from an aiMesh's faces.
        inline std::vector<glm::u32> buildIndices(const aiMesh& mesh)
        {
            std::vector<glm::u32> indices;
            indices.reserve(mesh.mNumFaces * 3);
            for (unsigned int f = 0; f < mesh.mNumFaces; ++f)
            {
                const aiFace& face = mesh.mFaces[f];
                for (unsigned int i = 0; i < face.mNumIndices; ++i)
                    indices.push_back(face.mIndices[i]);
            }
            return indices;
        }

        // Upload a CPU vertex vector to a device-local vertex buffer.
        template<typename StreamT>
        gfx::Resource<gfx::Buffer> uploadVertexBuffer(const std::vector<StreamT>& vertices)
        {
            return gfx::Buffer::Builder<StreamT>()
                .setDataView(std::span<const StreamT>(vertices))
                .addUsage(gfx::Buffer::Usage::eVertex)
                .addUsage(gfx::Buffer::Usage::eTransferDst)
                .addUsage(gfx::Buffer::Usage::eTransferSrc)
                .addUsage(gfx::Buffer::Usage::eStorage)
                .setType(gfx::Buffer::Type::eDeviceLocal)
                .build();
        }

        // Upload a CPU index vector to a device-local index buffer.
        inline gfx::Resource<gfx::Buffer> uploadIndexBuffer(const std::vector<glm::u32>& indices)
        {
            return gfx::Buffer::Builder<glm::u32>()
                .setDataView(std::span<const glm::u32>(indices))
                .addUsage(gfx::Buffer::Usage::eIndex)
                .addUsage(gfx::Buffer::Usage::eTransferDst)
                .addUsage(gfx::Buffer::Usage::eTransferSrc)
                .addUsage(gfx::Buffer::Usage::eStorage)
                .setType(gfx::Buffer::Type::eDeviceLocal)
                .build();
        }
    } // namespace detail

    // -------------------------------------------------------------------------
    // LoadMesh<MeshT>(aiMesh)
    //   Builds a standalone ParamMesh with its own GPU buffers.
    //   MeshT must be ParamMesh<Stream0, Stream1, ...>.
    // -------------------------------------------------------------------------
    template<typename MeshT>
    gfx::Resource<MeshT> LoadMesh(const aiMesh& mesh)
    {
        typename MeshT::Builder builder;

        [&]<std::size_t... I>(std::index_sequence<I...>)
        {
            (builder.SetVertexBuffer(
                static_cast<glm::u32>(I),
                detail::uploadVertexBuffer(
                    detail::buildVertices<std::tuple_element_t<I, typename MeshT::Streams>>(mesh)
                )
            ), ...);
        }(std::make_index_sequence<std::tuple_size_v<typename MeshT::Streams>>{});

        if (mesh.HasFaces())
        {
            auto indices = detail::buildIndices(mesh);
            builder.SetIndexBuffer(detail::uploadIndexBuffer(indices), gfx::ChannelType::eUInt);
        }

        return builder.Build();
    }

    // -------------------------------------------------------------------------
    // LoadMeshIntoHeap<Streams...>(heap, aiMesh)
    //   Allocates and uploads a mesh into an existing MeshHeap.
    //   Returns nullopt if the heap has insufficient space.
    // -------------------------------------------------------------------------
    template<typename... Streams>
    std::optional<typename MeshHeap<Streams...>::Allocation>
    LoadMeshIntoHeap(const MeshHeap<Streams...>& heap, const aiMesh& mesh)
    {
        std::tuple<std::vector<Streams>...> streamsTuple{
            detail::buildVertices<Streams>(mesh)...
        };

        std::optional<std::vector<glm::u32>> indices;
        if (mesh.HasFaces())
            indices = detail::buildIndices(mesh);

        return [&]<std::size_t... I>(std::index_sequence<I...>)
        {
            return heap.Create(
                std::span<const Streams>(std::get<I>(streamsTuple))...,
                indices
                    ? std::make_optional(std::span<const glm::u32>(*indices))
                    : std::nullopt
            );
        }(std::index_sequence_for<Streams...>{});
    }

} // namespace gfx

