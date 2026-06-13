//
// Created by radue on 2/16/2026.
//

module;

#include "api.h"
#include <imgui.h>

export module gfx:context;
import :types;

import std;
import resource;
import task;
import gfx.mainThreadExecutor;
import gfx.backgroundExecutor;

namespace gfx {
    enum class API {
        eOpenGL,
        eVulkan,
    };

    class Context
    {
        friend class Window;
        friend class Scheduler;
        friend class GUI;
    public:
        static GFX_API Window& FocusedWindow();
        static GFX_API Window& GetWindow();
        static GFX_API const gfx::Scheduler& GetScheduler();

        static GFX_API ::ResourceRef<const Framebuffer> DefaultFramebuffer();
        static GFX_API ImGuiContext* GetCurrentImGuiContext();

        static GFX_API ::SwitchAwaiter SwitchToMainThread();
        static GFX_API ::SwitchAwaiter SwitchToBackgroundThread();
        static GFX_API void DrainMainThread();

    private:
        static GFX_API void setFocusedWindow(gfx::Window* window);

        inline static gfx::Window* _focusedWindow = nullptr;
        inline static gfx::Window* _window = nullptr;
        inline static gfx::Scheduler* _scheduler = nullptr;
        inline static ImGuiContext* _imguiContext = nullptr;

        inline static MainThreadExecutor* _mainThreadExecutor = nullptr;
        inline static BackgroundExecutor* _backgroundExecutor   = nullptr;
    };
}
