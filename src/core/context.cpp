//
// Created by radue on 2/26/2026.
//


#include <surface.h>
#include <framebuffer.h>
#include "context.h"
#include <filesystem>

#include "scheduler.h"
#include "window.h"

thread_local std::thread::id gfx::Context::_threadId;

std::filesystem::path gfx::assetPath(const std::filesystem::path& relativePath)
{
    return std::filesystem::path(ASSETS_PATH) / relativePath;
}

std::filesystem::path gfx::shaderPath(const std::filesystem::path& relativePath)
{
    return std::filesystem::path(SHADERS_PATH) / relativePath;
}

std::filesystem::path gfx::scenePath(const std::filesystem::path& relativePath)
{
    return std::filesystem::path(SCENES_PATH) / relativePath;
}

std::filesystem::path gfx::templatePath(const std::filesystem::path& relativePath)
{
    return std::filesystem::path(TEMPLATES_PATH) / relativePath;
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

const gfx::Scheduler& gfx::Context::Scheduler()
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
    _threadId = std::this_thread::get_id();
    if (_scheduler != nullptr) {
        throw std::runtime_error("Scheduler is already initialized for this thread");
    }
    _scheduler = Scheduler::Builder()
         .setMinImageCount(2)
         .setImageCount(3)
         .build();
    _scheduler->Initialize();
}

void gfx::Context::DestroyScheduler()
{
    if (_scheduler == nullptr) {
        throw std::runtime_error("Scheduler is not initialized for this thread");
    }

    delete _scheduler;
    _scheduler = nullptr;
}

ImGuiContext* gfx::Context::GetCurrentImGuiContext()
{
    if (_imguiContext == nullptr) {
        throw std::runtime_error("ImGui context is not initialized for this thread");
    }
    return _imguiContext;
}

void gfx::Context::setFocusedWindow(io::Window* window)
{
    _focusedWindow = window;
}
