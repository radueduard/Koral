//
// Created by radue on 2/23/2026.
//

#pragma once

#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "api.h"
#include "context.h"
#include "task.h"
#include "meshLayout.h"
#include "meshHeap.h"
#include "buffer.h"

namespace kor
{
    class Image;

    enum class FileFormat
    {
        ePNG,
        eJPG,
        eBMP,
        eTGA,
        eHDR,
        eDDS,
        ePPM,
        eTIF,
    };

    struct KORAL_API AABB {
        glm::vec3 min;
        glm::vec3 max;
    };

    class KORAL_API Importer
    {
    public:
        struct KORAL_API Material {
            std::string name;

            float alphaCutoff = 1.f;
            glm::vec4 baseColorFactor = glm::vec4(1.0f);
            glm::vec4 emissiveFactor = glm::vec4(1.0f);
            float roughness = 1.0f;
            float metallic = 1.0f;
            int doubleSided = false;

            std::optional<std::filesystem::path> albedoTexturePath;
            std::optional<std::filesystem::path> normalTexturePath;
            std::optional<std::filesystem::path> roughnessTexturePath;
            std::optional<std::filesystem::path> metallicTexturePath;
            std::optional<std::filesystem::path> ambientOcclusionTexturePath;
            std::optional<std::filesystem::path> emissiveTexturePath;

            std::optional<std::filesystem::path> ambientTexturePath;
            std::optional<std::filesystem::path> diffuseTexturePath;
            std::optional<std::filesystem::path> specularTexturePath;
            std::optional<std::filesystem::path> shininessTexturePath;

            std::optional<std::filesystem::path> displacementTexturePath;
            std::optional<std::filesystem::path> alphaTexturePath;
            std::optional<std::filesystem::path> heightTexturePath;
        };

        struct KORAL_API Mesh {
            std::string name;

            std::vector<glm::vec3> positions;
            std::optional<std::vector<glm::vec3>> normals;
            std::optional<std::vector<glm::vec3>> tangents;
            std::optional<std::vector<glm::vec3>> bitangents;
            std::unordered_map<glm::u32, std::vector<glm::vec3>> vertexColors;
            std::unordered_map<glm::u32, std::vector<glm::vec2>> vertexUVs;
            std::optional<std::pair<std::vector<glm::vec4>, std::vector<glm::uvec4>>> boneData;
            std::optional<std::vector<glm::u32>> indices;
        };

        struct KORAL_API Node {
            glm::i32 id = -1;
            std::string name;
            std::vector<glm::u32> childIndices;

            std::vector<glm::u32> meshIndices;
            std::vector<glm::u32> materialIndices;

            glm::vec3 position = glm::vec3(0.f, 0.f, 0.f);
            glm::vec3 rotation = glm::vec3(0.f, 0.f, 0.f);
            glm::vec3 scale = glm::vec3(1.f, 1.f, 1.f);
            AABB aabb;
        };

        struct KORAL_API Light {
            enum class Type { ePoint, eDirectional, eSpot };

            std::string name;
            Type      type      = Type::ePoint;
            glm::vec3 position  = glm::vec3(0.0f);              // world space
            glm::vec3 direction = glm::vec3(0.0f, -1.0f, 0.0f); // world space (spot/directional)
            glm::vec3 color     = glm::vec3(1.0f);
            float     intensity = 1.0f;
            float     range          = 0.0f;  // 0 = no limit
            float     innerConeAngle = 0.0f;  // radians (spot)
            float     outerConeAngle = 0.0f;  // radians (spot)
        };

        struct KORAL_API Scene {
            std::vector<Mesh> meshes;
            std::vector<Material> materials;
            std::vector<Node> nodes;
            std::vector<Light> lights;
        };

        virtual ~Importer() = default;

        // A relative path is resolved against the asset search roots — the directories listed under
        // "assetDirectories" in koral.json, then the ones that ship with the engine — so a scene can
        // say LoadImage("textures/wood.png") without knowing where the project ended up on disk.
        // An absolute path is opened as given. See kor::assetPath and projectConfig.h.
        static kor::Resource<Image> LoadImage(const std::filesystem::path& relativePath, bool generateMipmaps = false);
        // Returns the image directly via the awaited task. All CPU work runs on background
        // threads; GPU uploads are chunked so the main/render thread is never held for long.
        static Task<Resource<Image>> LoadImageAsync(const std::filesystem::path& relativePath, bool generateMipmaps = false);

        // Writes where it is told: an output path is not a lookup, so it is never resolved.
        static void SaveImage(const std::filesystem::path& path, const std::string& name, FileFormat fileFormat, ResourceRef<const Image> image);

