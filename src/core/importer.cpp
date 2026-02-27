//
// Created by radue on 2/23/2026.
//

#include "importer.h"

#include <stb_image.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include "mesh.h"

namespace gfx
{
    Importer::Importer(std::filesystem::path path)
    {
        constexpr auto flags = aiProcess_Triangulate
            | aiProcess_FlipUVs;
        _scene = _importer.ReadFile(path.string(), flags);
    }

    std::unique_ptr<Mesh> Importer::LoadMesh(const std::filesystem::path& path)
    {
        const Importer importer(path);
        Mesh::CreateInfo createInfo;

        // Load positions buffer
        {
            std::vector<float> positions;
            for (unsigned int i = 0; i < importer._scene->mNumMeshes; ++i)
            {
                const auto& mesh = importer._scene->mMeshes[i];
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

            createInfo.AddVertexBuffer(std::move(positionsBuffer), VertexInputBindingDescription{0, 3 * sizeof(float)});
            createInfo.AddVertexAttributeDescription(VertexInputAttributeDescription{0, 0, 3, ChannelType::eFloat, 0});
        }

        // Load normal buffer
        {
            std::vector<float> normals;
            for (unsigned int i = 0; i < importer._scene->mNumMeshes; ++i)
            {
                auto mesh = importer._scene->mMeshes[i];
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

            createInfo.AddVertexBuffer(std::move(uvBuffer), VertexInputBindingDescription{1, 3 * sizeof(float)});
            createInfo.AddVertexAttributeDescription(VertexInputAttributeDescription{1, 1, 3, ChannelType::eFloat, 0});
        }

        // Load uv buffer
        {
            std::vector<float> uvs;
            for (unsigned int i = 0; i < importer._scene->mNumMeshes; ++i)
            {
                auto mesh = importer._scene->mMeshes[i];
                if (!mesh->HasTextureCoords(0)) {
                    continue;
                }
                for (unsigned int j = 0; j < mesh->mNumVertices; ++j)
                {
                    uvs.push_back(mesh->mTextureCoords[0][j].x);
                    uvs.push_back(mesh->mTextureCoords[0][j].y);
                }
            }

            auto normalBuffer = Buffer::Builder()
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

            normalBuffer->CopyFrom(*stagingBuffer, 0, 0, uvs.size() * sizeof(float));

            createInfo.AddVertexBuffer(std::move(normalBuffer), VertexInputBindingDescription{2, 2 * sizeof(float)});
            createInfo.AddVertexAttributeDescription(VertexInputAttributeDescription{2, 2, 2, ChannelType::eFloat, 0});
        }

        // Load index buffer
        {
            std::vector<unsigned int> indices;
            for (unsigned int i = 0; i < importer._scene->mNumMeshes; ++i)
            {
                const auto& mesh = importer._scene->mMeshes[i];
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

            createInfo.SetIndexBuffer(std::move(indexBuffer), ChannelType::eUInt);
        }

        return Mesh::Create(createInfo);
    }

    std::unique_ptr<Image> Importer::LoadImage(const std::filesystem::path& path)
    {
        int width, height, channels;
        const stbi_uc* data = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
        if (!data)
        {
            throw std::runtime_error("Failed to load image: " + path.string());
        }

        auto image = Image::Builder()
            .setType(Image::Type::e2D)
            .setExtent({ static_cast<glm::u32>(width), static_cast<glm::u32>(height) })
            .setFormat(Image::Format::eRGBA8_UNORM)
            .setUsage(Image::Usage::eSampled)
            .addUsage(Image::Usage::eTransferDst)
            .build();

        const auto stagingBuffer = Buffer::Builder()
            .setSize(width * height * 4)
            .setUsage(Buffer::Usage::eTransferSrc)
            .addMemoryProperty(Buffer::MemoryProperty::eHostVisible)
            .addMemoryProperty(Buffer::MemoryProperty::eHostCoherent)
            .build();

        stagingBuffer->Map();
        stagingBuffer->Write(std::span { data, static_cast<unsigned long long>(width * height * 4) });
        stagingBuffer->Unmap();

        image->CopyFrom(*stagingBuffer);

        return image;
    }

    Importer::~Importer()
    {
        _importer.FreeScene();
    }
}
