//
// Created by radue on 2/17/2026.
//

#include <iostream>
#include <chrono>
#include <thread>
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "gui.h"
#include "log.h"
#include "sceneManager.h"
#include "scheduler.h"
#include "shader.h"

namespace gfx
{
    class Engine
    {
    public:
        static void Run(int argc, char** argv);
    };

    void Engine::Run(const int argc, char** argv)
    {
        const auto scenePath = argv[1];

        // The project may export a config (CreateProjectConfig) carrying its shader folder
        // and window/runtime defaults; present CLI flags override those defaults.
        ProjectConfig config = SceneManager::LoadConfig(scenePath).value_or(ProjectConfig{});

        for (int i = 2; i < argc; ++i) {
            if (const std::string arg = argv[i]; arg == "--fullscreen") {
                config.fullscreen = true;
            } else if (arg == "--resizable") {
                config.resizable = true;
            } else if (arg == "--borderless") {
                config.decorated = false;
            } else if (arg == "--transparent") {
                config.transparentFramebuffer = true;
            } else if (arg == "--vsync") {
                config.vsync = true;
            } else if (arg == "--no-vsync") {
                config.vsync = false;
            } else if (arg == "--width") {
                if (i + 1 < argc) config.extent.x = std::stoul(argv[++i]);
                else std::cerr << "Missing value for --width" << std::endl;
            } else if (arg == "--height") {
                if (i + 1 < argc) config.extent.y = std::stoul(argv[++i]);
                else std::cerr << "Missing value for --height" << std::endl;
            } else if (arg == "--api" && i + 1 < argc) {
                if (const std::string apiStr = argv[++i]; apiStr == "Vulkan") config.api = API::eVulkan;
                else if (apiStr == "OpenGL") config.api = API::eOpenGL;
            }
        }

        // Register the project's shader folder; the API's own shaders/ is always a root.
        if (!config.shaderDirectory.empty())
            Shader::addSearchPath(config.shaderDirectory);

        // Headless path: a library exporting CreateJob runs on a device-only context
        // and terminates — no window, surface, swap chain or GUI. Run() returns a
        // Task, so we pump the executors until it (and anything it co_awaited) finishes.
        if (auto job = SceneManager::LoadJob(scenePath)) {
            Context::InitHeadless(config.api);
            {
                Task<void> task = job->Run();
                while (!task.done()) {
                    Context::DrainMainThread();
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                if (auto result = task.take(); !result)
                    log::error("[engine] job failed: {}", result.error());
            } // task destroyed before the executors it may reference
            Context::ShutdownHeadless();
            return;
        }

        auto window = Window::Builder(SceneManager::LoadScene(scenePath))
            .setTitle(config.title.empty() ? std::string(scenePath) : config.title)
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
    }
}
