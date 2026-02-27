//
// Created by radue on 2/17/2026.
//

#include <GL/glew.h>

#include "io/manager.h"
#include "io/window.h"
#include "lab/labMesh.h"
#include "lab/labMultiFrameBuffer.h"
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

    auto labRectangle = LabRectangle();
    io::Manager::createWindow(io::Window::CreateInfo(labRectangle)
        .setExtent({1280, 720})
        .setFullscreen(false)
        .setAPI(API::OpenGL));

    auto labTriangle = LabTriangle();
    io::Manager::createWindow(io::Window::CreateInfo(labTriangle)
        .setTitle("Triangle")
        .setExtent({1280, 720})
        .setResizable(true)
        .setFullscreen(false)
        .setAPI(API::OpenGL));

    auto labMesh = LabMesh();
    io::Manager::createWindow(io::Window::CreateInfo(labMesh)
        .setTitle("Mesh")
        .setExtent({1280, 720})
        .setResizable(true)
        .setFullscreen(false)
        .setAPI(API::OpenGL));

    auto labMultiFrameBuffer = LabMultiFrameBuffer();
    io::Manager::createWindow(io::Window::CreateInfo(labMultiFrameBuffer)
        .setTitle("MultiFrameBuffer")
        .setExtent({1280, 720})
        .setResizable(true)
        .setFullscreen(false)
        .setAPI(API::OpenGL));

    while (!io::Manager::_windows.empty()) {
        io::Manager::update();
    }
}
