//
// Created by radue on 2/26/2026.
//

module;

#include <filesystem>

#include "../executor/BackgroundExecutor.h"
#include "../executor/MainThreadExecutor.h"

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

module gfx.context;

namespace
{
    // Directory holding the GFX_RELOADED shared library itself (this translation unit is
    // compiled into it). Used to locate co-installed resources without any env var.
    std::filesystem::path CurrentModuleDir()
    {
#if defined(_WIN32)
        HMODULE mod = nullptr;
        if (GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&CurrentModuleDir), &mod))
        {
            wchar_t buf[MAX_PATH];
            const DWORD n = GetModuleFileNameW(mod, buf, MAX_PATH);
            if (n > 0 && n < MAX_PATH) {
                return std::filesystem::path(buf, buf + n).parent_path();
            }
        }
#else
        Dl_info info{};
        if (dladdr(reinterpret_cast<void*>(&CurrentModuleDir), &info) && info.dli_fname) {
            std::error_code ec;
            auto resolved = std::filesystem::canonical(info.dli_fname, ec);
            return (ec ? std::filesystem::path(info.dli_fname) : resolved).parent_path();
        }
#endif
        return {};
    }

    // <prefix>/share/gfx in an installed SDK, or empty when running from the build tree
    // (then callers fall back to the compile-time source paths). Resolved once.
    const std::filesystem::path& InstalledShareDir()
    {
        static const std::filesystem::path shareDir = []() -> std::filesystem::path
        {
            const auto moduleDir = CurrentModuleDir();
            if (moduleDir.empty()) {
                return {};
            }
            // The module sits in <prefix>/bin (Windows) or <prefix>/lib (Unix); resources are
            // at <prefix>/share/gfx. Probe a few parents so the lookup tolerates either layout.
            for (const auto& candidate : { moduleDir, moduleDir.parent_path(),
                                           moduleDir.parent_path().parent_path() })
            {
                std::error_code ec;
                auto share = candidate / "share" / "gfx";
                if (std::filesystem::exists(share, ec)) {
                    return share;
                }
            }
            return {};
        }();
        return shareDir;
    }
}

std::filesystem::path gfx::assetPath(const std::filesystem::path& relativePath)
{
    const auto& share = InstalledShareDir();
    const auto base = share.empty() ? std::filesystem::path(ASSETS_PATH) : share / "assets";
    return base / relativePath;
}

std::filesystem::path gfx::shaderPath(const std::filesystem::path& relativePath)
{
    const auto& share = InstalledShareDir();
    const auto base = share.empty() ? std::filesystem::path(SHADERS_PATH) : share / "shaders";
    return base / relativePath;
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

void gfx::Context::setFocusedWindow(io::Window* window)
{
    _focusedWindow = window;
}
