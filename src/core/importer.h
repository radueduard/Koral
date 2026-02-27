//
// Created by radue on 2/23/2026.
//

#pragma once

#include <filesystem>
#include <memory>
#include <assimp/Importer.hpp>

#include "mesh.h"

namespace gfx
{
    class Importer
    {
    public:
        static std::unique_ptr<Mesh> LoadMesh(const std::filesystem::path& path);
        static std::unique_ptr<Image> LoadImage(const std::filesystem::path& path);

        ~Importer();
    protected:
        Assimp::Importer _importer = {};
        explicit Importer(std::filesystem::path path);

        const aiScene* _scene;
    };
}

