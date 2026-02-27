//
// Created by radue on 2/26/2026.
//

#include "context.h"

#include "io/window.h"

std::filesystem::path gfx::asset(const std::filesystem::path& relativePath)
{
    return std::filesystem::path(ASSETS_PATH) / relativePath;
}

std::filesystem::path gfx::shader(const std::filesystem::path& relativePath)
{
    return std::filesystem::path(SHADERS_PATH) / relativePath;
}

gfx::io::Window& gfx::Context::FocusedWindow()
{
    if (_focusedWindow == nullptr) {
        throw std::runtime_error("No window is currently focused!");
    }
    return *_focusedWindow;
}

gfx::io::Window& gfx::Context::Window()
{
    if (_currentThreadLinkedWindow == nullptr) {
        throw std::runtime_error("No window is linked to the current thread!");
    }
    return *_currentThreadLinkedWindow;
}

const gfx::Framebuffer* gfx::Context::DefaultFramebuffer()
{
    if (_currentThreadLinkedWindow == nullptr)
    {
        throw std::runtime_error("There is no default framebuffer!");
    }
    return _currentThreadLinkedWindow->getFramebuffer();
}

void gfx::Context::setFocusedWindow(io::Window* window)
{
    _focusedWindow = window;
}
