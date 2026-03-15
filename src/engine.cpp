//
// Created by radue on 2/17/2026.
//

#include <GL/glew.h>

#include "mesh.h"
#include "sceneManager.h"
#include "backends/vulkan/vulkanContext.h"
#include "io/manager.h"

#include "lab/LabMesh/Lab01.h"

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

        {
            // SceneManager::LoadScene("F:/GFX_PROJECTS/SimpleTriangle/cmake-build-debug/SimpleTriangle.dll");
            SceneManager::LoadScene("F:/GFX_PROJECTS/MultiFramebuffer/cmake-build-debug/MultiFramebuffer.dll");

            while (!io::Manager::_windows.empty()) {
                io::Manager::update();
            }
        }
        vk::Context::Destroy();
    }
}
