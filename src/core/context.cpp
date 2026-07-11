//
// Created by radue on 2/26/2026.
//


#include <surface.h>
#include <framebuffer.h>
#include "context.h"
#include "paths.h"
#include <filesystem>
#include <mutex>
#include <stdexcept>

#include <GLFW/glfw3.h>

#include "scheduler.h"
#include "shader.h"
#include "window.h"
#include "resource.h"
#include "../executor/BackgroundExecutor.h"
#include "../executor/MainThreadExecutor.h"
#include "../backends/vulkan/vulkanContext.h"


std::filesystem::path gfx::assetPath(const std::filesystem::path& relativePath)
{
    // Resolved once, relative to the installed library rather than to whatever machine built it.
    static const auto roots = detail::dataRoots("assets", "GFX_ASSETS_DIR", ASSETS_PATH);

    for (const auto& root : roots) {
        std::error_code ec;
        if (auto candidate = root / relativePath; std::filesystem::exists(candidate, ec))
            return candidate;
    }

    // Nothing matched. Hand back the best guess anyway, so the caller's own "file not found" names
    // a path the user can act on rather than an empty string.
    return roots.empty() ? relativePath : roots.front() / relativePath;
}

std::filesystem::path gfx::shaderPath(const std::filesystem::path& relativePath)
{
    // Resolve across the registered shader roots (the install roots below, plus any the project
    // added via Shader::addSearchPath); first existing wins.
    for (const auto& root : Shader::searchPaths()) {
        std::error_code ec;
        if (auto candidate = root / relativePath; std::filesystem::exists(candidate, ec))
            return candidate;
    }

    const auto& roots = Shader::searchPaths();
    return roots.empty() ? relativePath : roots.front() / relativePath;
}

gfx::Window& gfx::Context::Window()
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

gfx::ResourceRef<const gfx::Framebuffer> gfx::Context::DefaultFramebuffer()
{
    if (_window == nullptr)
    {
        throw std::runtime_error("There is no default framebuffer!");
    }
    return _window->getFramebuffer();
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

gfx::Repository & gfx::Context::Repository() {
    if (!_repository) {
        throw std::runtime_error("Resource repository is not initialized for this thread!");
    }
    return *_repository;
}

gfx::API gfx::Context::activeAPI()
{
    return _activeAPI;
}

bool gfx::Context::IsHeadless()
{
    return _headless;
}

void gfx::Context::InitHeadless(const API api)
{
    if (_window != nullptr)
        throw std::runtime_error("Context::InitHeadless: a window is already active; headless mode is mutually exclusive with a window.");
    if (_headless)
        throw std::runtime_error("Context::InitHeadless: a headless context is already active.");
    if (api != API::eVulkan)
        throw std::runtime_error("Context::InitHeadless: only the Vulkan backend supports headless (device-only) mode.");

    _activeAPI = api;

    // GLFW is initialized so the Vulkan runtime can query platform instance
    // extensions; no window is created. A failure here is non-fatal — a
    // compute/transfer-only instance does not require the WSI surface extensions.
    glfwInit();

    gfx::vk::Context::Init();            // instance + physical/logical device + allocator + descriptor pool

    // Same async machinery as a window: a Job's Initialize() returns a Task and
    // may co_await background work, so the executors must exist even headless.
    _mainThreadExecutor = new MainThreadExecutor();
    _backgroundExecutor = new BackgroundExecutor();

    _repository = new gfx::Repository(); // pipelines register themselves here on build
                                         // (qualified: Context::Repository() the method shadows the type here)
    _headless = true;
}

void gfx::Context::ShutdownHeadless()
{
    if (!_headless) return;

    // Destroy the repository first: its destructor stops the FileWatcher and waits
    // for that coroutine to finish, which requires the background executor to still
    // be alive so the worker can run the watcher to completion.
    delete _repository;
    _repository = nullptr;

    delete _mainThreadExecutor;
    _mainThreadExecutor = nullptr;
    delete _backgroundExecutor;
    _backgroundExecutor = nullptr;

    gfx::vk::Context::Destroy();
    glfwTerminate();
    _headless = false;
}
