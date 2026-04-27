//
// Created by radue on 2/16/2026.
//

#pragma once
#include "api.h"
#include <filesystem>

#include "task.h"
#include "resource.h"

struct ImGuiContext;
class MainThreadExecutor;
class BackgroundExecutor;

namespace gfx {
    class GUI;
    class Scheduler;
    class Framebuffer;

    namespace io
    {
        class Window;
    }

    GFX_API std::filesystem::path assetPath(const std::filesystem::path& relativePath);
    GFX_API std::filesystem::path shaderPath(const std::filesystem::path& relativePath);

    enum class API {
        eOpenGL,
        eVulkan,
    };

    class Context
    {
        friend class io::Window;
        friend class gfx::Scheduler;
        friend class gfx::GUI;
    public:
        static GFX_API io::Window& FocusedWindow();

        static GFX_API io::Window& Window();
        static GFX_API const gfx::Scheduler& Scheduler();

        static GFX_API gfx::ResourceRef<gfx::Framebuffer> DefaultFramebuffer();
        static GFX_API ImGuiContext* GetCurrentImGuiContext();

        static GFX_API gfx::SwitchAwaiter SwitchToMainThread();
        static GFX_API gfx::SwitchAwaiter SwitchToBackgroundThread();
        static GFX_API void DrainMainThread();

        static GFX_API gfx::Repository& Repository();

    private:
        static GFX_API void setFocusedWindow(io::Window* window);

        inline static io::Window* _focusedWindow = nullptr;
        inline static io::Window* _window = nullptr;
        inline static gfx::Scheduler* _scheduler = nullptr;
        inline static ImGuiContext* _imguiContext = nullptr;

        inline static MainThreadExecutor* _mainThreadExecutor = nullptr;
        inline static BackgroundExecutor* _backgroundExecutor   = nullptr;

        inline static gfx::Repository* _repository = nullptr;
    };
}
