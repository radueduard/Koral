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
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "gui.h"
#include "log.h"
#include "projectConfig.h"
#include "sceneManager.h"
#include "scheduler.h"
#include "shader.h"

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
            if (auto found = ProjectConfig::find(std::filesystem::absolute(scenePath, ec).parent_path()))
                return found;

            return ProjectConfig::find(std::filesystem::current_path(ec));
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
            log::error("[engine] {}", configFile.error());
            return EXIT_FAILURE;
        }
        if (*configFile) {
            if (const auto merged = config.mergeFile(**configFile); !merged) {
                log::error("[engine] {}", merged.error().message);
                return EXIT_FAILURE;
            }
            log::info("[engine] configuration: {}", (*configFile)->string());
        }

        if (const auto overridden = config.applyOverrides(args); !overridden) {
            log::error("[engine] {}\n\nOptions:\n{}", overridden.error().message, ProjectConfig::usage());
            return EXIT_FAILURE;
        }

        // Before anything is loaded: every relative texture, model and shader path from here on is
        // resolved against these roots.
        config.registerSearchPaths();

        // Headless path: a library exporting CreateJob runs on a device-only context
        // and terminates — no window, surface, swap chain or GUI. Run() returns a
        // Task, so we pump the executors until it (and anything it co_awaited) finishes.
        if (auto job = SceneManager::LoadJob(scenePath)) {
            bool failed = false;
            Context::InitHeadless(config.api);
            {
                Task<void> task = job->Run();
                while (!task.done()) {
                    Context::DrainMainThread();
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                if (auto result = task.take(); !result) {
                    log::error("[engine] job failed: {}", result.error());
                    failed = true;
                }
            } // task destroyed before the executors it may reference
            Context::ShutdownHeadless();
            return failed ? EXIT_FAILURE : EXIT_SUCCESS;
        }

        auto window = Window::Builder(SceneManager::LoadScene(scenePath))
            .setTitle(config.title.empty() ? scenePath.string() : config.title)
            .setExtent(config.extent)
            .setFullscreen(config.fullscreen)
            .setResizable(config.resizable)
            .setDecorated(config.decorated)
            .setTransparentFramebuffer(config.transparentFramebuffer)
            .setVSync(config.vsync)
            .setAPI(config.api)
            .build();

        while (!window->shouldClose()) {
            glfwPollEvents();
            Context::DrainMainThread();

            if (window->isPaused()) {
                Input::update();
                continue;
            }
            auto& scene = *window->_scene;
            if (window->hasResized()) {
                scene.OnResize(window->getExtent());
            }
            Time::update();
            Context::Scheduler().Draw([&](CommandBuffer& commandBuffer) {
                Context::Repository().update();
                scene.Update();
                scene.Render(commandBuffer);
                GUI::Render(commandBuffer, scene);
            });
            Input::update();
            window->LateUpdate();
        }
        window.reset();
        return EXIT_SUCCESS;
    }
}
