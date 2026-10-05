//
// Created by radue on 2/17/2026.
//

#include <cstdlib>
#include <chrono>
#include <expected>
#include <filesystem>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>
#include <GLFW/glfw3.h>

#include "app.h"
#include "framebuffer.h"
#include "imageView.h"
#include "log.h"
#include "module.h"
#include "projectConfig.h"
#include "sceneManager.h"
#include "scheduler.h"
#include "shader.h"
#include "core/tokenState.h"

#undef LoadLibrary   // sceneManager.h brings in <windows.h>; this file means App::LoadLibrary

namespace kor
{
    class Engine
    {
    public:
        static int Run(int argc, char** argv);
    };

    int Engine::Run(const int argc, char** argv)
    {
        const std::filesystem::path scenePath = argv[1];
        const std::vector<std::string> args(argv + 2, argv + argc);

        // Three layers, weakest first. The library's own CreateProjectConfig is what the project was
        // compiled to want; koral.json is what its author configured without recompiling; the flags
        // are what this one run overrides. See projectConfig.h.
        ProjectConfig config = SceneManager::LoadConfig(scenePath).value_or(ProjectConfig{});

        std::error_code absoluteError;
        const auto configFile = ProjectConfig::Locate(args, std::filesystem::absolute(scenePath, absoluteError).parent_path());
        if (!configFile) {
            log::Error("[engine] {}", configFile.error());
            return EXIT_FAILURE;
        }
        if (*configFile) {
            if (const auto merged = config.MergeFile(**configFile); !merged) {
                log::Error("[engine] {}", merged.error().message);
                return EXIT_FAILURE;
            }
            log::Info("[engine] configuration: {}", (*configFile)->string());
        }

        if (const auto overridden = config.ApplyOverrides(args); !overridden) {
            log::Error("[engine] {}\n\nOptions:\n{}", overridden.error().message, ProjectConfig::Usage());
            return EXIT_FAILURE;
        }

        // Before anything is loaded: every relative texture, model and shader path from here on is
        // resolved against these roots.
        config.RegisterSearchPaths();

        // The modules koral.json names by hand — the ones nothing links against. A module the
        // project *uses* is already in the process by the time its library is loaded, below, and
        // needs nothing here.
        //
        // This runs before the device, and before either entry point below, for two reasons: a
        // module is expected to be able to influence how the device is created, and both a Scene
        // and a Job get the same set — a module is a property of the project, not of the way it
        // happens to be run.
        //
        // The scene library's own directory is searched too, so a project that keeps its modules
        // beside its build output needs no "moduleDirectories" at all.
        {
            std::error_code ec;
            auto moduleDirectories = config.moduleDirectories;
            if (auto sceneDir = std::filesystem::absolute(scenePath, ec).parent_path(); !ec)
                moduleDirectories.push_back(std::move(sceneDir));

            if (const auto loaded = ModuleHost::Load(config.modules, moduleDirectories); !loaded) {
                log::Error("[engine] {}", loaded.error().message);
                return EXIT_FAILURE;
            }
        }

        // Before the device exists — the Vulkan backend reads it while picking the physical
        // device, which happens inside InitHeadless / the window build below.
        if (!config.gpu.empty()) SetPreferredGpu(config.gpu);

        // Headless path: a library exporting CreateJob runs on a device-only context
        // and terminates — no window, surface, swap chain or GUI. Run() returns a
        // Task, so we pump the executors until it (and anything it co_awaited) finishes.
        if (auto job = SceneManager::LoadJob(scenePath)) {
            bool failed = false;
            // The job's library is loaded, so every module it links has registered itself by now.
            if (const auto resolved = ModuleHost::Resolve(); !resolved) {
                log::Error("[engine] {}", resolved.error().message);
                return EXIT_FAILURE;
            }
            Context::InitHeadless(config.api);
            ModuleHost::Initialize();
            {
                Task<void> task = job->Run();
                while (!task.Done()) {
                    Context::DrainMainThread();
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                if (auto result = task.Take(); !result) {
                    log::Error("[engine] job failed: {}", result.error());
                    failed = true;
                }
            } // task destroyed before the executors it may reference
            // One-off submissions still held for the GPU, while the modules whose resources their
            // records may keep alive are still loaded.
            detail::collectRetired(/*all=*/true);
            ModuleHost::Shutdown();
            Context::ShutdownHeadless();
            return failed ? EXIT_FAILURE : EXIT_SUCCESS;
        }

        // The windowed path: the application, the project's library of scenes, and the one it names.
        std::unique_ptr<App> app;
        try {
            app = std::make_unique<App>(AppSettings{
                .api = config.api,
                .platform = config.platform,
                .gpu = config.gpu,
            });
        } catch (const std::exception& e) {
            log::Error("[engine] {}", e.what());
            return EXIT_FAILURE;
        }

        // Loading the library is also what pulls in every module it links, each registering itself as
        // it is loaded; the application resolves and starts them before the first scene opens.
        const auto names = app->LoadLibrary(scenePath);
        if (!names) {
            log::Error("[engine] {}", names.error().message);
            return EXIT_FAILURE;
        }
        const std::string start = !config.scene.empty() ? config.scene : names->empty() ? std::string() : names->front();
        if (start.empty()) {
            log::Error("[engine] '{}' offers no scenes", scenePath.string());
            return EXIT_FAILURE;
        }

        const WindowSettings window {
            .title = config.title.empty() ? scenePath.stem().string() : config.title,
            .extent = config.extent,
            .resizable = config.resizable,
            .fullscreen = config.fullscreen,
            .decorated = config.decorated,
            .transparentFramebuffer = config.transparentFramebuffer,
            .vsync = config.vsync,
        };
        if (!app->Open(start, window)) return EXIT_FAILURE;   // already reported, with the reason
        if (!config.hotReload) return app->Run();

        // Development: the library is watched, and reloaded — every scene from it reopened with its
        // Scene::State() — once a rebuild has finished writing it. A write is taken as finished when
        // the file has stopped changing for a moment, so a linker still writing is not caught halfway.
        log::Info("[engine] hot reload: watching '{}'", scenePath.string());
        using Clock = std::chrono::steady_clock;
        std::error_code ec;
        auto loaded = std::filesystem::last_write_time(scenePath, ec);
        std::optional<std::filesystem::file_time_type> seen;
        Clock::time_point seenAt {}, checkedAt {};
        while (app->Frame()) {
            const auto now = Clock::now();
            if (now - checkedAt < std::chrono::milliseconds(250)) continue;
            checkedAt = now;
            const auto written = std::filesystem::last_write_time(scenePath, ec);
            if (ec || written == loaded) { seen.reset(); continue; }
            if (!seen || *seen != written) { seen = written; seenAt = now; continue; }
            if (now - seenAt < std::chrono::milliseconds(500)) continue;
            log::Info("[engine] '{}' was rebuilt: reloading", scenePath.filename().string());
            if (const auto reloaded = app->ReloadLibrary(scenePath); !reloaded)
                log::Error("[engine] the rebuilt library could not be loaded: {}", reloaded.error().message);
            loaded = written;
            seen.reset();
        }
        return EXIT_SUCCESS;
    }
}
