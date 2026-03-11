//
// Created by radue on 2/26/2026.
//

#include "context.h"

#include "core/scheduler.h"
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

gfx::Scheduler& gfx::Context::Scheduler()
{
    if (_scheduler == nullptr) {
        throw std::runtime_error("No scheduler is linked to the current thread!");
    }
    return *_scheduler;
}

const gfx::Framebuffer& gfx::Context::DefaultFramebuffer()
{
    if (_currentThreadLinkedWindow == nullptr)
    {
        throw std::runtime_error("There is no default framebuffer!");
    }
    return *_currentThreadLinkedWindow->getFramebuffer();
}

void gfx::Context::InitScheduler()
{
    if (_scheduler != nullptr) {
        throw std::runtime_error("Scheduler is already initialized for this thread");
    }
    _scheduler = Scheduler::Builder()
                 .setMinImageCount(2)
                 .setImageCount(3)
                 .build();
}

void gfx::Context::DestroyScheduler()
{
    if (_scheduler == nullptr) {
        throw std::runtime_error("Scheduler is not initialized for this thread");
    }
    delete _scheduler;
    _scheduler = nullptr;
}

void gfx::Context::setFocusedWindow(io::Window* window)
{
    _focusedWindow = window;
}
