//
// Created by radue on 3/13/2026.
//

#pragma once
#include <assimp/scene.h>

#include "core/mesh.h"
#include <glm/glm.hpp>

#include "core/importer.h"

namespace gfx
{
    class PBRMesh final : public CustomMesh<PBRMesh>
    {
    public:
        struct Vertex
        {
            glm::vec3 position;
            glm::vec3 normal;
            glm::vec3 tangent;
            glm::vec3 bitangent;
            glm::vec2 uv;
        };


        static void DefineMesh()
        {
            _vertexBindingDescription = {
                { 0, sizeof(Vertex) }
            };

            _vertexAttributeDescription = {
                { 0, 0, 3, ChannelType::eFloat, offsetof(Vertex, position) },
                { 1, 0, 3, ChannelType::eFloat, offsetof(Vertex, normal) },
                { 2, 0, 3, ChannelType::eFloat, offsetof(Vertex, tangent) },
                { 3, 0, 3, ChannelType::eFloat, offsetof(Vertex, bitangent) },
                { 4, 0, 2, ChannelType::eFloat, offsetof(Vertex, uv) },
            };
        }

        explicit PBRMesh(Builder& createInfo) : CustomMesh(createInfo) {}

        static std::unique_ptr<PBRMesh> ImportFromFile(const std::filesystem::path& path)
        {
            const Importer importer(path);
            auto scene = importer.GetScene();
            Builder builder;

            // Load vertex buffer
            {
                std::vector<Vertex> vertices;
                for (unsigned int i = 0; i < scene->mNumMeshes; ++i)
                {
                    const auto& mesh = scene->mMeshes[i];
                    for (unsigned int j = 0; j < mesh->mNumVertices; ++j)
                    {
                        Vertex vertex {};
                        vertex.position = { mesh->mVertices[j].x, mesh->mVertices[j].y, mesh->mVertices[j].z };
                        vertex.normal = { mesh->mNormals[j].x, mesh->mNormals[j].y, mesh->mNormals[j].z };
                        vertex.tangent = { mesh->mTangents[j].x, mesh->mTangents[j].y, mesh->mTangents[j].z };
                        vertex.bitangent = { mesh->mBitangents[j].x, mesh->mBitangents[j].y, mesh->mBitangents[j].z };
                        if (mesh->HasTextureCoords(0))
                        {
                            vertex.uv = { mesh->mTextureCoords[0][j].x, mesh->mTextureCoords[0][j].y };
                        }
                        vertices.push_back(vertex);
                    }
                }

                auto vertexBuffer = Buffer::Builder()
                    .setSize(vertices.size() * sizeof(Vertex))
                    .setUsage(Buffer::Usage::eVertex)
                    .addUsage(Buffer::Usage::eTransferDst)
                    .setMemoryProperties(Buffer::MemoryProperty::eDeviceLocal)
                    .build();

                const auto stagingBuffer = Buffer::Builder()
                    .setSize(vertices.size() * sizeof(Vertex))
                    .setUsage(Buffer::Usage::eTransferSrc)
                    .addMemoryProperty(Buffer::MemoryProperty::eHostVisible)
                    .addMemoryProperty(Buffer::MemoryProperty::eHostCoherent)
                    .build();

                stagingBuffer->Map();
                stagingBuffer->Write(std::span<Vertex> { vertices });
                stagingBuffer->Unmap();

                vertexBuffer->CopyFrom(*stagingBuffer, 0, 0, vertices.size() * sizeof(Vertex));
                builder.SetVertexBuffer(0, std::move(vertexBuffer));
            }

            // Load index buffer
            {
                std::vector<unsigned int> indices;
                for (unsigned int i = 0; i < scene->mNumMeshes; ++i)
                {
                    const auto& mesh = scene->mMeshes[i];
                    for (unsigned int j = 0; j < mesh->mNumFaces; ++j)
                    {
                        const auto& face = mesh->mFaces[j];
                        for (unsigned int k = 0; k < face.mNumIndices; ++k)
                        {
                            indices.push_back(face.mIndices[k]);
                        }
                    }
                }

                auto indexBuffer = Buffer::Builder()
                    .setSize(indices.size() * sizeof(unsigned int))
                    .setUsage(Buffer::Usage::eIndex)
                    .addUsage(Buffer::Usage::eTransferDst)
                    .setMemoryProperties(Buffer::MemoryProperty::eDeviceLocal)
                    .build();

                const auto stagingBuffer = Buffer::Builder()
                    .setSize(indices.size() * sizeof(unsigned int))
                    .setUsage(Buffer::Usage::eTransferSrc)
                    .addMemoryProperty(Buffer::MemoryProperty::eHostVisible)
                    .addMemoryProperty(Buffer::MemoryProperty::eHostCoherent)
                    .build();

                stagingBuffer->Map();
                stagingBuffer->Write(std::span { indices });
                stagingBuffer->Unmap();


                indexBuffer->CopyFrom(*stagingBuffer, 0, 0, indices.size() * sizeof(unsigned int));
                builder.SetIndexBuffer(std::move(indexBuffer), ChannelType::eUInt);
            }

            return builder.Build();
        }
    };
}
