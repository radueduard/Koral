//
// Created by radue on 2/23/2026.
//

#pragma once

#include <filesystem>
#include <memory>
#include <assimp/Importer.hpp>

#include "api.h"

namespace gfx
{
    class Image;

    class GFX_API Importer
    {
    public:
        static std::unique_ptr<Image> LoadImage(const std::filesystem::path& path);

        explicit Importer(std::filesystem::path path);
        ~Importer();

        [[nodiscard]] const aiScene* GetScene() const { return _scene; }

    protected:
        Assimp::Importer _importer = {};
        const aiScene* _scene;
    };
}