        // Resolved like LoadImage. The model's material textures are then looked for beside the
        // model itself, and failing that, across the same asset roots.
        static std::unique_ptr<Importer> Load(const std::filesystem::path& relativePath);

        virtual std::vector<std::string> GetMeshNames() = 0;
        virtual std::vector<std::string> GetMaterialNames() = 0;

        virtual Mesh GetMesh(const std::string& name) = 0;
        virtual Material GetMaterial(const std::string& name) = 0;

        virtual Scene LoadScene() = 0;

        virtual std::expected<std::vector<glm::mat4>, std::string> GetBoneTransformationMatrices() = 0;

        // ------------------------------------------------------------------
        // Typed GPU mesh loading — works on any Importer::Mesh returned by
        // LoadScene() or GetMesh(), without needing a concrete importer type.
        // ------------------------------------------------------------------

        // Build a standalone ParamMesh from scene mesh data.
        // MeshT must be a ParamMesh<Stream0, ...>; specify it explicitly.
        template<typename MeshT>
        kor::Resource<MeshT> LoadMesh(const Mesh& mesh);

        // Suballocate into a MeshHeap. Streams are deduced from the heap type.
        template<typename... Streams>
        std::optional<typename MeshHeap<Streams...>::Allocation>
        LoadMeshIntoHeap(const Mesh& mesh, const MeshHeap<Streams...>& heap);

        template <class ... Streams>
        Task<std::optional<typename MeshHeap<Streams...>::Allocation>>
        LoadMeshIntoHeapAsync(const Mesh& mesh, const MeshHeap<Streams...>& heap);

        template<typename... Streams>
        std::optional<typename MeshHeap<Streams...>::Allocation>
        LoadMeshIntoHeap(const Mesh& mesh, ResourceRef<const MeshHeap<Streams...>> heap)
        { return LoadMeshIntoHeap(mesh, *heap); }

        template<typename... Streams>
        Task<std::optional<typename MeshHeap<Streams...>::Allocation>>
        LoadMeshIntoHeapAsync(const Mesh& mesh, Resource<const MeshHeap<Streams...>>& heap)
        { return LoadMeshIntoHeapAsync(mesh, *heap); }

    protected:
        std::filesystem::path _path;
    };
} // namespace kor

// =============================================================================
// ImporterAttributeTraits<Attr>
//   Maps each VertexAttributeType to the corresponding field in Importer::Mesh.
//   Specialise this for any custom attribute tags.
// =============================================================================
namespace kor
{
    template<typename Attr>
    struct ImporterAttributeTraits
    {
        static bool available(const Importer::Mesh&) { return false; }
        static Attr::ValueType get(const Importer::Mesh&, unsigned int) { return {}; }
    };

    // ---- Position -----------------------------------------------------------
    template<> struct ImporterAttributeTraits<Position2> {
        static bool available(const Importer::Mesh& m) { return !m.positions.empty(); }
        static glm::vec2 get(const Importer::Mesh& m, unsigned int v) { return { m.positions[v].x, m.positions[v].y }; }
    };
    template<> struct ImporterAttributeTraits<Position> {
        static bool available(const Importer::Mesh& m) { return !m.positions.empty(); }
        static glm::vec3 get(const Importer::Mesh& m, unsigned int v) { return m.positions[v]; }
    };
    template<> struct ImporterAttributeTraits<Position4> {
        static bool available(const Importer::Mesh& m) { return !m.positions.empty(); }
        static glm::vec4 get(const Importer::Mesh& m, unsigned int v) { return { m.positions[v], 1.f }; }
    };

    // ---- Normal -------------------------------------------------------------
    template<> struct ImporterAttributeTraits<Normal> {
        static bool available(const Importer::Mesh& m) { return m.normals.has_value(); }
        static glm::vec3 get(const Importer::Mesh& m, unsigned int v) { return (*m.normals)[v]; }
    };
    template<> struct ImporterAttributeTraits<Normal4> {
        static bool available(const Importer::Mesh& m) { return m.normals.has_value(); }
        static glm::vec4 get(const Importer::Mesh& m, unsigned int v) { return { (*m.normals)[v], 0.f }; }
    };

    // ---- UV -----------------------------------------------------------------
    template<> struct ImporterAttributeTraits<UV> {
        static bool available(const Importer::Mesh& m) { return m.vertexUVs.contains(0u); }
        static glm::vec2 get(const Importer::Mesh& m, unsigned int v) { return m.vertexUVs.at(0u)[v]; }
    };
    template<std::size_t Channel>
    struct ImporterAttributeTraits<IndexedAttribute<UV, Channel>> {
        static bool available(const Importer::Mesh& m) { return m.vertexUVs.contains(static_cast<glm::u32>(Channel)); }
        static glm::vec2 get(const Importer::Mesh& m, unsigned int v) { return m.vertexUVs.at(static_cast<glm::u32>(Channel))[v]; }
    };

