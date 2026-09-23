//
// Created by radue on 2/16/2026.
//

#pragma once
#include <memory>
#include <cstdint>
#include "api.h"
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "task.h"
#include "resource.h"

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

    /**
     * @brief Which GPU the Vulkan backend should pick, instead of its automatic choice.
     *
     * @p preference is either an index into the list the runtime logs at startup
     * ("[vulkan] GPU 0: ..."), or a case-insensitive substring of a device name ("radeon",
     * "GeForce RTX 4070"). Empty restores the automatic choice (best suitable device,
     * discrete first). A preference that matches nothing, or matches a device missing a
     * required capability, is reported and the automatic choice is used instead — it never
     * turns a startable run into a failed one.
     *
     * Must be set before the device exists — i.e. before the window is built or
     * Context::InitHeadless runs. The runtime sets this from `rendering.gpu` in koral.json
     * or the `--gpu` flag; call it directly only when embedding Koral without the runtime.
     * The OpenGL backend cannot choose a device and ignores this.
     */
    KORAL_API void setPreferredGpu(std::string_view preference);

    /** @brief The preference set by @ref setPreferredGpu; empty means automatic. */
    KORAL_API const std::string& preferredGpu();

    /** @brief The graphics backend a context runs on. */
    enum class API : std::uint8_t {
        eOpenGL,    ///< OpenGL. Broadest hardware support; no ray tracing or mesh shaders.
        eVulkan,    ///< Vulkan. The default, and the only backend with ray tracing.
    };

    /**
     * @brief Which windowing system to open the window on. Linux only; ignored elsewhere.
     *
     * On Linux a GLFW build can target both X11 and Wayland, and picks one automatically at startup.
     * eAuto keeps that automatic choice (with one exception: OpenGL is always pinned to X11, because
     * GLEW resolves entry points through GLX and cannot drive a Wayland/EGL context). eX11 and
     * eWayland force the respective platform when the GLFW build and the running session support it,
     * falling back to automatic selection with a warning when they do not.
     */
    enum class WindowPlatform : std::uint8_t {
        eAuto,
        eX11,
        eWayland,
    };

    /**
     * @brief Process-wide access to whatever the runtime has brought up.
     *
     * Everything here is static, because there is one window, one device and one scheduler per
     * process. A scene reaches the pieces it needs through this rather than being handed them:
     *
     * @code
     * const auto extent = kor::Context::Window().extent();
     * commandBuffer.BeginRendering(kor::Context::defaultFramebuffer());
     * @endcode
     *
     * The accessors are only valid once a context exists — after the window has been built, or
     * after InitHeadless(). Calling them from a scene is always safe: by the time Initialize() runs,
     * everything below is up.
     */
    class Context
    {
        friend class kor::Window;
        friend class kor::Scheduler;
    public:
        /** @brief The application window. Not valid in a headless context, which has none. */
        static KORAL_API kor::Window& Window();

        /** @brief The frame scheduler: swap chain, frames in flight, and which image is current. */
        static KORAL_API kor::Scheduler& Scheduler();

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
        static KORAL_API bool isHeadless();

        /**
         * @brief Whether there is a graphics device at all — a window's, or a headless one's.
         *
         * For code that has to answer a question about the device without being able to assume one
         * exists yet, such as which image formats it supports. @see Image::isFormatSupported
         */
        [[nodiscard]] static KORAL_API bool hasDevice() noexcept;

        /**
         * @brief Whether the active device supports ray tracing (acceleration structures + the
         *        ray tracing pipeline).
         *
         * Not every GPU does -- older/integrated GPUs and MoltenVK (macOS) commonly lack it. Check
         * this before building an AccelerationStructure or RayTracingPipeline: on a device without
         * support they still build cleanly into a poisoned resource with a clear error rather than
         * crashing, but this lets a caller decide not to attempt ray tracing at all. False under
         * the OpenGL backend, and before any window/headless context exists.
         */
        static KORAL_API bool supportsRayTracing();

        /**
         * @brief The framebuffer wrapping the swap-chain image this frame presents.
         * @return The window's default framebuffer — the same one CommandBuffer::BeginRendering
         *         uses when called without one. Recreated on resize, so hold it for a frame rather
         *         than for the run.
         */
        static KORAL_API kor::ResourceRef<const kor::Framebuffer> defaultFramebuffer();

        /**
         * @brief Awaitable that moves the rest of a coroutine onto the main thread.
         *
         * The thread the run loop drives, and the only one that may touch the device. Anything a
         * background coroutine produced has to come back here before it is used.
         */
        static KORAL_API kor::SwitchAwaiter SwitchToMainThread();

        /**
         * @brief Awaitable that moves the rest of a coroutine onto a background thread.
         *
         * For work that would otherwise stall the frame — reading a file, decoding an image,
         * compiling a shader.
         */
        static KORAL_API kor::SwitchAwaiter SwitchToBackgroundThread();

        /**
         * @brief Runs everything queued for the main thread, then returns.
         *
         * Called by the run loop once per frame, which is what lets a coroutine resume there. Only
         * of interest when driving Koral without the runtime.
         */
        static KORAL_API void DrainMainThread();

        /**
         * @brief The resource repository, which tracks live resources and updates them each frame.
         *
         * How per-frame buffers propagate their writes and how a pipeline notices that its shader
         * was recompiled. Resources register themselves; a scene rarely touches this.
         */
        static KORAL_API kor::Repository& Repository();

        /**
         * @brief Whether there is a repository to reach — that is, whether a context exists at all.
         *
         * For code that may legitimately run before startup or after shutdown, typically a module
         * registering something of its own for the per-frame update. @ref Repository throws in that
         * situation, which is right for a scene (it cannot happen) and wrong for a library.
         */
        [[nodiscard]] static KORAL_API bool hasRepository() noexcept;

    private:
        inline static kor::Window* _window = nullptr;

        /// Owned outright, and declared rather than defined here: kor::Scheduler is only
        /// forward-declared in this header, and unique_ptr's destructor needs the complete type.
        /// Defined in context.cpp, which is also where the rest of this state ought to live.
        /// @see the note below.
        static KORAL_API std::unique_ptr<kor::Scheduler> _scheduler;

        inline static API _activeAPI = API::eVulkan;
        inline static bool _headless = false;

        inline static MainThreadExecutor* _mainThreadExecutor = nullptr;
        inline static BackgroundExecutor* _backgroundExecutor   = nullptr;

        inline static kor::Repository* _repository = nullptr;

        // These remaining members are `inline static`, which means the definition sits in this
        // header and every translation unit that odr-uses one emits its own. Under
        // -fvisibility=hidden that would give the executable and each loaded module a separate
        // copy — the hazard that moved GUI's font map into gui.cpp. It is latent rather than live
        // here, because they are private and nothing outside libKoral touches them: a module only
        // ever reaches this state through the exported accessors above, which are compiled into
        // the library and read the library's copy. Keep it that way — anything that needs direct
        // access belongs in a .cpp inside libKoral, and a new member of this kind should be
        // declared here and defined there, as _scheduler now is.
    };
}
