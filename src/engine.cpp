//
// Created by radue on 2/17/2026.
//

#include <GL/glew.h>

#include "impl/vulkan/vulkanContext.h"
#include "io/manager.h"
#include "io/window.h"

#include "lab/LabMesh/Lab01.h"
#include "lab/LabDeffered/labMultiFrameBuffer.h"
#include "lab/labRectangle.h"
#include "lab/labTriangle.h"


namespace gfx
{
    class Engine
    {
        friend int ::main();
        static void Run();
    };
}

void gfx::Engine::Run()
{
    glewExperimental = GL_TRUE;
    gfx::vk::Context::Init();
    {
        // auto labRectangle = LabRectangle();
        // io::Window::Builder(labRectangle)
        //     .setExtent({1280, 720})
        //     .setFullscreen(false)
        //     .setAPI(API::eVulkan)
        //     .build();

        // auto labTriangle = LabTriangle();
        // io::Window::Builder(labTriangle)
        //     .setTitle("Triangle")
        //     .setExtent({1280, 720})
        //     .setAPI(API::eVulkan)
        //     .build();

        // auto labTriangleOgl = LabTriangle();
        // io::Window::Builder(labTriangleOgl)
        //     .setTitle("Triangle")
        //     .setExtent({1280, 720})
        //     .setAPI(API::eVulkan)
        //     .build();

        // auto labMeshVk = Lab01();
        // io::Window::Builder(labMeshVk)
        //     .setTitle("Mesh")
        //     .setExtent({1280, 720})
        //     .setAPI(API::eVulkan)
        //     .build();

        // auto labMeshOgl = Lab01();
        // io::Window::Builder(labMeshOgl)
        //     .setTitle("Mesh")
        //     .setExtent({1280, 720})
        //     .setAPI(API::eOpenGL)
        //     .build();

        auto labMultiFrameBuffer = LabMultiFrameBuffer();
        io::Window::Builder(labMultiFrameBuffer)
            .setTitle("MultiFrameBuffer")
            .setExtent({1280, 720})
            .setAPI(API::eVulkan)
            .build();

        while (!io::Manager::_windows.empty()) {
            io::Manager::update();
        }
    }
    gfx::vk::Context::Destroy();
}
