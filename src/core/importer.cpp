//
// Created by radue on 2/23/2026.
//

#include "../../include/importer.h"

#include <stb_image.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include "../../include/image.h"
#include "../../include/mesh.h"

namespace gfx
{
    Importer::Importer(std::filesystem::path path)
    {
        constexpr auto flags = aiProcess_Triangulate
            | aiProcess_FlipUVs
            | aiProcess_CalcTangentSpace
            | aiProcess_JoinIdenticalVertices
            | aiProcess_OptimizeMeshes;
        _scene = _importer.ReadFile(path.string(), flags);
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

        image->CopyFrom(*stagingBuffer, 0, 0);

        return image;
    }

    Importer::~Importer()
    {
        _importer.FreeScene();
    }
}
