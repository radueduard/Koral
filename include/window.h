//
// Created by radue on 10/13/2024.
//

#pragma once

#include <string>

#include <filesystem>
#include <glm/glm.hpp>

#include "input.h"
#include "context.h"
#include "gtime.h"
#include "api.h"
// Scene is only ever held by pointer here, so it is forward-declared rather than included. That is
// deliberate and worth keeping: scene.h pulls in gui.h, whose inline registrar makes *every* includer
// an ImGui client — which is right for a scene library and wrong for, say, a camera module that only
// wanted the window's extent. A translation unit that actually uses a Scene includes scene.h itself.

namespace kor
{
    class Surface;
    class Framebuffer;
    class Engine;
}

struct GLFWmonitor;
struct GLFWwindow;
struct GLFWimage;
struct GLFWvidmode;

namespace kor {
    class Scene;

    /**
     * @brief The application window, the surface it presents to, and the scene drawn into it.
     *
     * Building a Window is what brings the graphics device up: it creates the OS window, the
     * surface, the swap chain, the scheduler that owns the frames in flight, the default
     * framebuffer and the GUI, and then calls Scene::Initialize on the scene it was given. Tearing
     * it down does the reverse, and waits for the device to go idle first.
     *
     * A scene does not normally construct one. The runtime builds the window from the project's
     * configuration before the first frame and drives it; reach the live one through
     * Context::Window() and the image being drawn to through getFramebuffer().
     *
     * There is one window per process. It is neither copyable nor thread-safe: every method here
     * must be called from the thread that created it, which is the thread the scene is driven on.
     */
    class KORAL_API Window {
        friend class Input;
        friend class Input::Callbacks;
        friend class kor::Engine;
        friend class Time;
    public:
        /**
         * @brief Settings the window is created with, and the scene it will run.
         *
         * Every setter returns the builder, so they chain; build() then creates the window. The
         * scene is the one argument with no default, because a window exists to draw one — it is
         * passed to the constructor and its ownership moves into the window.
         *
         * These are the same settings the runtime reads out of koral.json, so a project usually
         * configures its window there rather than through this type.
         */
        struct KORAL_API Builder {
            std::string title = "GFXFramework";                 ///< Text in the title bar.
            glm::uvec2 extent = { 1280, 720 };                  ///< Initial size of the drawable area, in pixels.
            bool resizable = true;                              ///< Whether the user may resize the window.
            bool fullscreen = false;                            ///< Whether to open fullscreen on the primary monitor.
            bool decorated = true;                              ///< Whether the OS draws a title bar and border.
            bool transparentFramebuffer = false;                ///< Whether the framebuffer's alpha composites with the desktop.
            bool vsync = true;                                  ///< Whether presentation waits for the display's refresh.
            API api = API::eVulkan;                             ///< Graphics backend to bring up.
            WindowPlatform platform = WindowPlatform::eAuto;    ///< Linux windowing system to open on.
            std::filesystem::path imguiIni;                     ///< Where Dear ImGui persists its layout; empty keeps its default (imgui.ini in the working directory).
            /// The scene the window will initialize and draw. No `= nullptr` initializer: a default
            /// member initializer on a unique_ptr instantiates its destructor right here, which needs
            /// Scene complete — the very thing this header no longer requires. It is null anyway.
            std::unique_ptr<Scene> scene;

            // Out of line, all of them — including the constructor, whose by-value parameter is
            // destroyed when it returns: every one of these has to destroy a unique_ptr<Scene>, and
            // that needs Scene complete, which this header no longer makes it. Defined in window.cpp,
            // which does include scene.h.

            /** @param scene The scene to run. Ownership passes to the window that is built. */
            explicit Builder(std::unique_ptr<Scene> scene);
            ~Builder();
            Builder(Builder&&) noexcept;
            Builder& operator=(Builder&&) noexcept;

            /** @brief Sets the text shown in the title bar. */
            Builder& setTitle(const std::string& title) {
                this->title = title;
                return *this;
            }

