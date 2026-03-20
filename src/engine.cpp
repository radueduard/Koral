//
// Created by radue on 2/17/2026.
//

#include <GL/glew.h>

#include "sceneManager.h"
#include "backends/vulkan/vulkanContext.h"
#include "io/manager.h"

#include "project/generate.h"

namespace gfx
{
    class Engine
    {
        friend int ::main(int argc, char **argv);
        static void Run(std::filesystem::path scenePath);
    };

    void Engine::Run(std::filesystem::path scenePath)
    {
        glewExperimental = GL_TRUE;
        vk::Context::Init();

        // ProjectManager::generate("F:/GFX_PROJECTS", "CameraAndSimpleMesh");
        // SceneManager::LoadScene(scenePath("liblSystems.dylib"));
        // SceneManager::LoadScene(scenePath("libconway.dylib"));

        // io::Window::Builder(std::unique_ptr<Scene>(new Lab01))
        //         .setTitle("Lab 01")
        //         .setExtent({ 1280, 720 })
        //         .setResizable(true)
        //         .setFullscreen(false)
        //         .setAPI(API::eVulkan)
        //         .build();

        // SceneManager::LoadScene(scenePath("CameraAndSimpleMesh.dll"));
        // SceneManager::LoadScene(scenePath("MultiFramebuffer.dll"));
        SceneManager::LoadScene(scenePath);

        while (!io::Manager::_windows.empty()) {
            io::Manager::update();
        }
        vk::Context::Destroy();
    }
}
