//
// Created by radue on 4/1/2026.
//

module;

#include <assimp/Importer.hpp>
#include <assimp/Exporter.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/GltfMaterial.h>

#include <glm/glm.hpp>

export module gfx:assimpImporter;
import :importer;

import std;

namespace gfx {
    export class AssimpImporter : public gfx::Importer {
    public:
        explicit AssimpImporter(const std::filesystem::path &path);

        std::vector<std::string> GetMeshNames() override;
        std::vector<std::string> GetMaterialNames() override;

        Mesh GetMesh(const std::string &meshName) override;
        Material GetMaterial(const std::string &materialName) override;

        Importer::Node GetNode(const aiNode *node) const;

        Scene LoadScene() override;

        std::expected<std::vector<glm::mat4>, std::string> GetBoneTransformationMatrices() override;

    private:
        Assimp::Importer _importer = {};
        Assimp::Exporter _exporter = {};
        const aiScene* _scene;
        std::unordered_map<std::string, glm::u32> _meshNameToIndex;
        std::unordered_map<glm::u32, std::string> _indexToMeshName;

        std::unordered_map<std::string, glm::u32> _materialNameToIndex;
        std::unordered_map<glm::u32, std::string> _indexToMaterialName;
    };

    inline AssimpImporter::AssimpImporter(const std::filesystem::path &path) {
        _path = path;
        _scene = _importer.ReadFile(path.string(), aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_SortByPType | aiProcess_GenBoundingBoxes);
        std::string format = path.extension().string();

        if (_scene->HasMeshes() && !_scene->mMeshes[0]->HasTextureCoords(0)) {
            _importer.ApplyPostProcessing(aiProcess_GenUVCoords);
        }
        _importer.ApplyPostProcessing(aiProcess_FlipUVs);

        if (_scene->HasMeshes() && !_scene->mMeshes[0]->HasNormals()) {
            _importer.ApplyPostProcessing(aiProcess_GenSmoothNormals);
        }

        if (_scene->HasMeshes() && !_scene->mMeshes[0]->HasTangentsAndBitangents()) {
            _importer.ApplyPostProcessing(aiProcess_CalcTangentSpace);
        }

        if (_scene == nullptr) {
            std::cerr << "Could not read file: " << path.string() << std::endl;
            return;
        }

        for (unsigned int i = 0; i < _scene->mNumMeshes; ++i) {
            const std::string meshName = _scene->mMeshes[i]->mName.C_Str();
            _meshNameToIndex[meshName] = i;
            _indexToMeshName[i] = meshName;
        }

        for (unsigned int i = 0; i < _scene->mNumMaterials; ++i) {
            const std::string materialName = _scene->mMaterials[i]->GetName().C_Str();
            _materialNameToIndex[materialName] = i;
            _indexToMaterialName[i] = materialName;
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

    inline std::vector<std::string> AssimpImporter::GetMaterialNames() {
        std::vector<std::string> materialNames;
        materialNames.reserve(_scene->mNumMaterials);
        for (unsigned int i = 0; i < _scene->mNumMaterials; ++i) {
            materialNames.emplace_back(_scene->mMaterials[i]->GetName().C_Str());
        }
        return materialNames;
    }

    inline Importer::Mesh AssimpImporter::GetMesh(const std::string &meshName) {
        const auto meshIndexIt = _meshNameToIndex.find(meshName);
        if (meshIndexIt == _meshNameToIndex.end()) {
            throw std::runtime_error("Mesh not found: " + meshName);
        }
        const auto meshIndex = meshIndexIt->second;
        const auto& mesh = _scene->mMeshes[meshIndex];

        Mesh result;
        result.positions.reserve(mesh->mNumVertices);
        for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
            result.positions.emplace_back(mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z);
        }

        if (mesh->HasNormals()) {
            std::vector<glm::vec3> normals;
            normals.reserve(mesh->mNumVertices);
            for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
                normals.emplace_back(mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z);
            }
            result.normals = std::move(normals);
        }

        if (mesh->HasTangentsAndBitangents()) {
            std::vector<glm::vec3> tangents;
            tangents.reserve(mesh->mNumVertices);
            std::vector<glm::vec3> bitangents;
            bitangents.reserve(mesh->mNumVertices);
            for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
                tangents.emplace_back(mesh->mTangents[i].x, mesh->mTangents[i].y, mesh->mTangents[i].z);
                bitangents.emplace_back(mesh->mBitangents[i].x, mesh->mBitangents[i].y, mesh->mBitangents[i].z);
            }
            result.tangents = std::move(tangents);
            result.bitangents = std::move(bitangents);
        }

