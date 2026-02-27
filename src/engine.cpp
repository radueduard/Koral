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
    io::Window::Builder(labRectangle)
        .setExtent({1280, 720})
        .setFullscreen(false)
        .setAPI(API::OpenGL)
        .build();

    auto labTriangle = LabTriangle();
    io::Window::Builder(labTriangle)
        .setTitle("Triangle")
        .setExtent({1280, 720})
        .setAPI(API::OpenGL)
        .build();

    auto labMesh = LabMesh();
    io::Window::Builder(labMesh)
        .setTitle("Mesh")
        .setExtent({1280, 720})
        .setAPI(API::OpenGL)
        .build();

    auto labMultiFrameBuffer = LabMultiFrameBuffer();
    io::Window::Builder(labMultiFrameBuffer)
        .setTitle("MultiFrameBuffer")
        .setExtent({1280, 720})
        .setAPI(API::OpenGL)
        .build();

    while (!io::Manager::_windows.empty()) {
        io::Manager::update();
    }
}
