//
// Created by radue on 10/13/2024.
//

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <filesystem>
#include <glm/glm.hpp>

#include "api.h"
#include "resource.h"

namespace kor
{
    class Surface;
    class Framebuffer;
    class Image;
    class App;
    class Input;
    namespace vk { class Scheduler; }
}

struct GLFWmonitor;
struct GLFWwindow;
struct GLFWimage;
struct GLFWvidmode;

namespace kor {
    struct WindowSettings;
    struct OffscreenSettings;

    /**
     * @brief Where a scene is drawn: an OS window — the surface it presents to, its swap chain and
     *        its default framebuffer — or an image of the application's (an *offscreen* window).
     *
     * Every scene has exactly one, opened by the application along with the scene (App::Open,
     * App::OpenOffscreen, Navigator::Open) and reached inside the scene as `Window::` (Scene::Window)
     * or from outside as Scene::SceneWindow(). Replacing the scene in it (Navigator::Replace, Push)
     * keeps the window; the window closes with the last scene shown in it.
     *
     * An offscreen window is the same to the scene drawn into it — its default framebuffer, the frame
     * graph's Screen, a render pass opened without a framebuffer — but has no OS window: what the
     * scene draws stays in Image(), for another scene to show (an editor's viewport, a preview) or for
     * the program to read. It is shown every frame it is not paused, sized by Resize(), and gets its
     * input from whoever feeds it (Input::FeedKey and the rest, kgui::SceneView).
     *
     * Neither copyable nor thread-safe: everything here is called from the thread the application
     * runs on.
     */
    class KORAL_API Window {
        friend class Input;
        friend class App;
        friend class View;
        friend class kor::vk::Scheduler;
    public:
        /**
         * @brief What a window can present in: the formats a display offers, which an image cannot
         *        be created with. @see Image::Format for those.
         *
         * The BGRA ones are what nearly every desktop display offers first. A window's image in one
         * reports the Image::Format of the same size and encoding (eRGBA8_UNORM for eBGRA8_UNORM),
         * with Image::IsBgrOrder() saying the red and blue channels are swapped in memory.
         */
        enum class Format : std::uint8_t {
            eBGRA8_UNORM,
            eBGRA8_SRGB,   ///< Encoded to sRGB by the hardware on every write.
            eRGBA8_UNORM,
            eRGBA8_SRGB,
        };

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

        /** @brief Whether it is an image rather than an OS window. @see App::OpenOffscreen */
        [[nodiscard]] bool IsOffscreen() const { return _offscreen; }

        /**
         * @brief What the scene in it draws: its default framebuffer's colour image.
         *
         * For an offscreen window, the picture itself — sampled by another scene, shown in an
         * interface, copied or saved. Made with Image::Usage::eSampled, eTransferSrc and eTransferDst
         * as well as eColorAttachment. Replaced on a resize; its generation says so.
         */
        [[nodiscard]] ResourceRef<const kor::Image> Image() const;

        /**
         * @brief Asks for a new size for the drawable area.
         *
         * An offscreen window takes it at the start of the next frame, when the scene in it gets
         * OnResize. An OS window asks the platform, which may or may not agree; the resize then
         * arrives as the user's would.
         */
        void Resize(glm::uvec2 extent);

        /** @brief The underlying GLFW window handle, for code that has to talk to GLFW directly. Null offscreen. */
        [[nodiscard]] GLFWwindow* operator*() const { return _window; }

        /** @brief Current size of the drawable area in pixels, which is not the window's outer size on a scaled display. */
        [[nodiscard]] glm::uvec2 Extent() const { return _extent; }

        /**
         * @brief Whether the window currently has no drawable area, i.e. it is minimized.
         *
         * The scene shown in it is not updated or drawn while this holds.
         */
        [[nodiscard]] bool IsPaused() const { return _paused || _iconified; }

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
        /**
         * @brief Whether the window really is see-through: it was opened with a transparent framebuffer
         *        and the display blends what it presents with what is behind it. A display that cannot
         *        leaves such a window opaque — which an overlay over the desktop has to know.
         */
        [[nodiscard]] bool CompositesWithDesktop() const;
        /** @brief The format the window presents in: the first of WindowSettings::formats the display offers. */
        [[nodiscard]] Format PixelFormat() const;
        /** @brief The formats it was asked to present in, most wanted first. */
        [[nodiscard]] const std::vector<Format>& RequestedFormats() const { return _formats; }

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

        /** @brief Whether the pointer is over the window's drawable area. False for an offscreen window. */
        [[nodiscard]] bool IsHovered() const;

        /**
         * @brief Where the window's drawable area starts on the desktop, in screen coordinates — (0, 0)
         *        where the platform does not say (Wayland) or for an offscreen window. @see CanBePositioned
         */
        [[nodiscard]] glm::ivec2 Position() const;
        /** @brief Moves the window. Nothing happens where the platform does not let a client place its windows. */
        void SetPosition(glm::ivec2 position);
        /** @brief Brings the window to the front and gives it the keyboard, where the platform allows. */
        void Focus();