            /** @brief Sets the initial size of the drawable area, in pixels. */
            Builder& setExtent(const glm::uvec2& extent) {
                this->extent = extent;
                return *this;
            }

            /**
             * @brief Sets whether the user may resize the window.
             *
             * A resize recreates the swap chain and the default framebuffer, and the scene is told
             * through Scene::OnResize. Fixing the size does not exempt a scene from handling that:
             * a fullscreen window still adopts the monitor's resolution.
             */
            Builder& setResizable(bool resizable) {
                this->resizable = resizable;
                return *this;
            }

            /** @brief Sets whether the window opens fullscreen on the primary monitor, at that monitor's resolution. */
            Builder& setFullscreen(bool fullscreen) {
                this->fullscreen = fullscreen;
                return *this;
            }

            /** @brief Sets whether the OS draws a title bar and border around the window. */
            Builder& setDecorated(bool decorated) {
                this->decorated = decorated;
                return *this;
            }

            /**
             * @brief Sets whether the framebuffer's alpha channel composites with the desktop.
             *
             * Requires a compositor that supports it, and is what makes a decorationless overlay
             * window possible. What shows through is whatever alpha the scene leaves in the
             * swap-chain image.
             */
            Builder& setTransparentFramebuffer(bool transparent) {
                this->transparentFramebuffer = transparent;
                return *this;
            }

            /**
             * @brief Sets vertical sync.
             *
             * Enabled (the default), frames are presented in step with the display: no tearing, and
             * the frame rate is capped to the refresh rate. Disabled, frames present as soon as they
             * are ready — uncapped, and free to tear. Turn it off to measure how fast a scene
             * actually renders.
             */
            Builder& setVSync(bool vsync) {
                this->vsync = vsync;
                return *this;
            }

            /** @brief Selects the graphics backend the window and everything drawn in it will use. */
            Builder& setAPI(API api) {
                this->api = api;
                return *this;
            }

            /**
             * @brief Selects the Linux windowing system to open on, X11 or Wayland.
             *
             * Ignored on Windows and macOS. WindowPlatform::eAuto takes whichever the session
             * provides. OpenGL always resolves to X11 whatever this says, so a Wayland session runs
             * it through XWayland.
             *
             * @see WindowPlatform
             */
            Builder& setPlatform(WindowPlatform platform) {
                this->platform = platform;
                return *this;
            }

            /**
             * @brief Sets the file Dear ImGui persists its window layout to.
             *
             * Empty keeps ImGui's default, imgui.ini in the working directory — which means the
             * layout follows whoever launched the program rather than the project. Naming a path
             * gives the project its own layout; missing parent directories are created.
             */
            Builder& setImguiIni(std::filesystem::path imguiIni) {
                this->imguiIni = std::move(imguiIni);
                return *this;
            }

            /**
             * @brief Creates the window, brings the graphics device up, and initializes the scene.
             * @return The window. It owns the scene, the surface and the default framebuffer.
             */
            std::unique_ptr<Window> build();
        };

        /** @brief Constructs the window from a builder. Prefer Builder::build(). */
        explicit Window(Builder&);

        /** @brief Waits for the device to go idle, then tears down the scene, the GUI and the device. */
        ~Window();

        Window(const Window &) = delete;
        Window &operator=(const Window &) = delete;

        Window(Window &&);
        Window &operator=(Window &&);

        /**
         * @brief Whether the window has been asked to close, by the user or by close().
         * @return true once the request has been made; the frame in progress still completes.
         */
        [[nodiscard]] bool shouldClose() const;

        /**
         * @brief Asks the window to close.
         *
         * Sets the same flag the OS close button does, so shouldClose() reports it and the run loop
         * exits after the current frame. Nothing is destroyed here.
         */
        void close();

        /** @brief The underlying GLFW window handle, for code that has to talk to GLFW directly. */
        [[nodiscard]] GLFWwindow* operator*() const { return _window; }

        /** @brief Current size of the drawable area in pixels, which is not the window's outer size on a scaled display. */
        [[nodiscard]] glm::uvec2 getExtent() const { return _extent; }

