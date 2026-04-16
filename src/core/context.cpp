//
// Created by radue on 2/26/2026.
//


#include <surface.h>
#include <framebuffer.h>
#include "context.h"
#include <filesystem>
#include <mutex>

#include "scheduler.h"
#include "window.h"
#include "../executor/BackgroundExecutor.h"
#include "../executor/MainThreadExecutor.h"


std::filesystem::path gfx::assetPath(const std::filesystem::path& relativePath)
{
    return std::filesystem::path(ASSETS_PATH) / relativePath;
}

std::filesystem::path gfx::shaderPath(const std::filesystem::path& relativePath)
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
    if (_window == nullptr) {
        throw std::runtime_error("No window is linked to the current thread!");
    }
    return *_window;
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
    if (_window == nullptr)
    {
        throw std::runtime_error("There is no default framebuffer!");
    }
    return *_window->getFramebuffer();
}

ImGuiContext* gfx::Context::GetCurrentImGuiContext()
{
    if (_imguiContext == nullptr) {
        throw std::runtime_error("ImGui context is not initialized for this thread");
    }
    return _imguiContext;
}

gfx::SwitchAwaiter gfx::Context::SwitchToMainThread() {
    if (!_mainThreadExecutor) {
        throw std::runtime_error("Main thread executor is not initialized for this thread!");
    }
    return _mainThreadExecutor->SwitchToMainThread();
}

gfx::SwitchAwaiter gfx::Context::SwitchToBackgroundThread() {
    if (!_backgroundExecutor) {
        throw std::runtime_error("Background thread executor is not initialized for this thread!");
    }
    return _backgroundExecutor->SwitchToBackground();
}

void gfx::Context::DrainMainThread() {
    if (!_mainThreadExecutor) {
        throw std::runtime_error("Main thread executor is not initialized for this thread!");
    }
    _mainThreadExecutor->Drain();
}

void gfx::Context::setFocusedWindow(io::Window* window)
{
    _focusedWindow = window;
}
