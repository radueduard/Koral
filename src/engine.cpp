//
// Created by radue on 2/17/2026.
//

#include <GLFW/glfw3.h>

#include "gui.h"
#include "sceneManager.h"
#include "scheduler.h"
#include "project/generate.h"

namespace gfx
{
    class Engine
    {
    public:
        static void Run(const std::filesystem::path& scenePath);
    };

    void Engine::Run(const std::filesystem::path& scenePath)
    {
        auto window = SceneManager::LoadScene(scenePath);
        while (!window->shouldClose()) {
            glfwPollEvents();
            auto& scene = *window->_scene;
            window->_inputState.update();
            window->_timeState.update();
            scene.Update();
            Context::Scheduler().Draw([&](gfx::CommandBuffer& commandBuffer) {
                scene.Render(commandBuffer);
                gfx::GUI::Render(commandBuffer, scene);
            });
        }
        window.reset();
    }
}