        /**
         * @brief Whether the pointer goes through the window to what is behind it. While it does the
         *        window hears nothing of the pointer — ask CursorPosition where it is, and take the
         *        pointer back when it is over something of yours.
         */
        void SetMousePassthrough(bool passthrough);
        [[nodiscard]] bool IsMousePassthrough() const { return _mousePassthrough; }
        /**
         * @brief Says which parts of the window the pointer lands on — @p rects, each x, y, width and
         *        height in pixels — and lets it through everywhere else. Unlike SetMousePassthrough the
         *        window then hears of the pointer by itself, over those parts, with nothing to ask.
         * @return Whether the platform has such a thing (X11). Where it has none nothing changes, and
         *         SetMousePassthrough is what there is.
         */
        bool SetInputRegion(std::span<const glm::ivec4> rects);
        /**
         * @brief Where the pointer is, relative to the window's drawable area, in pixels — wherever it
         *        is on the desktop, over this window or not, and whether or not the window lets it
         *        through. (0, 0) for an offscreen window.
         */
        [[nodiscard]] glm::vec2 CursorPosition() const;
        /**
         * @brief Pixels of the drawable area per screen coordinate: 2 on a Retina display (or a Wayland
         *        output scaled 2x), 1 where screen coordinates are pixels (Windows, X11) or offscreen.
         *        Extent() is in pixels, Position() and the desktop in screen coordinates.
         */
        [[nodiscard]] float PixelRatio() const;

        /** @brief What the pointer looks like over a window. */
        enum class Cursor : std::uint8_t {
            eArrow,
            eResizeHorizontal,      ///< ↔ : over something dragged sideways — the line between two side-by-side panes.
            eResizeVertical,        ///< ↕ : over something dragged up and down.
            eResizeDiagonal,        ///< ⤡ : over a corner that resizes both ways.
            eHand,
            eText,
        };
        /**
         * @brief Sets what the pointer looks like over this window, until it is set again. Nothing happens
         *        for an offscreen window. (Const: it is how the window looks to the pointer, not what the
         *        window is — and what draws in a window, and knows what is under the pointer, has it const.)
         */
        void SetCursor(Cursor cursor) const;

        /**
         * @brief Colours the window's own title bar — the system's, with its buttons — so that it goes
         *        with what is drawn under it: @p background behind @p text, each 0xRRGGBB's parts from 0
         *        to 1. A dark background also asks for the system's dark buttons and menu. Where the
         *        system does not colour title bars (before Windows 11, which only goes dark or light;
         *        other platforms), and for an offscreen window, as much of it happens as can.
         */
        void SetTitleBarColors(glm::vec3 background, glm::vec3 text) const;

        /**
         * @brief Takes the system's title bar away (true) so that the application draws its own: the
         *        window keeps its frame — resized by its edges, snapped, shadowed, rounded as the system
         *        does — and all of it, to its top edge, is the application's to draw in. What is drawn
         *        there moves the window with BeginMove and has buttons that call Minimize,
         *        ToggleMaximize and RequestClose. (kui::TitleBar is one.) Nothing happens for an
         *        offscreen window. On macOS the system's own buttons stay, at the bar's left (see
         *        SystemButtonsWidth), and the window keeps all it does; on Linux the window loses its
         *        decoration altogether.
         */
        void SetCustomTitleBar(bool custom) const;
        [[nodiscard]] bool HasCustomTitleBar() const { return _customTitleBar; }
        /**
         * @brief How much of the left of a custom title bar the system's own window buttons take, in
         *        screen coordinates: on macOS, where they stay, the room to leave them. 0 elsewhere, where
         *        the application draws its own.
         */
        [[nodiscard]] float SystemButtonsWidth() const;
        /**
         * @brief Lets the system move the window with the pointer, as dragging its title bar does —
         *        snapping to the screen's edges and all. Called when the button goes down on what
         *        stands for the title bar; the button's release goes to the system, and the window is
         *        told the button is up. (Windows, macOS, X11 and Wayland.)
         */
        void BeginMove() const;
        void Minimize() const;
        /** @brief Fills the screen's work area, or goes back to the size it had. */
        void ToggleMaximize() const;
        [[nodiscard]] bool IsMaximized() const;
        /** @brief Asks the window to close, as its close button does — from what only has it const. @see Close */
        void RequestClose() const;

        /** @brief A monitor's place on the desktop, in screen coordinates. */
        struct MonitorArea { glm::ivec2 position {}; glm::ivec2 size {}; };
        /** @brief The monitor most of the window is on (the primary one for an offscreen window, or where windows cannot say where they are). */
        [[nodiscard]] MonitorArea Monitor() const;
        /** @brief The whole desktop: the smallest area that holds every monitor. */
        [[nodiscard]] static MonitorArea Desktop();

        /**
         * @brief Where the pointer is on the desktop, in screen coordinates, asked of the system itself:
         *        right whichever window the pointer is over, and while a window is being moved under
         *        it — which what a window was last told of the pointer is not. Nothing where the
         *        platform has no such thing to ask (anything but X11, for now).
         */
        [[nodiscard]] static std::optional<glm::ivec2> DesktopCursor();

