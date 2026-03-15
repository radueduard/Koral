//
// Created by eduard on 10.03.2026.
//

#pragma once
#include <assimp/scene.h>

namespace gfx
{
    class LabMesh final : public CustomMesh<LabMesh>
    {
    public:
        static void DefineMesh()
        {
            _vertexBindingDescription = {
                { 0, sizeof(float) * 3 },
                { 1, sizeof(float) * 3 },
                { 2, sizeof(float) * 2 },
            };

            _vertexAttributeDescription = {
                { 0, 0, 3, ChannelType::eFloat, 0 },
                { 1, 1, 3, ChannelType::eFloat, 0 },
                { 2, 2, 2, ChannelType::eFloat, 0 },
            };
        }

        explicit LabMesh(Builder& createInfo) : CustomMesh(createInfo) {}


        static std::unique_ptr<LabMesh> ImportFromFile(const std::filesystem::path& path)
        {
            const Importer importer(path);
            auto scene = importer.GetScene();
            Builder builder;

            // Load positions buffer
            {
                std::vector<float> positions;
                for (unsigned int i = 0; i < scene->mNumMeshes; ++i)
                {
                    const auto& mesh = scene->mMeshes[i];
                    for (unsigned int j = 0; j < mesh->mNumVertices; ++j)
                    {
                        positions.push_back(mesh->mVertices[j].x);
                        positions.push_back(mesh->mVertices[j].y);
                        positions.push_back(mesh->mVertices[j].z);
                    }
                }

                auto positionsBuffer = Buffer::Builder()
                        .setSize(positions.size() * sizeof(float))
                        .setUsage(Buffer::Usage::eVertex)
                        .addUsage(Buffer::Usage::eTransferDst)
                        .setMemoryProperties(Buffer::MemoryProperty::eDeviceLocal)
                        .build();

                const auto stagingBuffer = Buffer::Builder()
                        .setSize(positions.size() * sizeof(float))
                        .setUsage(Buffer::Usage::eTransferSrc)
                        .addMemoryProperty(Buffer::MemoryProperty::eHostVisible)
                        .addMemoryProperty(Buffer::MemoryProperty::eHostCoherent)
                        .build();

                stagingBuffer->Map();
                stagingBuffer->Write(std::span { positions });
                stagingBuffer->Unmap();

                positionsBuffer->CopyFrom(*stagingBuffer, 0, 0, positions.size() * sizeof(float));

                builder.SetVertexBuffer(0, std::move(positionsBuffer));
            }

            // Load normal buffer
            {
                std::vector<float> normals;
                for (unsigned int i = 0; i < scene->mNumMeshes; ++i)
                {
                    auto mesh = scene->mMeshes[i];
                    if (!mesh->HasNormals()) {
                        continue;
                    }
                    for (unsigned int j = 0; j < mesh->mNumVertices; ++j)
                    {
                        normals.push_back(mesh->mNormals[j].x);
                        normals.push_back(mesh->mNormals[j].y);
                        normals.push_back(mesh->mNormals[j].z);
                    }
                }

                auto uvBuffer = Buffer::Builder()
                        .setSize(normals.size() * sizeof(float))
                        .setUsage(Buffer::Usage::eVertex)
                        .addUsage(Buffer::Usage::eTransferDst)
                        .setMemoryProperties(Buffer::MemoryProperty::eDeviceLocal)
                        .build();

                const auto stagingBuffer = Buffer::Builder()
                        .setSize(normals.size() * sizeof(float))
                        .setUsage(Buffer::Usage::eTransferSrc)
                        .addMemoryProperty(Buffer::MemoryProperty::eHostVisible)
                        .addMemoryProperty(Buffer::MemoryProperty::eHostCoherent)
                        .build();

                stagingBuffer->Map();
                stagingBuffer->Write(std::span { normals } );
                stagingBuffer->Unmap();

                uvBuffer->CopyFrom(*stagingBuffer, 0, 0, normals.size() * sizeof(float));

                builder.SetVertexBuffer(1, std::move(uvBuffer));
            }

            // Load uv buffer
            {
                std::vector<float> uvs;
                for (unsigned int i = 0; i < scene->mNumMeshes; ++i)
                {
                    auto mesh = scene->mMeshes[i];
                    if (!mesh->HasTextureCoords(0)) {
                        continue;
                    }
                    for (unsigned int j = 0; j < mesh->mNumVertices; ++j)
                    {
                        uvs.push_back(mesh->mTextureCoords[0][j].x);
                        uvs.push_back(mesh->mTextureCoords[0][j].y);
                    }
                }

                auto uvsBuffer = Buffer::Builder()
                        .setSize(uvs.size() * sizeof(float))
                        .setUsage(Buffer::Usage::eVertex)
                        .addUsage(Buffer::Usage::eTransferDst)
                        .setMemoryProperties(Buffer::MemoryProperty::eDeviceLocal)
                        .build();

                const auto stagingBuffer = Buffer::Builder()
                        .setSize(uvs.size() * sizeof(float))
                        .setUsage(Buffer::Usage::eTransferSrc)
                        .addMemoryProperty(Buffer::MemoryProperty::eHostVisible)
                        .addMemoryProperty(Buffer::MemoryProperty::eHostCoherent)
                        .build();

                stagingBuffer->Map();
                stagingBuffer->Write(std::span { uvs });
                stagingBuffer->Unmap();

                uvsBuffer->CopyFrom(*stagingBuffer, 0, 0, uvs.size() * sizeof(float));

                builder.SetVertexBuffer(2, std::move(uvsBuffer));
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
