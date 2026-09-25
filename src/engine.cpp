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
#include "gui.h"
#include "imageView.h"
#include "log.h"
#include "module.h"
#include "projectConfig.h"
#include "sceneManager.h"
#include "scheduler.h"
#include "shader.h"
#include "core/tokenState.h"

namespace kor
{
    class Engine
    {
    public:
        static int Run(int argc, char** argv);
    };

    namespace
    {
        /**
         * @brief Which config file to read, if any.
         *
         * In order: an explicit `--config`; the KORAL_CONFIG environment variable (how a packaged
         * game or a test harness points the runtime at a config without touching the command line);
         * the nearest koral.json at or above the scene library — which is where a project's own
         * config sits, since the library is built into a subdirectory of the project; and finally
         * the same search from the working directory, for a library kept somewhere else entirely.
         *
         * A config named explicitly and then not found is an error, not a shrug: the user asked for
         * settings that would silently not be applied.
         */
        std::expected<std::optional<std::filesystem::path>, std::string> locateConfig(
            const std::span<const std::string> args, const std::filesystem::path& scenePath)
        {
            const auto mustExist = [](std::filesystem::path file, const std::string_view origin)
                -> std::expected<std::optional<std::filesystem::path>, std::string>
            {
                std::error_code ec;
                if (!std::filesystem::is_regular_file(file, ec))
                    return std::unexpected(std::format("config file '{}' ({}) does not exist",
                                                       file.string(), origin));
                return file;
            };

            for (std::size_t i = 0; i < args.size(); ++i) {
                if (args[i] != "--config") continue;
                if (i + 1 >= args.size()) return std::unexpected("missing value for --config");
                return mustExist(args[i + 1], "--config");
            }

            if (const char* fromEnv = std::getenv("KORAL_CONFIG"); fromEnv != nullptr && *fromEnv != '\0')
                return mustExist(fromEnv, "KORAL_CONFIG");

            std::error_code ec;
            if (auto found = ProjectConfig::Find(std::filesystem::absolute(scenePath, ec).parent_path()))
                return found;

            return ProjectConfig::Find(std::filesystem::current_path(ec));
        }
    }

    int Engine::Run(const int argc, char** argv)
    {
        const std::filesystem::path scenePath = argv[1];
        const std::vector<std::string> args(argv + 2, argv + argc);

        // Three layers, weakest first. The library's own CreateProjectConfig is what the project was
        // compiled to want; koral.json is what its author configured without recompiling; the flags
        // are what this one run overrides. See projectConfig.h.
        ProjectConfig config = SceneManager::LoadConfig(scenePath).value_or(ProjectConfig{});

        const auto configFile = locateConfig(args, scenePath);
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

        // If nothing set an ImGui layout path (neither the config file nor a flag), default it to
        // sit beside the config file, so each project keeps its own layout and it is found again no
        // matter which directory the runtime was launched from. With no config file at all there is
        // no project directory to anchor to, so ImGui keeps its own default (imgui.ini in the CWD).
        if (config.imguiIni.empty() && *configFile)
            config.imguiIni = (*configFile)->parent_path() / "imgui.ini";

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
            // Each scene with an interface keeps its layout beside the project's (imgui.<scene>.ini).
            std::filesystem::path interfaceDirectory;
            if (!config.imguiIni.empty()) interfaceDirectory = config.imguiIni.parent_path();
            app = std::make_unique<App>(AppSettings{
                .api = config.api,
                .platform = config.platform,
                .gpu = config.gpu,
                .interfaceDirectory = interfaceDirectory,
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
        return app->Run();
    }
}