        /** @brief The text on the system's clipboard, as UTF-8: empty when it holds none, or there is no windowing system. */
        [[nodiscard]] static std::string ClipboardText();
        /** @brief Puts @p text on the system's clipboard. */
        static void SetClipboardText(const std::string& text);
        /**
         * @brief Whether windows here can be asked where they are and put somewhere: true on Windows,
         *        macOS and X11, false on Wayland, where only the compositor places windows.
         */
        [[nodiscard]] static bool CanBePositioned();

        /** @brief Whether the frame being built draws into this window: it had an image when the frame started. */
        [[nodiscard]] bool IsShownThisFrame() const { return _shownThisFrame; }

    private:
        /** @brief Opens the OS window, its surface, swap chain and default framebuffer. The application's to call. */
        explicit Window(const WindowSettings& settings);
        /** @brief Makes an offscreen window: its image and default framebuffer. The application's to call. */
        explicit Window(const OffscreenSettings& settings);

        /** @brief Takes a size asked for with Resize, for an offscreen window. At the start of a frame. */
        void ApplyResize();

        /** @brief Ends the frame's one-frame flags: HasResized() among them. */
        void LateUpdate();

        /** @brief Hands the surface and OS window over, for the application to destroy once no frame uses them. */
        void Retire(std::shared_ptr<kor::Surface>& surface, GLFWwindow*& window);

    	static void FramebufferResize(GLFWwindow* handle, int width, int height);
        static void CloseRequested(GLFWwindow* handle);
        static void Iconified(GLFWwindow* handle, int iconified);
        /**
         * The edges of a window with a title bar of its own, where the system gives it none to resize
         * it by (X11): the pointer over one shows it, and a press there resizes the window. Input's to
         * call, with what the window heard; true when the edge has it, and nothing else should.
         */
        static bool FramePointerMoved(GLFWwindow* handle, double x, double y);
        static bool FramePressed(GLFWwindow* handle);
        void ShowCursor() const;

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
        bool _mousePassthrough = false;
        mutable Cursor _cursor = Cursor::eArrow;
        mutable bool _customTitleBar = false;
        mutable bool _frameChanged = false;     ///< The frame is to be worked out again, after this frame.
        mutable int _frameGrip = -1;            ///< The edge the pointer is over, as kor::x11::Grip; -1 for none.
        std::optional<std::vector<glm::ivec4>> _inputRegion;
        bool _vsync;
        std::vector<Format> _formats;

        kor::Resource<kor::Framebuffer> _framebuffer;

        bool _paused = false;
        bool _iconified = false;        ///< Minimized: not drawn into, whatever size it is said to have.
        bool _closeRequested = false;
        bool _focused = true;
        bool _hasResized = false;
        bool _shownThisFrame = false;

        bool _offscreen = false;
        Format _offscreenFormat = Format::eRGBA8_UNORM;
        std::optional<glm::uvec2> _requestedExtent;
        kor::Resource<kor::Image> _offscreenColor;
        kor::Resource<kor::Image> _offscreenDepth;
    };

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
        bool alwaysOnTop = false;                   ///< Whether it stays above every other window.
        bool mousePassthrough = false;              ///< Whether the pointer goes through it. @see Window::SetMousePassthrough
        bool focusOnOpen = true;                    ///< Whether opening it takes the keyboard from whatever had it.
        bool taskbar = true;                        ///< Whether it has a button in the taskbar (Windows; elsewhere the platform's choice).
        bool vsync = true;                          ///< Whether presentation waits for the display's refresh.
        /// Where on the desktop to open it, in screen coordinates; centred on the primary monitor when
        /// not given. Ignored where the platform places windows itself. @see Window::CanBePositioned
        std::optional<glm::ivec2> position;
        /**
         * The formats to present in, most wanted first; the first the display offers is used. The
         * default is plain 8-bit: a scene that writes sRGB-encoded values itself (a tone-mapping
         * pass, say) wants UNORM, one that wants the hardware to encode on write wants SRGB.
         */
        std::vector<Window::Format> formats = { Window::Format::eBGRA8_UNORM, Window::Format::eRGBA8_UNORM };
    };

    /**
     * @brief What an offscreen window is made with. @see App::OpenOffscreen
     *
     * @code
     * auto* game = app.OpenOffscreen("Level", {.extent = {1280, 720}});
     * // in the editor's RenderUI: _view.Draw("Game", *game);   (kgui::SceneView)
     * @endcode
     */
    struct OffscreenSettings {
        std::string title = "Offscreen";            ///< What Window::Title() says; nothing shows it.
        glm::uvec2 extent = { 1280, 720 };          ///< Initial size of the image, in pixels.
        /**
         * The image's format: eRGBA8_UNORM or eRGBA8_SRGB. An image is never BGRA, so a BGRA one is
         * taken as its RGBA counterpart.
         */
        Window::Format format = Window::Format::eRGBA8_UNORM;
    };
}