        for (unsigned int i = 0; i < AI_MAX_NUMBER_OF_TEXTURECOORDS; ++i) {
            if (mesh->HasTextureCoords(i)) {
                std::vector<glm::vec2> vertexUVs;
                vertexUVs.reserve(mesh->mNumVertices);
                for (unsigned int j = 0; j < mesh->mNumVertices; ++j) {
                    vertexUVs.emplace_back(mesh->mTextureCoords[i][j].x, mesh->mTextureCoords[i][j].y);
                }
                result.vertexUVs[i] = std::move(vertexUVs);
            }
        }

        for (unsigned int i = 0; i < AI_MAX_NUMBER_OF_COLOR_SETS; ++i) {
            if (mesh->HasVertexColors(i)) {
                std::vector<glm::vec3> vertexColors;
                vertexColors.reserve(mesh->mNumVertices);
                for (unsigned int j = 0; j < mesh->mNumVertices; ++j) {
                    vertexColors.emplace_back(mesh->mColors[i][j].r, mesh->mColors[i][j].g, mesh->mColors[i][j].b);
                }
                result.vertexColors[i] = std::move(vertexColors);
            }
        }

        if (mesh->HasBones()) {
            std::vector boneWeights(mesh->mNumVertices, glm::vec4(0.f));
            std::vector boneIDs(mesh->mNumVertices, glm::uvec4(0));
            for (unsigned int i = 0; i < mesh->mNumBones; ++i) {
                const auto& bone = mesh->mBones[i];
                for (unsigned int j = 0; j < bone->mNumWeights; ++j) {
                    const auto& weight = bone->mWeights[j];
                    const unsigned int vertexID = weight.mVertexId;
                    const float weightValue = weight.mWeight;

                    // Find the first available slot for this vertex
                    for (int k = 0; k < 4; ++k) {
                        if (boneWeights[vertexID][k] == 0.f) {
                            boneWeights[vertexID][k] = weightValue;
                            boneIDs[vertexID][k] = i;
                            break;
                        }
                    }
                }
            }
        }

        if (mesh->HasFaces()) {
            std::vector<glm::u32> indices;
            indices.reserve(mesh->mNumFaces * 3); // Assuming triangulated mesh
            for (unsigned int i = 0; i < mesh->mNumFaces; ++i) {
                const auto& face = mesh->mFaces[i];
                if (face.mNumIndices != 3) {
                    throw std::runtime_error("Non-triangulated face found in mesh: " + meshName);
                }
                indices.emplace_back(face.mIndices[0]);
                indices.emplace_back(face.mIndices[1]);
                indices.emplace_back(face.mIndices[2]);
            }
            result.indices = std::move(indices);
        }

        result.name = meshName;

