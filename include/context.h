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
    class Window;

    GFX_API std::filesystem::path assetPath(const std::filesystem::path& relativePath);
    GFX_API std::filesystem::path shaderPath(const std::filesystem::path& relativePath);

    enum class API {
        eOpenGL,
        eVulkan,
    };

    class Context
    {
        friend class gfx::Window;
        friend class gfx::Scheduler;
        friend class gfx::GUI;
    public:
        static GFX_API gfx::Window& Window();
        static GFX_API const gfx::Scheduler& Scheduler();

        /**
         * @brief The active graphics backend.
         *
         * Set when a window is created, or by @ref InitHeadless. Backend selection
         * across the API keys off this rather than the window, so device-only
         * (headless) sessions work without one.
         */
        static GFX_API API activeAPI();

        /**
         * @brief Create a device-only context with no window, surface or swap chain.
         *
         * Brings up just enough to allocate buffers/images, build pipelines and run
         * one-off compute/transfer commands (via CommandBuffer::SingleTimeCommand) —
         * for tests and offscreen/compute tools. Vulkan only. There is no
         * presentation: anything that needs a framebuffer/swap chain (windowed
         * rendering, the GUI, per-frame resources) is unavailable. Pair with
         * @ref ShutdownHeadless. Not valid while a window exists.
         */
        static GFX_API void InitHeadless(API api = API::eVulkan);

        /** @brief Tear down a headless context created by @ref InitHeadless. */
        static GFX_API void ShutdownHeadless();

        /** @brief Whether a headless (device-only) context is currently active. */
        static GFX_API bool IsHeadless();

        static GFX_API gfx::ResourceRef<const gfx::Framebuffer> DefaultFramebuffer();
        static GFX_API ImGuiContext* GetCurrentImGuiContext();

        static GFX_API gfx::SwitchAwaiter SwitchToMainThread();
        static GFX_API gfx::SwitchAwaiter SwitchToBackgroundThread();
        static GFX_API void DrainMainThread();

        static GFX_API gfx::Repository& Repository();

    private:
        inline static gfx::Window* _window = nullptr;
        inline static gfx::Scheduler* _scheduler = nullptr;
        inline static ImGuiContext* _imguiContext = nullptr;

        inline static API _activeAPI = API::eVulkan;
        inline static bool _headless = false;

        inline static MainThreadExecutor* _mainThreadExecutor = nullptr;
        inline static BackgroundExecutor* _backgroundExecutor   = nullptr;

        inline static gfx::Repository* _repository = nullptr;
    };
}