    // ---- Tangent / Bitangent ------------------------------------------------
    template<> struct ImporterAttributeTraits<Tangent> {
        static bool available(const Importer::Mesh& m) { return m.tangents.has_value(); }
        static glm::vec3 get(const Importer::Mesh& m, unsigned int v) { return (*m.tangents)[v]; }
    };
    template<> struct ImporterAttributeTraits<Bitangent> {
        static bool available(const Importer::Mesh& m) { return m.bitangents.has_value(); }
        static glm::vec3 get(const Importer::Mesh& m, unsigned int v) { return (*m.bitangents)[v]; }
    };
    template<> struct ImporterAttributeTraits<PackedTangent> {
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
    template<> struct ImporterAttributeTraits<Color3> {
        static bool available(const Importer::Mesh& m) { return m.vertexColors.contains(0u); }
        static glm::vec3 get(const Importer::Mesh& m, unsigned int v) { return m.vertexColors.at(0u)[v]; }
    };
    template<> struct ImporterAttributeTraits<Color> {
        static bool available(const Importer::Mesh& m) { return m.vertexColors.contains(0u); }
        static glm::vec4 get(const Importer::Mesh& m, unsigned int v) { return { m.vertexColors.at(0u)[v], 1.f }; }
    };
    template<std::size_t Channel>
    struct ImporterAttributeTraits<IndexedAttribute<Color3, Channel>> {
        static bool available(const Importer::Mesh& m) { return m.vertexColors.contains(static_cast<glm::u32>(Channel)); }
        static glm::vec3 get(const Importer::Mesh& m, unsigned int v) { return m.vertexColors.at(static_cast<glm::u32>(Channel))[v]; }
    };
    template<std::size_t Channel>
    struct ImporterAttributeTraits<IndexedAttribute<Color, Channel>> {
        static bool available(const Importer::Mesh& m) { return m.vertexColors.contains(static_cast<glm::u32>(Channel)); }
        static glm::vec4 get(const Importer::Mesh& m, unsigned int v) { return { m.vertexColors.at(static_cast<glm::u32>(Channel))[v], 1.f }; }
    };

    // ---- Bones --------------------------------------------------------------
    // boneData = pair<vector<vec4> weights, vector<uvec4> ids>
    template<> struct ImporterAttributeTraits<BoneWeights> {
        static bool available(const Importer::Mesh& m) { return m.boneData.has_value(); }
        static glm::vec4 get(const Importer::Mesh& m, unsigned int v) { return m.boneData->first[v]; }
    };
    template<> struct ImporterAttributeTraits<BoneIds> {
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
                .setDataView(std::span<const StreamT>(vertices))
                .addUsage(kor::Buffer::Usage::eVertex)
                .addUsage(kor::Buffer::Usage::eTransferDst)
                .addUsage(kor::Buffer::Usage::eTransferSrc)
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
                .setDataView(std::span(indices))
                .addUsage(kor::Buffer::Usage::eIndex)
                .addUsage(kor::Buffer::Usage::eTransferDst)
                .addUsage(kor::Buffer::Usage::eTransferSrc)
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

    // =============================================================================
    // Importer::LoadMesh / Importer::LoadMeshIntoHeap — out-of-line definitions
    // =============================================================================

    template<typename MeshT>
    kor::Resource<MeshT> Importer::LoadMesh(const Mesh& mesh)
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

    template<typename... Streams>
    std::optional<typename MeshHeap<Streams...>::Allocation>
    Importer::LoadMeshIntoHeap(const Mesh& mesh, const MeshHeap<Streams...>& heap)
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

    template<typename... Streams>
    kor::Task<std::optional<typename MeshHeap<Streams...>::Allocation>>
    Importer::LoadMeshIntoHeapAsync(const Mesh& mesh, const MeshHeap<Streams...>& heap)
    {
        Context::SwitchToBackgroundThread();

        std::tuple<std::vector<Streams>...> streamsTuple{
            importer_detail::buildVertices<Streams>(mesh)...
        };

        std::optional<std::span<const glm::u32>> indexSpan;
        if (mesh.indices.has_value())
            indexSpan = std::span<const glm::u32>(*mesh.indices);

        Context::SwitchToMainThread();
        auto allocation = [&]<std::size_t... I>(std::index_sequence<I...>)
        {
            return heap.Create(
                std::span<const Streams>(std::get<I>(streamsTuple))...,
                indexSpan
            );
        }(std::index_sequence_for<Streams...>{});
        co_return std::move(allocation);
    }

} // namespace kor