        return result;
    }

    inline Importer::Material AssimpImporter::GetMaterial(const std::string &materialName) {
        const auto materialIndexIt = _materialNameToIndex.find(materialName);
        if (materialIndexIt == _materialNameToIndex.end()) {
            throw std::runtime_error("Material not found: " + materialName);
        }
        const auto materialIndex = materialIndexIt->second;
        const auto& material = _scene->mMaterials[materialIndex];

        Material result;

        aiColor4D baseColorFactor;
        if (material->Get(AI_MATKEY_BASE_COLOR, baseColorFactor) == AI_SUCCESS) {
            result.baseColorFactor = glm::vec4(baseColorFactor.r, baseColorFactor.g, baseColorFactor.b, baseColorFactor.a);
        }

        aiColor3D emissiveFactor;
        if (material->Get(AI_MATKEY_COLOR_EMISSIVE, emissiveFactor) == AI_SUCCESS) {
            result.emissiveFactor = glm::vec4(emissiveFactor.r, emissiveFactor.g, emissiveFactor.b, 1.f);
        }

        float roughness;
        if (material->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS) {
            result.roughness = roughness;
        }

        float metallic;
        if (material->Get(AI_MATKEY_METALLIC_FACTOR, metallic) == AI_SUCCESS) {
            result.metallic = metallic;
        }

        int doubleSided;
        if (material->Get(AI_MATKEY_TWOSIDED, doubleSided) == AI_SUCCESS) {
            result.doubleSided = doubleSided != 0;
        }

        // glTF alpha mode: "OPAQUE" → cutoff 1.0 (default, discard nothing)
        //                  "MASK"   → use AI_MATKEY_GLTF_ALPHACUTOFF (default 0.5)
        //                  "BLEND"  → cutoff 0.0 (no discard; blending handles transparency)
        // The engine convention: alphaCutoff == 0.0 → blended draw bucket,
        //                        0 < alphaCutoff < 1 → masked/two-faced bucket,
        //                        alphaCutoff == 1.0 → fully opaque bucket.
        aiString alphaMode;
        if (material->Get(AI_MATKEY_GLTF_ALPHAMODE, alphaMode) == AI_SUCCESS) {
            std::string mode(alphaMode.C_Str());
            if (mode == "BLEND") {
                result.alphaCutoff = 0.0f;          // blended — never discard
            } else if (mode == "MASK") {
                float cutoff = 0.5f;
                material->Get(AI_MATKEY_GLTF_ALPHACUTOFF, cutoff);
                result.alphaCutoff = cutoff;        // masked — discard below threshold
            } else {
                result.alphaCutoff = 1.0f;          // OPAQUE — fully opaque bucket
            }
        } else {
            // Non-glTF fallback: use AI_MATKEY_OPACITY (0=transparent … 1=opaque)
            // Map opacity < 1 → blended (0.0 cutoff), opacity == 1 → opaque (1.0 cutoff)
            float opacity = 1.0f;
            material->Get(AI_MATKEY_OPACITY, opacity);
            result.alphaCutoff = (opacity < 1.0f) ? 0.0f : 1.0f;
        }

        aiString texName;
        if (material->GetTextureCount(aiTextureType_BASE_COLOR) > 0) {
            if (material->GetTexture(aiTextureType_BASE_COLOR, 0, &texName) == AI_SUCCESS) {
                result.albedoTexturePath = _path.parent_path() / std::filesystem::path(texName.C_Str());
            }
        }

        if (material->GetTextureCount(aiTextureType_EMISSIVE) > 0) {
            if (material->GetTexture(aiTextureType_EMISSIVE, 0, &texName) == AI_SUCCESS) {
                result.emissiveTexturePath = _path.parent_path() / std::filesystem::path(texName.C_Str());
            }
        }
        if (material->GetTextureCount(aiTextureType_EMISSION_COLOR) > 0) {
            if (material->GetTexture(aiTextureType_EMISSION_COLOR, 0, &texName) == AI_SUCCESS) {
                result.emissiveTexturePath = _path.parent_path() / std::filesystem::path(texName.C_Str());
            }
        }

        // AMBIENT, DIFFUSE, SPECULAR AND SHININESS
        {
            if (material->GetTexture(aiTextureType_AMBIENT, 0, &texName) == AI_SUCCESS) {
                if (material->GetTexture(aiTextureType_AMBIENT, 0, &texName) == AI_SUCCESS) {
                    result.ambientTexturePath = _path.parent_path() / std::filesystem::path(texName.C_Str());
                }
            }
            if (material->GetTextureCount(aiTextureType_DIFFUSE) > 0) {
                if (material->GetTexture(aiTextureType_DIFFUSE, 0, &texName) == AI_SUCCESS) {
                    result.diffuseTexturePath = _path.parent_path() / std::filesystem::path(texName.C_Str());
                }
            }
            if (material->GetTextureCount(aiTextureType_SPECULAR) > 0) {
                if (material->GetTexture(aiTextureType_SPECULAR, 0, &texName) == AI_SUCCESS) {
                    result.specularTexturePath = _path.parent_path() / std::filesystem::path(texName.C_Str());
                }
            }
            if (material->GetTextureCount(aiTextureType_SHININESS) > 0) {
                if (material->GetTexture(aiTextureType_SHININESS, 0, &texName) == AI_SUCCESS) {
                    result.roughnessTexturePath = _path.parent_path() / std::filesystem::path(texName.C_Str());
                }
            }
        }

        // NORMAL AND DISPLACEMENT/HEIGHT
        {
            if (material->GetTextureCount(aiTextureType_NORMALS) > 0) {
                if (material->GetTexture(aiTextureType_NORMALS, 0, &texName) == AI_SUCCESS) {
                    result.normalTexturePath = _path.parent_path() / std::filesystem::path(texName.C_Str());
                }
            }
            if (material->GetTextureCount(aiTextureType_NORMAL_CAMERA) > 0) {
                if (material->GetTexture(aiTextureType_NORMAL_CAMERA, 0, &texName) == AI_SUCCESS) {
                    result.normalTexturePath = _path.parent_path() / std::filesystem::path(texName.C_Str());
                }
            }
            if (material->GetTextureCount(aiTextureType_DISPLACEMENT) > 0) {
                if (material->GetTexture(aiTextureType_DISPLACEMENT, 0, &texName) == AI_SUCCESS) {
                    result.displacementTexturePath = _path.parent_path() / std::filesystem::path(texName.C_Str());
                }
            }
            if (material->GetTextureCount(aiTextureType_HEIGHT) > 0) {
                if (material->GetTexture(aiTextureType_HEIGHT, 0, &texName) == AI_SUCCESS) {
                    result.displacementTexturePath = _path.parent_path() / std::filesystem::path(texName.C_Str());
                }
            }
        }

        // METALLIC, ROUGHNESS, AMBIENT OCCLUSION / LIGHTMAP
        {
            if (material->GetTextureCount(aiTextureType_METALNESS) > 0) {
                if (material->GetTexture(aiTextureType_METALNESS, 0, &texName) == AI_SUCCESS) {
                    result.metallicTexturePath = _path.parent_path() / std::filesystem::path(texName.C_Str());
                }
            }
            if (material->GetTextureCount(aiTextureType_DIFFUSE_ROUGHNESS) > 0) {
                if (material->GetTexture(aiTextureType_DIFFUSE_ROUGHNESS, 0, &texName) == AI_SUCCESS) {
                    result.roughnessTexturePath = _path.parent_path() / std::filesystem::path(texName.C_Str());
                }
            }

            if (material->GetTextureCount(aiTextureType_AMBIENT_OCCLUSION) > 0) {
                if (material->GetTexture(aiTextureType_AMBIENT_OCCLUSION, 0, &texName) == AI_SUCCESS) {
                    result.ambientOcclusionTexturePath = _path.parent_path() / std::filesystem::path(texName.C_Str());
                }
            }
            if (material->GetTextureCount(aiTextureType_LIGHTMAP) > 0) {
                if (material->GetTexture(aiTextureType_LIGHTMAP, 0, &texName) == AI_SUCCESS) {
                    result.ambientOcclusionTexturePath = _path.parent_path() / std::filesystem::path(texName.C_Str());
                }
            }
        }
        result.name = materialName;

        return result;
    }

    inline Importer::Node AssimpImporter::GetNode(const aiNode* node) const {
        Importer::Node result;
        if (node->mNumMeshes > 0) {
            result.meshIndices.reserve(node->mNumMeshes);
            result.materialIndices.reserve(node->mNumMeshes);
            for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
                result.meshIndices.emplace_back(node->mMeshes[i]);
                result.materialIndices.emplace_back(_scene->mMeshes[node->mMeshes[i]]->mMaterialIndex);
            }
        }
        aiVector3D position;
        aiVector3D rotation;
        aiVector3D scale;
        node->mTransformation.Decompose(scale, rotation, position);
        result.position = glm::vec3(position.x, position.y, position.z);
        result.rotation = glm::degrees(glm::vec3(rotation.x, rotation.y, rotation.z));
        result.scale = glm::vec3(scale.x, scale.y, scale.z);

        result.aabb = {
            .min = glm::vec3(std::numeric_limits<float>::max()),
            .max = glm::vec3(std::numeric_limits<float>::min())
        };
        for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
            const auto& mesh = _scene->mMeshes[node->mMeshes[i]];
            auto meshAABB = mesh->mAABB;
            result.aabb.min = glm::min(result.aabb.min, glm::vec3(meshAABB.mMin.x, meshAABB.mMin.y, meshAABB.mMin.z));
            result.aabb.max = glm::max(result.aabb.max, glm::vec3(meshAABB.mMax.x, meshAABB.mMax.y, meshAABB.mMax.z));
        }
        if (node->mNumMeshes == 0) {
            result.aabb.min = glm::vec3(0.f);
            result.aabb.max = glm::vec3(0.f);
        }

        result.name = node->mName.C_Str();


        return result;
    }

    inline Importer::Scene AssimpImporter::LoadScene() {
        Scene result;

        result.meshes.reserve(_scene->mNumMeshes);
        for (unsigned int i = 0; i < _scene->mNumMeshes; ++i) {
            const std::string meshName = _scene->mMeshes[i]->mName.C_Str();
            result.meshes.emplace_back(GetMesh(meshName));
        }

        result.materials.reserve(_scene->mNumMaterials);
        for (unsigned int i = 0; i < _scene->mNumMaterials; ++i) {
            const std::string materialName = _scene->mMaterials[i]->GetName().C_Str();
            result.materials.emplace_back(GetMaterial(materialName));
        }

        std::queue<std::pair<aiNode*, glm::u32>> nodeQueue;
        nodeQueue.emplace(_scene->mRootNode, -1);
        while (!nodeQueue.empty()) {
            const auto [currentNode, parentIndex] = nodeQueue.front();
            nodeQueue.pop();

            const std::string nodeName = currentNode->mName.C_Str();
            const auto nodeIndex = static_cast<glm::u32>(result.nodes.size());
            result.nodes.emplace_back(GetNode(currentNode));
            if (parentIndex != -1) {
                result.nodes[parentIndex].childIndices.emplace_back(nodeIndex);
            }

            for (unsigned int i = 0; i < currentNode->mNumChildren; ++i) {
                nodeQueue.emplace(currentNode->mChildren[i], nodeIndex);
            }
        }

        return result;
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
}

