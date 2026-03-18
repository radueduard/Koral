//
// Created by radue on 2/17/2026.
//

#include <GL/glew.h>

#include "sceneManager.h"
#include "backends/vulkan/vulkanContext.h"
#include "io/manager.h"
#include "lab/labRectangle.h"
#include "lab/LabMesh/Lab01.h"

#include "project/generate.h"

namespace gfx
{
    class Engine
    {
        friend int ::main();
        static void Run();
    };

    void Engine::Run()
    {
        glewExperimental = GL_TRUE;
        vk::Context::Init();

        // ProjectManager::generate("/Users/radue", "conway");
        // SceneManager::LoadScene(scenePath("liblSystems.dylib"));
        SceneManager::LoadScene(scenePath("libconway.dylib"));

        // io::Window::Builder(std::unique_ptr<Scene>(new Lab01))
                // .setTitle("Lab 01")
                // .setExtent({ 1280, 720 })
                // .setResizable(true)
                // .setFullscreen(false)
                // .setAPI(API::eVulkan)
                // .build();

        while (!io::Manager::_windows.empty()) {
            io::Manager::update();
        }
        vk::Context::Destroy();
    }
}
