//
// Created by radue on 3/15/2026.
//

#pragma once
#include <filesystem>
#include <optional>


#include <scene.h>
#include <job.h>
#include <projectConfig.h>

#include "context.h"
#include "window.h"

#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <dlfcn.h>
#endif


using CreateSceneFn = kor::Scene*(*)();
using CreateJobFn = kor::Job*(*)();
using CreateConfigFn = kor::ProjectConfig*(*)();

namespace kor
{
    class SceneManager
    {
    public:
        // Reads an optional project config exported by the scene library (CreateProjectConfig).
        // Returns nullopt if the symbol is absent, so the engine can fall back to defaults.
        static std::optional<ProjectConfig> LoadConfig(const std::filesystem::path& path)
        {
            if (!std::filesystem::exists(path)) return std::nullopt;

#ifdef _WIN32
            const HMODULE module = LoadLibraryW(path.wstring().c_str());
            if (!module) return std::nullopt;
            const auto createConfig = reinterpret_cast<CreateConfigFn>(GetProcAddress(module, "CreateProjectConfig"));
#elif defined(__linux__) || defined(__APPLE__)
            void* module = dlopen(path.string().c_str(), RTLD_LAZY);
            if (!module) return std::nullopt;
            const auto createConfig = reinterpret_cast<CreateConfigFn>(dlsym(module, "CreateProjectConfig"));
#endif
            if (!createConfig) return std::nullopt;
            ProjectConfig* raw = createConfig();
            if (!raw) return std::nullopt;
            ProjectConfig config = *raw;
            delete raw;
            return config;
        }

        static std::unique_ptr<Scene> LoadScene(const std::filesystem::path& path)
        {
            if (!std::filesystem::exists(path)) {
                throw std::runtime_error("Scene file does not exist: " + path.string());
            }

#ifdef _WIN32
            const HMODULE module = LoadLibraryW(path.wstring().c_str());
            if (!module) {
                DWORD error = GetLastError();
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


           return std::unique_ptr<Scene>(createScene());
        }

        // Loads the project's headless entry point (CreateJob), if it exports one.
        // Returns nullptr when the symbol is absent, so the engine can fall back to
        // the windowed Scene path. A library exports either CreateScene or CreateJob.
        static std::unique_ptr<Job> LoadJob(const std::filesystem::path& path)
        {
            if (!std::filesystem::exists(path)) return nullptr;

#ifdef _WIN32
            const HMODULE module = LoadLibraryW(path.wstring().c_str());
            if (!module) return nullptr;
            const auto createJob = reinterpret_cast<CreateJobFn>(GetProcAddress(module, "CreateJob"));
#elif defined(__linux__) || defined(__APPLE__)
            void* module = dlopen(path.string().c_str(), RTLD_LAZY);
            if (!module) return nullptr;
            const auto createJob = reinterpret_cast<CreateJobFn>(dlsym(module, "CreateJob"));
#endif
            if (!createJob) return nullptr;
            return std::unique_ptr<Job>(createJob());
        }
    };
}
