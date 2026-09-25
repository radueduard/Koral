//
// Created by radue on 10/13/2024.
//

#pragma once

#include <memory>
#include <string>

#include <filesystem>
#include <glm/glm.hpp>

#include "api.h"
#include "resource.h"

namespace kor
{
    class Surface;
    class Framebuffer;
    class App;
    class Input;
    namespace vk { class Scheduler; }
}

struct GLFWmonitor;
struct GLFWwindow;
struct GLFWimage;
struct GLFWvidmode;

namespace kor {
    /**
     * @brief What a window is opened with.
     *
     * Designated initialisers name just what differs from the defaults:
     *
     * @code
     * kor::Navigator::Open("Inspector", {.title = "Buffers", .extent = {640, 360}});
     * @endcode
     *
     * The runtime reads the first scene's from koral.json.
     */
    struct WindowSettings {
        std::string title = "Koral";               ///< Text in the title bar.
        glm::uvec2 extent = { 1280, 720 };          ///< Initial size of the drawable area, in pixels.
        bool resizable = true;                      ///< Whether the user may resize it.
        bool fullscreen = false;                    ///< Whether to open fullscreen on the primary monitor.
        bool decorated = true;                      ///< Whether the OS draws a title bar and border.
        bool transparentFramebuffer = false;        ///< Whether the framebuffer's alpha composites with the desktop.
        bool vsync = true;                          ///< Whether presentation waits for the display's refresh.
    };

    /**
     * @brief One scene's OS window: the surface it presents to, its swap chain and its default
     *        framebuffer.
     *
     * Every scene has exactly one, opened by the application along with the scene (App::Open,
     * Navigator::Open) and reached inside the scene as `Window::` (Scene::Window) or from outside as
     * Scene::SceneWindow(). Replacing the scene in it (Navigator::Replace, Push) keeps the window; the
     * window closes with the last scene shown in it.
     *
     * Neither copyable nor thread-safe: everything here is called from the thread the application
     * runs on.
     */
    class KORAL_API Window {
        friend class Input;
        friend class App;
        friend class kor::vk::Scheduler;
    public:

        /** @brief Waits for nothing: the application retires a window once the frames using it are done. */
        ~Window();

        Window(const Window &) = delete;
        Window &operator=(const Window &) = delete;
        Window(Window &&) = delete;
        Window &operator=(Window &&) = delete;

        /**
         * @brief Whether the window has been asked to close, by the user or by Close().
         *
         * The application answers it after the frame: Scene::OnCloseRequested decides, and by default
         * the window closes with every scene shown in it.
         */
        [[nodiscard]] bool ShouldClose() const;

        /** @brief Asks the window to close, as its close button does. */
        void Close();

        /** @brief The underlying GLFW window handle, for code that has to talk to GLFW directly. */
        [[nodiscard]] GLFWwindow* operator*() const { return _window; }

        /** @brief Current size of the drawable area in pixels, which is not the window's outer size on a scaled display. */
        [[nodiscard]] glm::uvec2 Extent() const { return _extent; }

        /**
         * @brief Whether the window currently has no drawable area, i.e. it is minimized.
         *
         * The scene shown in it is not updated or drawn while this holds.
         */
        [[nodiscard]] bool IsPaused() const { return _paused; }

        /** @brief Whether the user may resize the window. */
        [[nodiscard]] bool IsResizable() const { return _resizable; }
        /** @brief Whether the window is fullscreen. */
        [[nodiscard]] bool IsFullscreen() const { return _fullscreen; }
        /** @brief Whether the OS draws a title bar and border. */
        [[nodiscard]] bool IsDecorated() const { return _decorated; }
        /** @brief Whether presentation waits for the display's refresh. */
        [[nodiscard]] bool IsVSync() const { return _vsync; }
        /** @brief Whether the framebuffer's alpha composites with the desktop. */
        [[nodiscard]] bool IsFramebufferTransparent() const { return _transparentFramebuffer; }

        /** @brief Marks the window paused. Set automatically when it is minimized. */
        void Pause() { _paused = true; }

        /** @brief Clears the paused state. */
        void Unpause() { _paused = false; }

        /** @brief Changes the text in the title bar. */
        void SetTitle(const std::string &title);

        /** @brief The text currently in the title bar. */
        [[nodiscard]] const std::string& Title() const { return _title; }

        /**
         * @brief The default framebuffer: the swap-chain image this frame is presented from.
         *
         * What a pass renders into when it opens one without naming a framebuffer, and what the frame
         * graph calls FrameGraph::Screen. Recreated on resize, so hold the reference for a frame.
         */
        [[nodiscard]] kor::ResourceRef<kor::Framebuffer> DefaultFramebuffer() const;

        /**
         * @brief Whether the drawable area changed size since the last frame.
         * @return true for the one frame that follows the resize. The application turns it into a
         *         Scene::OnResize call.
         */
        [[nodiscard]] bool HasResized() const { return _hasResized; }

        /** @brief The presentation surface the swap chain was created for. */
        [[nodiscard]] const kor::Surface& RenderSurface() const { return *_surface; }

        /**
         * @brief Loads an image from disk and makes it the window's icon.
         *
         * Replaces any icon set earlier. A file that fails to load leaves the icon unchanged.
         */
        void SetIcon(const std::filesystem::path& iconPath);

        /** @brief Whether the window currently has keyboard focus. */
        [[nodiscard]] bool IsFocused() const { return _focused; }

        /** @brief Whether the frame being built draws into this window: it had an image when the frame started. */
        [[nodiscard]] bool IsShownThisFrame() const { return _shownThisFrame; }

    private:
        /** @brief Opens the OS window, its surface, swap chain and default framebuffer. The application's to call. */
        explicit Window(const WindowSettings& settings);

        /** @brief Ends the frame's one-frame flags: HasResized() among them. */
        void LateUpdate();

        /** @brief Hands the surface and OS window over, for the application to destroy once no frame uses them. */
        void Retire(std::shared_ptr<kor::Surface>& surface, GLFWwindow*& window);

    	static void FramebufferResize(GLFWwindow* handle, int width, int height);
        static void CloseRequested(GLFWwindow* handle);

        GLFWwindow* _window = nullptr;
        GLFWmonitor* _monitor = nullptr;
        GLFWimage* _icon = nullptr;
        const GLFWvidmode *_videoMode = nullptr;
        std::shared_ptr<kor::Surface> _surface;

        std::string _title;
        glm::uvec2 _extent;
        bool _resizable;
        bool _fullscreen;
        bool _decorated;
        bool _transparentFramebuffer;
        bool _vsync;

        kor::Resource<kor::Framebuffer> _framebuffer;

        bool _paused = false;
        bool _closeRequested = false;
        bool _focused = true;
        bool _hasResized = false;
        bool _shownThisFrame = false;
    };
}
