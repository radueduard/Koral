//
// Created by radue on 2/16/2026.
//

#pragma once
#include "api.h"
#include <filesystem>
#include <vector>

#include "task.h"
#include "resource.h"

struct ImGuiContext;
class MainThreadExecutor;
class BackgroundExecutor;

namespace kor {
    class GUI;
    class Scheduler;
    class Framebuffer;
    class Window;

    /**
     * @brief Resolve @p relativePath against the asset search roots; first existing wins.
     *
     * An absolute path is returned untouched. When nothing matches, the path is still joined
     * onto the first root, so the caller's "file not found" names somewhere the user can look
     * instead of an empty string.
     */
    KORAL_API std::filesystem::path assetPath(const std::filesystem::path& relativePath);

    /** @brief The same, against the shader search roots (see Shader::searchPaths). */
    KORAL_API std::filesystem::path shaderPath(const std::filesystem::path& relativePath);

    /**
     * @brief Register a directory to resolve relative asset paths against.
     *
     * Koral's own assets/ is always a root. A project adds its own here — or, more usually,
     * declares them in koral.json under "assetDirectories" and lets the runtime do it.
     *
     * @param front Search this root before the ones already registered. This is what the
     *              config uses: a project's own assets take precedence over the engine's,
     *              so it can shadow a built-in by name without ever losing access to the rest.
     */
    KORAL_API void addAssetSearchPath(const std::filesystem::path& dir, bool front = false);

    /** @brief The asset search roots, in the order assetPath() consults them. */
    KORAL_API const std::vector<std::filesystem::path>& assetSearchPaths();

    enum class API {
        eOpenGL,
        eVulkan,
    };

    class Context
    {
        friend class kor::Window;
        friend class kor::Scheduler;
        friend class kor::GUI;
    public:
        static KORAL_API kor::Window& Window();
        static KORAL_API const kor::Scheduler& Scheduler();

        /**
         * @brief The active graphics backend.
         *
         * Set when a window is created, or by @ref InitHeadless. Backend selection
         * across the API keys off this rather than the window, so device-only
         * (headless) sessions work without one.
         */
        static KORAL_API API activeAPI();

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
        static KORAL_API void InitHeadless(API api = API::eVulkan);

        /** @brief Tear down a headless context created by @ref InitHeadless. */
        static KORAL_API void ShutdownHeadless();

        /** @brief Whether a headless (device-only) context is currently active. */
        static KORAL_API bool IsHeadless();

        static KORAL_API kor::ResourceRef<const kor::Framebuffer> DefaultFramebuffer();
        static KORAL_API ImGuiContext* GetCurrentImGuiContext();

        static KORAL_API kor::SwitchAwaiter SwitchToMainThread();
        static KORAL_API kor::SwitchAwaiter SwitchToBackgroundThread();
        static KORAL_API void DrainMainThread();

        static KORAL_API kor::Repository& Repository();

    private:
        inline static kor::Window* _window = nullptr;
        inline static kor::Scheduler* _scheduler = nullptr;
        inline static ImGuiContext* _imguiContext = nullptr;

        inline static API _activeAPI = API::eVulkan;
        inline static bool _headless = false;

        inline static MainThreadExecutor* _mainThreadExecutor = nullptr;
        inline static BackgroundExecutor* _backgroundExecutor   = nullptr;

        inline static kor::Repository* _repository = nullptr;
    };
}