        /**
         * @brief Whether the window currently has no drawable area, i.e. it is minimized.
         * @return true while the framebuffer is zero-sized. Rendering into a zero-sized swap chain
         *         is invalid, so the run loop skips the frame entirely while this holds.
         */
        [[nodiscard]] bool isPaused() const { return _paused; }

        /** @brief Whether the user may resize the window. */
        [[nodiscard]] bool isResizable() const { return _resizable; }
        /** @brief Whether the window is fullscreen. */
        [[nodiscard]] bool isFullscreen() const { return _fullscreen; }
        /** @brief Whether the OS draws a title bar and border. */
        [[nodiscard]] bool isDecorated() const { return _decorated; }
        /** @brief Whether presentation waits for the display's refresh. */
        [[nodiscard]] bool isVSync() const { return _vsync; }
        /** @brief Whether the framebuffer's alpha composites with the desktop. */
        [[nodiscard]] bool isFramebufferTransparent() const { return _transparentFramebuffer; }

        /** @brief Marks the window paused, so the run loop stops rendering it. Set automatically when it is minimized. */
        void pause() { _paused = true; }

        /** @brief Clears the paused state, so rendering resumes. */
        void unPause() { _paused = false; }

        /** @brief Changes the text in the title bar. */
        void setTitle(const std::string &title);

        /** @brief The graphics backend this window was brought up on. */
        [[nodiscard]] API getAPI() const { return _api; }

        /** @brief The text currently in the title bar. */
        [[nodiscard]] const std::string& getTitle() const { return _title; }

        /**
         * @brief The file Dear ImGui persists its layout to, or empty for ImGui's own default.
         *
         * The string itself backs ImGui's io.IniFilename, which keeps the pointer rather than a
         * copy, so it stays valid for as long as the window does.
         */
        [[nodiscard]] const std::string& getImguiIniPath() const { return _imguiIni; }

        /**
         * @brief The default framebuffer: the swap-chain image this frame is presented from.
         *
         * This is what a scene renders into when it opens a pass without naming a framebuffer. It
         * is recreated on resize, so hold the reference for a frame, not for the run.
         */
        [[nodiscard]] kor::ResourceRef<kor::Framebuffer> getFramebuffer() const;

        /**
         * @brief Whether the drawable area changed size since the last frame.
         * @return true for the one frame that follows the resize; LateUpdate() clears it.
         *
         * The run loop turns this into a Scene::OnResize call, which is where a scene should
         * rebuild anything sized to the window.
         */
        [[nodiscard]] bool hasResized() const { return _hasResized; }

        /** @brief The presentation surface the swap chain was created for. */
        [[nodiscard]] const kor::Surface& getSurface() const { return *_surface; }

        /**
         * @brief Loads an image from disk and makes it the window's icon.
         * @param iconPath Image file to load; anything stb_image reads, converted to RGBA.
         *
         * Replaces any icon set earlier. A file that fails to load leaves the icon unchanged.
         */
        void setIcon(const std::filesystem::path& iconPath);

        /** @brief Whether the window currently has input focus. */
        [[nodiscard]] bool isFocused() const { return _focused; }

        /**
         * @brief End-of-frame bookkeeping: clears the one-frame flags, hasResized() among them.
         *
         * The run loop calls this after the frame has been submitted. A scene should not.
         */
        void LateUpdate();

    private:
    	static void framebufferResize(GLFWwindow* handle, int width, int height);

        GLFWwindow* _window = nullptr;
        GLFWmonitor* _monitor = nullptr;
        GLFWimage* _icon = nullptr;
        const GLFWvidmode *_videoMode = nullptr;
        std::unique_ptr<kor::Surface> _surface;

        std::string _title;
        glm::uvec2 _extent;
        bool _resizable;
        bool _fullscreen;
        bool _decorated;
        bool _transparentFramebuffer;
        bool _vsync;
        API _api;
        std::string _imguiIni;

        kor::Resource<kor::Framebuffer> _framebuffer;

        std::unique_ptr<Scene> _scene;

        bool _paused = false;
        bool _closed = false;
        bool _focused = true;
        bool _hasResized = false;
    };
}
