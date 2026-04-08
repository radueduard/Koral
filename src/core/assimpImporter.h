//
// Created by radue on 4/1/2026.
//

#pragma once

#include <unordered_map>

#include "importer.h"

#include <assimp/Importer.hpp>
#include <assimp/Exporter.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

namespace gfx {
    class AssimpImporter : public Importer {
    public:
        explicit AssimpImporter(const std::filesystem::path &path);

        std::vector<std::string> GetMeshNames() override;
        std::expected<std::vector<glm::vec3>, std::string> GetVertexPositions(const std::string &meshName) override;
        std::expected<std::vector<glm::vec3>, std::string> GetVertexNormals(const std::string &meshName) override;
        std::expected<std::vector<glm::vec3>, std::string> GetVertexTangents(const std::string &meshName) override;
        std::expected<std::vector<glm::vec3>, std::string> GetVertexBitangents(const std::string &meshName) override;
        std::expected<std::vector<glm::vec3>, std::string> GetVertexColors(const std::string &meshName, glm::u32 id) override;
        std::expected<std::vector<glm::vec2>, std::string> GetVertexUVs(const std::string &meshName, glm::u32 id) override;
        std::expected<std::pair<std::vector<glm::vec4>, std::vector<glm::uvec4>>, std::string> GetBoneData(const std::string &meshName) override;
        std::expected<std::vector<glm::u32>, std::string> GetIndices(const std::string &meshName) override;
        std::expected<std::vector<glm::mat4>, std::string> GetBoneTransformationMatrices() override;

        ~AssimpImporter() override;


    private:
        Assimp::Importer _importer = {};
        Assimp::Exporter _exporter = {};
        const aiScene* _scene;
        std::unordered_map<std::string, glm::u32> _meshNameToIndex;
        std::unordered_map<glm::u32, std::string> _indexToMeshName;
    };

    inline AssimpImporter::AssimpImporter(const std::filesystem::path &path) {
        _scene = _importer.ReadFile(path.string(), aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_SortByPType);
        std::string format = path.extension().string();

        bool shouldSave = false;
        if (_scene->HasMeshes() && !_scene->mMeshes[0]->HasTangentsAndBitangents()) {
            _importer.ApplyPostProcessing(aiProcess_CalcTangentSpace);
            shouldSave = true;
        }
        if (_scene->HasMeshes() && !_scene->mMeshes[0]->HasTextureCoords(0)) {
            _importer.ApplyPostProcessing(aiProcess_GenUVCoords);
            shouldSave = true;
        }
        if (shouldSave) {
            _importer.ApplyPostProcessing(aiProcess_OptimizeGraph);
            _importer.ApplyPostProcessing(aiProcess_OptimizeMeshes);

            // const std::filesystem::path outputPath = path.parent_path() / (path.stem().string() + "_processed" + path.extension().string());
            // Assimp::ExportProperties exportProps;
            // if (_exporter.Export(_scene, format.substr(1), outputPath.string()) != AI_SUCCESS) {
            //     std::cerr << "Failed to export processed scene: " << _exporter.GetErrorString() << std::endl;
            // }
        }

        _importer.ApplyPostProcessing(aiProcess_FlipUVs);

        if (_scene == nullptr) {
            std::cerr << "Could not read file: " << path.string() << std::endl;
            return;
        }

        for (unsigned int i = 0; i < _scene->mNumMeshes; ++i) {
            const std::string meshName = _scene->mMeshes[i]->mName.C_Str();
            _meshNameToIndex[meshName] = i;
            _indexToMeshName[i] = meshName;
        }
    }

    inline std::vector<std::string> AssimpImporter::GetMeshNames() {
        std::vector<std::string> meshNames;
        meshNames.reserve(_scene->mNumMeshes);
        for (unsigned int i = 0; i < _scene->mNumMeshes; ++i) {
            meshNames.emplace_back(_scene->mMeshes[i]->mName.C_Str());
        }
        return meshNames;
    }

