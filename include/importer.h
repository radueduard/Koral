//
// Created by radue on 2/23/2026.
//

#pragma once

#include <expected>
#include <filesystem>
#include <memory>

#include "api.h"
#include "task.h"

namespace gfx
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

    struct GFX_API AABB {
        glm::vec3 min;
        glm::vec3 max;
    };

    class GFX_API Importer
    {
    public:
        struct GFX_API Material {
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

        struct GFX_API Mesh {
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

        struct GFX_API Node {
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

        struct GFX_API Scene {
            std::vector<Mesh> meshes;
            std::vector<Material> materials;
            std::vector<Node> nodes;
        };

        virtual ~Importer() = default;

        static gfx::Resource<Image> LoadImage(const std::filesystem::path& path, bool generateMipmaps = false);
        static Task<void> LoadImageAsync(const std::filesystem::path& path, bool generateMipmaps, Resource<Image> &returnImage);

        static void SaveImage(const std::filesystem::path& path, const std::string& name, FileFormat fileFormat, ResourceRef<Image> image);
        static std::unique_ptr<Importer> Load(const std::filesystem::path& path);

        virtual std::vector<std::string> GetMeshNames() = 0;
        virtual std::vector<std::string> GetMaterialNames() = 0;

        virtual Mesh GetMesh(const std::string& name) = 0;
        virtual Material GetMaterial(const std::string& name) = 0;

        virtual Scene LoadScene() = 0;

        virtual std::expected<std::vector<glm::mat4>, std::string> GetBoneTransformationMatrices() = 0;

    protected:
        std::filesystem::path _path;
    };
}

