//
// Created by radue on 2/17/2026.
//

#include <GL/glew.h>

#include "mesh.h"
#include "sceneManager.h"
#include "backends/vulkan/vulkanContext.h"
#include "io/manager.h"
#include "lab/labRectangle.h"

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

        while (!io::Manager::_windows.empty()) {
            io::Manager::update();
        }
        vk::Context::Destroy();
    }
}
