//
// Created by radue on 2/17/2026.
//

#include <iostream>
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "gui.h"
#include "sceneManager.h"
#include "scheduler.h"

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

        bool fullscreen = false;
        bool resizable = false;
        bool decorated = true;
        bool transparentFramebuffer = false;
        glm::uvec2 extent = { 1280, 720 };
        auto api = API::eVulkan;

        for (int i = 2; i < argc; ++i) {
            if (const std::string arg = argv[i]; arg == "--fullscreen") {
                fullscreen = true;
            } else if (arg == "--resizable") {
                resizable = true;
            } else if (arg == "--borderless") {
                decorated = false;
            } else if (arg == "--transparent") {
                transparentFramebuffer = true;
            }
            else if (arg == "--width") {
                if (i + 1 < argc) {
                    extent.x = std::stoul(argv[++i]);
                } else {
                    std::cerr << "Missing value for --width" << std::endl;
                }
            } else if (arg == "--height") {
                if (i + 1 < argc) {
                    extent.y = std::stoul(argv[++i]);
                } else {
                    std::cerr << "Missing value for --height" << std::endl;
                }
            } else if (arg == "--api" && i + 1 < argc) {
                if (const std::string apiStr = argv[++i]; apiStr == "Vulkan") {
                    api = API::eVulkan;
                } else if (apiStr == "OpenGL") {
                    api = API::eOpenGL;
                }
            }
        }

        auto window = io::Window::Builder(SceneManager::LoadScene(scenePath))
            .setTitle(scenePath)
            .setExtent(extent)
            .setFullscreen(fullscreen)
            .setResizable(resizable)
            .setDecorated(decorated)
            .setTransparentFramebuffer(transparentFramebuffer)
            .setAPI(api)
            .build();

        while (!window->shouldClose()) {
            glfwPollEvents();
            Context::DrainMainThread();

            if (window->isPaused()) {
                continue;
            }
            auto& scene = *window->_scene;
            window->_inputState.update();
            window->_timeState.update();
            Context::Scheduler().Draw([&](CommandBuffer& commandBuffer) {
                Context::Repository().update();
                scene.Update();
                scene.Render(commandBuffer);
                GUI::Render(commandBuffer, scene);
            });
            window->LateUpdate();
        }
        window.reset();
    }
}
