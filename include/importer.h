//
// Created by radue on 2/23/2026.
//

#pragma once

#include <expected>
#include <filesystem>
#include <memory>

#include "api.h"

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

    class GFX_API Importer
    {
    public:
        virtual ~Importer() = default;

        static std::unique_ptr<Image> LoadImage(const std::filesystem::path& path, bool generateMipmaps = false);
        static void SaveImage(const std::filesystem::path& path, const std::string& name, FileFormat fileFormat, const Image& image);
        static std::unique_ptr<Importer> LoadMeshes(const std::filesystem::path& path);

        virtual std::vector<std::string> GetMeshNames() = 0;
        virtual std::expected<std::vector<glm::vec3>, std::string> GetVertexPositions(const std::string& meshName) = 0;
        virtual std::expected<std::vector<glm::vec3>, std::string> GetVertexNormals(const std::string& meshName) = 0;
        virtual std::expected<std::vector<glm::vec3>, std::string> GetVertexTangents(const std::string& meshName) = 0;
        virtual std::expected<std::vector<glm::vec3>, std::string> GetVertexBitangents(const std::string& meshName) = 0;
        virtual std::expected<std::vector<glm::vec3>, std::string> GetVertexColors(const std::string& meshName, glm::u32 id) = 0;
        virtual std::expected<std::vector<glm::vec2>, std::string> GetVertexUVs(const std::string& meshName, glm::u32 id) = 0;
        virtual std::expected<std::pair<std::vector<glm::vec4>, std::vector<glm::uvec4>>, std::string> GetBoneData(const std::string& meshName) = 0;
        virtual std::expected<std::vector<glm::u32>, std::string> GetIndices(const std::string& meshName) = 0;
        virtual std::expected<std::vector<glm::mat4>, std::string> GetBoneTransformationMatrices() = 0;
    };
}

