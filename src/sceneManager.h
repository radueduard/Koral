//
// Created by radue on 3/15/2026.
//

#pragma once
#include <filesystem>

#include "io/manager.h"

#include <scene.h>

#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <dlfcn.h>
#endif


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

#ifdef _WIN32
            const HMODULE module = LoadLibraryW(path.wstring().c_str());
            if (!module) {
                throw std::runtime_error("Failed to load DLL");
            }

            // load the SimpleTriangle class and initialize it
            const auto createScene = reinterpret_cast<CreateSceneFn>(
                GetProcAddress(module, "CreateScene")
            );
#elif defined(__linux__) || defined(__APPLE__)
            const void* module = dlopen(path.string().c_str(), RTLD_LAZY);
            if (!module) {
                throw std::runtime_error("Failed to load shared library: " + std::string(dlerror()));
            }

            const auto createScene = reinterpret_cast<CreateSceneFn>(
                dlsym(const_cast<void*>(module), "CreateScene")
            );
#else
#error "Unsupported platform"
#endif

            io::Window::Builder(std::unique_ptr<Scene>(createScene()))
                .setTitle(path.stem().string())
                .setExtent({ 512, 512 })
                .setResizable(true)
                .setFullscreen(false)
                .setAPI(API::eVulkan)
                .build();
        }
    };
}
