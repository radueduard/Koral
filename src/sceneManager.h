//
// Created by radue on 3/15/2026.
//

#pragma once
#include <filesystem>

#include "io/manager.h"

#include <scene.h>
#include <windows.h>

using CreateSceneFn = gfx::Scene*(*)();

namespace gfx
{
    class SceneManager
    {
    public:
        static void LoadScene(const std::filesystem::path& path)
        {
            if (!std::filesystem::exists(path)) {
                throw std::runtime_error("Scene file does not exist: " + path.string());
            }

            const HMODULE module = LoadLibraryW(path.wstring().c_str());
            if (!module) {
                throw std::runtime_error("Failed to load DLL");
            }

            // load the SimpleTriangle class and initialize it
            const auto createScene = reinterpret_cast<CreateSceneFn>(
                GetProcAddress(module, "CreateScene")
            );

            auto scene = std::unique_ptr<gfx::Scene>(createScene());
            io::Window::Builder(std::move(scene))
                .setTitle("Simple Triangle")
                .setExtent({ 1280, 720 })
                .setResizable(true)
                .setFullscreen(false)
                .setAPI(API::eOpenGL)
                .build();
        }
    };
}