    inline std::expected<std::vector<glm::vec3>, std::string> AssimpImporter::GetVertexPositions(const std::string &meshName) {
        if (!_meshNameToIndex.contains(meshName)) {
            return std::unexpected("Mesh not found: " + meshName);
        }
        std::vector<glm::vec3> positions;
        const auto mesh = _scene->mMeshes[_meshNameToIndex[meshName]];
        positions.reserve(mesh->mNumVertices);
        for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
            const auto& vertex = mesh->mVertices[i];
            positions.emplace_back(vertex.x, vertex.y, vertex.z);
        }
        return positions;
    }

    inline std::expected<std::vector<glm::vec3>, std::string> AssimpImporter::GetVertexNormals(const std::string &meshName) {
        if (!_meshNameToIndex.contains(meshName)) {
            return std::unexpected("Mesh not found: " + meshName);
        }
        const auto mesh = _scene->mMeshes[_meshNameToIndex[meshName]];
        if (mesh->mNormals == nullptr) {
            return std::unexpected("Mesh does not contain normals: " + meshName);
        }
        std::vector<glm::vec3> normals;
        normals.reserve(mesh->mNumVertices);
        for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
            const auto& normal = mesh->mNormals[i];
            normals.emplace_back(normal.x, normal.y, normal.z);
        }
        return normals;
    }

    inline std::expected<std::vector<glm::vec3>, std::string> AssimpImporter::GetVertexTangents(const std::string &meshName) {
        if (!_meshNameToIndex.contains(meshName)) {
            return std::unexpected("Mesh not found: " + meshName);
        }
        const auto mesh = _scene->mMeshes[_meshNameToIndex[meshName]];
        if (mesh->mTangents == nullptr) {
            return std::unexpected("Mesh does not contain tangents: " + meshName);
        }
        std::vector<glm::vec3> tangents;
        tangents.reserve(mesh->mNumVertices);
        for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
            const auto& tangent = mesh->mTangents[i];
            tangents.emplace_back(tangent.x, tangent.y, tangent.z);
        }
        return tangents;
    }

    inline std::expected<std::vector<glm::vec3>, std::string> AssimpImporter::GetVertexBitangents(const std::string &meshName) {
        if (!_meshNameToIndex.contains(meshName)) {
            return std::unexpected("Mesh not found: " + meshName);
        }
        const auto mesh = _scene->mMeshes[_meshNameToIndex[meshName]];
        if (mesh->mBitangents == nullptr) {
            return std::unexpected("Mesh does not contain bitangents: " + meshName);
        }
        std::vector<glm::vec3> bitangents;
        bitangents.reserve(mesh->mNumVertices);
        for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
            const auto& bitangent = mesh->mBitangents[i];
            bitangents.emplace_back(bitangent.x, bitangent.y, bitangent.z);
        }
        return bitangents;
    }

    inline std::expected<std::vector<glm::vec3>, std::string> AssimpImporter::GetVertexColors(const std::string &meshName, const glm::u32 id) {
        if (!_meshNameToIndex.contains(meshName)) {
            return std::unexpected("Mesh not found: " + meshName);
        }
        if (id >= AI_MAX_NUMBER_OF_COLOR_SETS) {
            return std::unexpected("Invalid color set ID: " + std::to_string(id));
        }
        const auto mesh = _scene->mMeshes[_meshNameToIndex[meshName]];
        if (mesh->mColors[id] == nullptr) {
            return std::unexpected("Mesh does not contain color set " + std::to_string(id) + ": " + meshName);
        }
        std::vector<glm::vec3> colors;
        colors.reserve(mesh->mNumVertices);
        for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
            const auto& color = mesh->mColors[id][i];
            colors.emplace_back(color.r, color.g, color.b);
        }
        return colors;
    }

    inline std::expected<std::vector<glm::vec2>, std::string> AssimpImporter::GetVertexUVs(const std::string &meshName, glm::u32 id) {
        if (!_meshNameToIndex.contains(meshName)) {
            return std::unexpected("Mesh not found: " + meshName);
        }
        if (id >= AI_MAX_NUMBER_OF_TEXTURECOORDS) {
            return std::unexpected("Invalid UV set ID: " + std::to_string(id));
        }
        const auto mesh = _scene->mMeshes[_meshNameToIndex[meshName]];
        if (mesh->mTextureCoords[id] == nullptr) {
            return std::unexpected("Mesh does not contain UV set " + std::to_string(id) + ": " + meshName);
        }
        std::vector<glm::vec2> uvs;
        uvs.reserve(mesh->mNumVertices);
        for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
            const auto& uv = mesh->mTextureCoords[id][i];
            uvs.emplace_back(uv.x, uv.y);
        }
        return uvs;
    }

    inline std::expected<std::pair<std::vector<glm::vec4>, std::vector<glm::uvec4>>, std::string> AssimpImporter::GetBoneData(const std::string &meshName) {
        if (!_meshNameToIndex.contains(meshName)) {
            return std::unexpected("Mesh not found: " + meshName);
        }
        const auto mesh = _scene->mMeshes[_meshNameToIndex[meshName]];
        if (mesh->mBones == nullptr) {
            return std::unexpected("Mesh does not contain bones: " + meshName);
        }
        std::vector<glm::u32> bonesPerVertex(mesh->mNumVertices, 0);
        std::vector boneWeights(mesh->mNumVertices, glm::vec4(0.0f));
        std::vector boneIDs(mesh->mNumVertices, glm::uvec4(0));
        for (unsigned int i = 0; i < mesh->mNumBones; ++i) {
            const auto& bone = mesh->mBones[i];
            for (unsigned int j = 0; j < bone->mNumWeights; ++j) {
                const auto& weight = bone->mWeights[j];
                boneWeights[weight.mVertexId][bonesPerVertex[weight.mVertexId]] = weight.mWeight;
                boneIDs[weight.mVertexId][bonesPerVertex[weight.mVertexId]] = i;
                bonesPerVertex[weight.mVertexId]++;
            }
        }
        return std::make_pair(std::move(boneWeights), std::move(boneIDs));
    }

    inline std::expected<std::vector<glm::u32>, std::string> AssimpImporter::GetIndices(const std::string &meshName) {
        if (!_meshNameToIndex.contains(meshName)) {
            return std::unexpected("Mesh not found: " + meshName);
        }
        std::vector<glm::u32> indices;
        const auto mesh = _scene->mMeshes[_meshNameToIndex[meshName]];
        indices.reserve(mesh->mNumFaces * 3);
        for (unsigned int i = 0; i < mesh->mNumFaces; ++i) {
            const auto& face = mesh->mFaces[i];
            for (unsigned int j = 0; j < face.mNumIndices; ++j) {
                indices.push_back(face.mIndices[j]);
            }
        }
        return indices;
    }

    inline std::expected<std::vector<glm::mat4>, std::string> AssimpImporter::GetBoneTransformationMatrices() {
        std::vector<glm::mat4> boneMatrices;
        boneMatrices.reserve(_scene->mNumMeshes);
        for (unsigned int i = 0; i < _scene->mNumSkeletons; ++i) {
            const auto& skeleton = _scene->mSkeletons[i];
            for (unsigned int j = 0; j < skeleton->mNumBones; ++j) {
                const auto& bone = skeleton->mBones[j];
                boneMatrices.emplace_back(bone->mOffsetMatrix.a1, bone->mOffsetMatrix.a2, bone->mOffsetMatrix.a3, bone->mOffsetMatrix.a4,
                                          bone->mOffsetMatrix.b1, bone->mOffsetMatrix.b2, bone->mOffsetMatrix.b3, bone->mOffsetMatrix.b4,
                                          bone->mOffsetMatrix.c1, bone->mOffsetMatrix.c2, bone->mOffsetMatrix.c3, bone->mOffsetMatrix.c4,
                                          bone->mOffsetMatrix.d1, bone->mOffsetMatrix.d2, bone->mOffsetMatrix.d3, bone->mOffsetMatrix.d4);
            }
        }
        return boneMatrices;
    }

    inline AssimpImporter::~AssimpImporter() {
    }
}
