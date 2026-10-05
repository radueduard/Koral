//
// Created by radue on 10/13/2024.
//
#include <window.h>
#include <framebuffer.h>
#include <image.h>
#include <imageView.h>

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include <stb_image.h>
#include <GLFW/glfw3.h>
#ifdef _WIN32
// For the one thing GLFW has no hint for: a window with no taskbar button.
#define GLFW_EXPOSE_NATIVE_WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <GLFW/glfw3native.h>
#endif

#include "context.h"
#include "windowX11.h"
#ifdef __APPLE__
#include "windowCocoa.h"
#endif
#include "scheduler.h"
#include "surface.h"
#include "../backends/vulkan/surface.h"

namespace kor {
    Window::Window(const WindowSettings& settings) :
        _title(settings.title),
        _extent(settings.extent),
        _resizable(settings.resizable),
        _fullscreen(settings.fullscreen),
        _decorated(settings.decorated),
        _transparentFramebuffer(settings.transparentFramebuffer),
        _vsync(settings.vsync),
        _formats(settings.formats)
    {
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, _resizable);
        glfwWindowHint(GLFW_DECORATED, _decorated);
        glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, _transparentFramebuffer);
        glfwWindowHint(GLFW_FLOATING, settings.alwaysOnTop);
        glfwWindowHint(GLFW_MOUSE_PASSTHROUGH, settings.mousePassthrough);
        glfwWindowHint(GLFW_FOCUSED, settings.focusOnOpen);
        glfwWindowHint(GLFW_FOCUS_ON_SHOW, settings.focusOnOpen);
        // On X11 a window is given the keyboard by the window manager as it is shown, whatever GLFW was
        // told: one that must not take it is made unseen, says so itself, and is shown once it has.
        const bool quiet = !settings.focusOnOpen && !_fullscreen && x11::Active();
        if (quiet) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        _mousePassthrough = settings.mousePassthrough;
        // The framebuffer is in physical pixels on a scaled display (GLFW 3.4's default, stated).
        glfwWindowHint(GLFW_SCALE_FRAMEBUFFER, GLFW_TRUE);

        if (_fullscreen) {
            _monitor = glfwGetPrimaryMonitor();
            if (_monitor) {
                _videoMode = glfwGetVideoMode(_monitor);
                _extent = { static_cast<glm::u32>(_videoMode->width), static_cast<glm::u32>(_videoMode->height) };
            }
        }

        _window = glfwCreateWindow(static_cast<int>(_extent.x), static_cast<int>(_extent.y),
                                   _title.c_str(), _monitor, nullptr);
        if (_window == nullptr) {
            const char* description = nullptr;
            glfwGetError(&description);
            throw std::runtime_error(std::string("GLFW could not create the window: ")
                                     + (description ? description : "no reason given"));
        }

#ifdef _WIN32
        // A window with no taskbar button: a tool window, which is also kept out of Alt+Tab.
        if (!settings.taskbar) {
            const HWND handle = glfwGetWin32Window(_window);
            LONG_PTR style = GetWindowLongPtrW(handle, GWL_EXSTYLE);
            style = (style | WS_EX_TOOLWINDOW) & ~static_cast<LONG_PTR>(WS_EX_APPWINDOW);
            ShowWindow(handle, SW_HIDE);
            SetWindowLongPtrW(handle, GWL_EXSTYLE, style);
            ShowWindow(handle, settings.focusOnOpen ? SW_SHOW : SW_SHOWNA);
        }
#else
        if (!settings.taskbar) x11::SkipTaskbar(_window);
#endif
        // Said again once the window is what it will be: the styles above are set after the hint was applied.
        if (settings.mousePassthrough) glfwSetWindowAttrib(_window, GLFW_MOUSE_PASSTHROUGH, GLFW_TRUE);

        glfwSetWindowUserPointer(_window, this);
        glfwSetFramebufferSizeCallback(_window, FramebufferResize);
        glfwSetWindowCloseCallback(_window, CloseRequested);
        glfwSetWindowIconifyCallback(_window, Iconified);

        // On Wayland the framebuffer size is not valid until the compositor has configured the
        // surface. Pumped briefly rather than waited out: a window opened from inside a running
        // application should not stall every other one. One that is still unsized simply is not
        // shown until it has a size.
        int width = 0, height = 0;
        glfwGetFramebufferSize(_window, &width, &height);
        for (int attempt = 0; (width == 0 || height == 0) && attempt < 50 && !glfwWindowShouldClose(_window); ++attempt) {
            glfwWaitEventsTimeout(0.01);
            glfwGetFramebufferSize(_window, &width, &height);
        }
        if (width == 0 || height == 0) {
            _paused = true;
        } else {
            _extent = { static_cast<glm::u32>(width), static_cast<glm::u32>(height) };
        }
        _focused = glfwGetWindowAttrib(_window, GLFW_FOCUSED) == GLFW_TRUE;

        // Centred on the primary monitor, where the platform lets a client place its window at all.
        if (!_fullscreen && settings.position && CanBePositioned()) {
            glfwSetWindowPos(_window, settings.position->x, settings.position->y);
        } else if (!_fullscreen && glfwGetPlatform() != GLFW_PLATFORM_WAYLAND) {
            if (const auto primaryMonitor = glfwGetPrimaryMonitor()) {
                if (const auto videoMode = glfwGetVideoMode(primaryMonitor)) {
                    int monitorX, monitorY;
                    glfwGetMonitorPos(primaryMonitor, &monitorX, &monitorY);
                    glfwSetWindowPos(_window, monitorX + (videoMode->width - static_cast<int>(settings.extent.x)) / 2,
                                     monitorY + (videoMode->height - static_cast<int>(settings.extent.y)) / 2);
                }
            }
        }
        if (quiet) {
            x11::ShowWithoutFocus(_window);
            glfwShowWindow(_window);
            _focused = false;
        }
    }

    bool Window::CanBePositioned() { return glfwGetPlatform() != GLFW_PLATFORM_WAYLAND && glfwGetPlatform() != GLFW_PLATFORM_NULL; }

    bool Window::IsHovered() const { return _window != nullptr && glfwGetWindowAttrib(_window, GLFW_HOVERED) == GLFW_TRUE; }

    glm::ivec2 Window::Position() const
    {
        if (_window == nullptr || !CanBePositioned()) return {};
        int x = 0, y = 0;
        glfwGetWindowPos(_window, &x, &y);
        return { x, y };
    }

    void Window::SetPosition(const glm::ivec2 position)
    {
        if (_window != nullptr && CanBePositioned()) glfwSetWindowPos(_window, position.x, position.y);
    }

    void Window::Focus()
    {
        if (_window != nullptr) glfwFocusWindow(_window);
    }

    void Window::SetMousePassthrough(const bool passthrough)
    {
        if (_window == nullptr || passthrough == _mousePassthrough) return;
        _mousePassthrough = passthrough;
        _inputRegion.reset();
        glfwSetWindowAttrib(_window, GLFW_MOUSE_PASSTHROUGH, passthrough);
    }

    std::optional<glm::ivec2> Window::DesktopCursor()
    {
        if (!x11::Active()) return std::nullopt;
        return x11::CursorPosition();
    }

    bool Window::SetInputRegion(const std::span<const glm::ivec4> rects)
    {
        if (_window == nullptr || !x11::Active()) return false;
        // Asked every frame by what follows something that moves: the server is told only of a change.
        if (_inputRegion && std::ranges::equal(*_inputRegion, rects)) return true;
        _inputRegion.emplace(rects.begin(), rects.end());
        x11::SetInputRegion(_window, rects);
        return true;
    }

    glm::vec2 Window::CursorPosition() const
    {
        if (_window == nullptr) return {};
        double x = 0., y = 0.;
        glfwGetCursorPos(_window, &x, &y);
        // GLFW answers in screen coordinates, which are not pixels where the display is scaled (macOS, Wayland).
        int width = 0, height = 0;
        glfwGetWindowSize(_window, &width, &height);
        const glm::vec2 scale = width > 0 && height > 0 ? glm::vec2(_extent) / glm::vec2(width, height) : glm::vec2(1.f);
        return glm::vec2(x, y) * scale;
    }

    float Window::PixelRatio() const
    {
        if (_window == nullptr) return 1.f;
        int width = 0, height = 0;
        glfwGetWindowSize(_window, &width, &height);
        return width > 0 && _extent.x > 0 ? static_cast<float>(_extent.x) / static_cast<float>(width) : 1.f;
    }

    Window::MonitorArea Window::Monitor() const
    {
        int count = 0;
        GLFWmonitor** monitors = glfwGetMonitors(&count);
        GLFWmonitor* best = glfwGetPrimaryMonitor();
        if (_window != nullptr && CanBePositioned()) {
            int x = 0, y = 0, width = 0, height = 0;
            glfwGetWindowPos(_window, &x, &y);
            glfwGetWindowSize(_window, &width, &height);
            long long most = 0;
            for (int i = 0; i < count; ++i) {
                const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
                if (!mode) continue;
                int mx = 0, my = 0;
                glfwGetMonitorPos(monitors[i], &mx, &my);
                const long long w = std::min(x + width, mx + mode->width) - std::max(x, mx);
                const long long h = std::min(y + height, my + mode->height) - std::max(y, my);
                if (w > 0 && h > 0 && w * h > most) { most = w * h; best = monitors[i]; }
            }
        }
        MonitorArea area;
        if (best) {
            glfwGetMonitorPos(best, &area.position.x, &area.position.y);
            if (const GLFWvidmode* mode = glfwGetVideoMode(best)) area.size = { mode->width, mode->height };
        }
        return area;
    }

#if defined(__linux__) && !defined(__ANDROID__)
}
// GLFW's own, as Koral's port of it has it (vcpkg-overlay-ports/glfw3): the Wayland compositor is asked
// to move the window (edges < 0) or to resize it by an XDG_TOPLEVEL_RESIZE_EDGE_*.
extern "C" void glfwKoralWaylandMoveResize(GLFWwindow* window, int edges);
namespace kor {
#endif
    namespace {
        /**
         * Hands the press that is still down to whatever moves windows here — the window manager on X11,
         * the compositor on Wayland — to move the window, or to resize it by an edge. With @p releaseHere
         * the window is told the button is up on X11 too (on Wayland it always is).
         */
        void moveOrResize(GLFWwindow* window, const x11::Grip grip, const bool releaseHere)
        {
            if (x11::Active()) { x11::BeginMoveResize(window, grip, releaseHere); return; }
#if defined(__linux__) && !defined(__ANDROID__)
            if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) {
                // XDG_TOPLEVEL_RESIZE_EDGE_*, in x11::Grip's order: clockwise from the top left.
                static constexpr int edges[8] = { 5, 1, 9, 8, 10, 2, 6, 4 };
                glfwKoralWaylandMoveResize(window, grip == x11::Grip::eMove ? -1 : edges[static_cast<int>(grip)]);
            }
#endif
        }

        /** Whether a window with no frame of the system's can still be moved and resized here: by asking. */
        bool movesByAsking()
        {
#if defined(__linux__) && !defined(__ANDROID__)
            return x11::Active() || glfwGetPlatform() == GLFW_PLATFORM_WAYLAND;
#else
            return false;
#endif
        }

        // The windows whose edges are their own to watch (FramePointerMoved): GLFW's callbacks are
        // handed a window and nothing else, and not every window's user pointer is a kor::Window.
        std::unordered_map<GLFWwindow*, const Window*>& framed()
        {
            static std::unordered_map<GLFWwindow*, const Window*> table;
            return table;
        }
    }

#ifdef _WIN32
    namespace {
        constexpr wchar_t FrameProcedure[] = L"KoralWindowProcedure";

        /**
         * A window with no title bar of the system's, and still its frame: the client area is all of the
         * window up to its top edge, and the top edge still resizes it. Everything else is as it was.
         */
        LRESULT CALLBACK CustomFrame(const HWND window, const UINT message, const WPARAM wParam, const LPARAM lParam)
        {
            const auto original = reinterpret_cast<WNDPROC>(GetPropW(window, FrameProcedure));
            if (!original) return DefWindowProcW(window, message, wParam, lParam);
            // How thick the frame's resizing edge is: what a maximized window hangs over the screen by.
            const int frame = GetSystemMetrics(SM_CYSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
            switch (message) {
            case WM_NCCALCSIZE:
                // Minimized, it is the system's to size: it has no client area then, and must have none.
                if (wParam && !IsIconic(window)) {
                    // The system's own sides and bottom (where the edges that resize it are), and no top:
                    // the client area starts where the window does — or, maximized, where the screen does.
                    auto* sizes = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
                    const LONG top = sizes->rgrc[0].top;
                    const LRESULT result = CallWindowProcW(original, window, message, wParam, lParam);
                    sizes->rgrc[0].top = top + (IsZoomed(window) ? frame : 0);
                    return result;
                }
                break;
            case WM_NCHITTEST: {
                const LRESULT hit = CallWindowProcW(original, window, message, wParam, lParam);
                if (hit != HTCLIENT || IsZoomed(window) || !(GetWindowLongPtrW(window, GWL_STYLE) & WS_THICKFRAME)) return hit;
                // Along the top, where the title bar's own edge was: it resizes the window still.
                POINT at { static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
                ScreenToClient(window, &at);
                RECT client;
                GetClientRect(window, &client);
                const int band = std::min(frame, 6);
                if (at.y >= band) return hit;
                if (at.x < 2 * band) return HTTOPLEFT;
                if (at.x >= client.right - 2 * band) return HTTOPRIGHT;
                return HTTOP;
            }
            case WM_NCDESTROY:
                SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original));
                RemovePropW(window, FrameProcedure);
                break;
            default: break;
            }
            return CallWindowProcW(original, window, message, wParam, lParam);
        }
    }
#endif

    void Window::SetCustomTitleBar(const bool custom) const
    {
        if (_window == nullptr || custom == _customTitleBar) return;
        _customTitleBar = custom;
#ifdef _WIN32
        const HWND handle = glfwGetWin32Window(_window);
        if (custom) {
            SetPropW(handle, FrameProcedure, reinterpret_cast<HANDLE>(GetWindowLongPtrW(handle, GWLP_WNDPROC)));
            SetWindowLongPtrW(handle, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&CustomFrame));
        } else if (const auto original = reinterpret_cast<LONG_PTR>(GetPropW(handle, FrameProcedure))) {
            SetWindowLongPtrW(handle, GWLP_WNDPROC, original);
            RemovePropW(handle, FrameProcedure);
        }
        // The frame is worked out again — the client area grows over where the title bar was, or gives it
        // back — once the frame being drawn is done with: that resizes what is drawn into.
        _frameChanged = true;
#elif defined(__APPLE__)
        cocoa::SetCustomTitleBar(_window, custom);
#else
        glfwSetWindowAttrib(_window, GLFW_DECORATED, custom ? GLFW_FALSE : GLFW_TRUE);
        // The frame went with the title bar, and what resized the window with it: its edges are
        // the window's own to watch from here (FramePointerMoved). On X11 the window manager is
        // asked to do the resizing, and on Wayland the compositor.
        if (movesByAsking()) {
            if (custom) framed()[_window] = this;
            else {
                framed().erase(_window);
                _frameGrip = -1;
                ShowCursor();
            }
        }
#endif
    }

    bool Window::FramePointerMoved(GLFWwindow* handle, const double x, const double y)
    {
        const auto it = framed().find(handle);
        if (it == framed().end()) return false;
        const Window& window = *it->second;
        int grip = -1;
        // Not with the button down: that is something being dragged to the edge, not the edge taken hold of.
        if (window._resizable && !window._fullscreen && !glfwGetWindowAttrib(handle, GLFW_MAXIMIZED)
            && glfwGetMouseButton(handle, GLFW_MOUSE_BUTTON_LEFT) != GLFW_PRESS
            && glfwGetInputMode(handle, GLFW_CURSOR) == GLFW_CURSOR_NORMAL) {
            int width = 0, height = 0;
            glfwGetWindowSize(handle, &width, &height);
            // As wide as an edge of the system's, and twice that along the edge for a corner.
            constexpr double band = 6., corner = 2. * band;
            const bool left = x < band, right = x >= width - band, top = y < band, bottom = y >= height - band;
            const bool nearLeft = x < corner, nearRight = x >= width - corner, nearTop = y < corner, nearBottom = y >= height - corner;
            using enum x11::Grip;
            if ((top && nearLeft) || (left && nearTop)) grip = static_cast<int>(eTopLeft);
            else if ((top && nearRight) || (right && nearTop)) grip = static_cast<int>(eTopRight);
            else if ((bottom && nearLeft) || (left && nearBottom)) grip = static_cast<int>(eBottomLeft);
            else if ((bottom && nearRight) || (right && nearBottom)) grip = static_cast<int>(eBottomRight);
            else if (top) grip = static_cast<int>(eTop);
            else if (bottom) grip = static_cast<int>(eBottom);
            else if (left) grip = static_cast<int>(eLeft);
            else if (right) grip = static_cast<int>(eRight);
        }
        if (grip != window._frameGrip) {
            window._frameGrip = grip;
            window.ShowCursor();
        }
        return grip >= 0;
    }

    bool Window::FramePressed(GLFWwindow* handle)
    {
        const auto it = framed().find(handle);
        if (it == framed().end() || it->second->_frameGrip < 0) return false;
        // The press is not told to anything else, so there is no release for anything to wait on.
        moveOrResize(handle, static_cast<x11::Grip>(it->second->_frameGrip), false);
        return true;
    }

    void Window::BeginMove() const
    {
        if (_window == nullptr) return;
#ifdef _WIN32
        const HWND handle = glfwGetWin32Window(_window);

        POINT screen;
        GetCursorPos(&screen);
        POINT client = screen;
        ScreenToClient(handle, &client);
        // In the window's own queue, in order: first the button is up, as far as the window knows — the
        // system's move takes the real release, which would otherwise never be heard of — and then it is
        // down on a title bar, which is how the system is asked to move a window.
        PostMessageW(handle, WM_LBUTTONUP, 0, MAKELPARAM(client.x, client.y));
        PostMessageW(handle, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(screen.x, screen.y));
#elif defined(__APPLE__)
        cocoa::BeginMove(_window);
#else
        moveOrResize(_window, x11::Grip::eMove, true);
#endif
    }

    float Window::SystemButtonsWidth() const
    {
#ifdef __APPLE__
        return _window ? cocoa::WindowButtonsWidth(_window) : 0.f;
#else
        return 0.f;
#endif
    }

    void Window::Minimize() const { if (_window) glfwIconifyWindow(_window); }

    void Window::ToggleMaximize() const
    {
        if (_window == nullptr) return;
        if (glfwGetWindowAttrib(_window, GLFW_MAXIMIZED)) glfwRestoreWindow(_window);
        else glfwMaximizeWindow(_window);
    }

    bool Window::IsMaximized() const { return _window && glfwGetWindowAttrib(_window, GLFW_MAXIMIZED); }

    void Window::RequestClose() const { if (_window) glfwSetWindowShouldClose(_window, GLFW_TRUE); }

    void Window::SetTitleBarColors(const glm::vec3 background, const glm::vec3 text) const
    {
        if (_window == nullptr) return;
#ifdef _WIN32
        // The desktop window manager's, found when first asked for: nothing links against it, and a
        // system without it (or without an attribute) just leaves the bar as it was.
        using Set = HRESULT (WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
        static const Set set = [] {
            const HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
            return dwm ? reinterpret_cast<Set>(reinterpret_cast<void*>(GetProcAddress(dwm, "DwmSetWindowAttribute"))) : nullptr;
        }();
        if (!set) return;
        const HWND handle = glfwGetWin32Window(_window);
        const auto colour = [](const glm::vec3 c) -> COLORREF {
            const auto byte = [](const float v) { return static_cast<DWORD>(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f); };
            return byte(c.r) | (byte(c.g) << 8) | (byte(c.b) << 16);
        };
        constexpr DWORD UseDarkMode = 20, BorderColour = 34, CaptionColour = 35, TextColour = 36;   // DWMWA_*
        const BOOL dark = background.r * 0.299f + background.g * 0.587f + background.b * 0.114f < 0.5f;
        set(handle, UseDarkMode, &dark, sizeof dark);
        const COLORREF caption = colour(background), ink = colour(text);
        set(handle, CaptionColour, &caption, sizeof caption);
        set(handle, BorderColour, &caption, sizeof caption);
        set(handle, TextColour, &ink, sizeof ink);
#else
        (void) background;
        (void) text;
#endif
    }

    void Window::SetCursor(const Cursor cursor) const
    {
        if (_window == nullptr || cursor == _cursor) return;
        _cursor = cursor;
        // Over an edge that resizes the window the pointer shows that, whatever is drawn under it.
        if (_frameGrip < 0) ShowCursor();
    }

    void Window::ShowCursor() const
    {
        if (_window == nullptr) return;
        // The system's own shapes, made once each and kept: GLFW frees them when it is shut down.
        static GLFWcursor* shapes[7] = {};
        static constexpr int standard[7] = { GLFW_ARROW_CURSOR, GLFW_RESIZE_EW_CURSOR, GLFW_RESIZE_NS_CURSOR, GLFW_RESIZE_NWSE_CURSOR,
                                             GLFW_POINTING_HAND_CURSOR, GLFW_IBEAM_CURSOR, GLFW_RESIZE_NESW_CURSOR };
        auto index = static_cast<std::size_t>(_cursor);
        if (_frameGrip >= 0) {
            // In x11::Grip's order: the corners and edges, clockwise from the top left.
            static constexpr std::size_t edges[8] = { 3, 2, 6, 1, 3, 2, 6, 1 };
            index = edges[_frameGrip];
        }
        if (!shapes[index]) shapes[index] = glfwCreateStandardCursor(standard[index]);
        // A shape the platform does not have leaves the pointer an arrow, which null is.
        glfwSetCursor(_window, shapes[index]);
    }

    std::string Window::ClipboardText()
    {
        if (glfwGetPlatform() == GLFW_PLATFORM_NULL) return {};
        const char* text = glfwGetClipboardString(nullptr);
        return text ? text : "";
    }

    void Window::SetClipboardText(const std::string& text)
    {
        if (glfwGetPlatform() != GLFW_PLATFORM_NULL) glfwSetClipboardString(nullptr, text.c_str());
    }

    Window::MonitorArea Window::Desktop()
    {
        int count = 0;
        GLFWmonitor** monitors = glfwGetMonitors(&count);
        glm::ivec2 low { 0 }, high { 0 };
        bool any = false;
        for (int i = 0; i < count; ++i) {
            const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
            if (!mode) continue;
            glm::ivec2 at {};
            glfwGetMonitorPos(monitors[i], &at.x, &at.y);
            const glm::ivec2 end = at + glm::ivec2(mode->width, mode->height);
            low = any ? glm::min(low, at) : at;
            high = any ? glm::max(high, end) : end;
            any = true;
        }
        return { low, high - low };
    }

    bool Window::CompositesWithDesktop() const
    {
        if (_offscreen || !_surface || !_transparentFramebuffer) return false;
        const auto& surface = dynamic_cast<const vk::Surface&>(*_surface);
        return surface.HasSwapChain() && surface.swapChain().compositesAlpha();
    }

    Window::Window(const OffscreenSettings& settings) :
        _title(settings.title),
        _extent(glm::max(settings.extent, glm::uvec2(1))),
        _resizable(true),
        _fullscreen(false),
        _decorated(false),
        _transparentFramebuffer(false),
        _vsync(false),
        _offscreen(true)
    {
        // An image is never BGRA: those formats are a display's, not something to create.
        _offscreenFormat = settings.format == Format::eBGRA8_SRGB || settings.format == Format::eRGBA8_SRGB
            ? Format::eRGBA8_SRGB : Format::eRGBA8_UNORM;
        const auto format = _offscreenFormat == Format::eRGBA8_SRGB ? kor::Image::Format::eRGBA8_SRGB : kor::Image::Format::eRGBA8_UNORM;

        _offscreenColor = kor::Image::Builder()
            .SetExtent(_extent)
            .SetFormat(format)
            .SetUsage(kor::Image::Usage::eColorAttachment | kor::Image::Usage::eSampled
                      | kor::Image::Usage::eTransferSrc | kor::Image::Usage::eTransferDst)
            .Build();
        _offscreenDepth = kor::Image::Builder()
            .SetExtent(_extent)
            .SetFormat(kor::Image::Format::eD32_SFLOAT_S8_UINT)
            .SetUsage(kor::Image::Usage::eDepthStencilAttachment)
            .Build();
        _offscreenColor.SetName(_title + " color");
        _offscreenDepth.SetName(_title + " depth");
        // The same targets, names and clears as a window's own, so a scene cannot tell the two apart.
        _framebuffer = Framebuffer::Builder()
            .AddColor({ .name = "color", .view = _offscreenColor, .clear = glm::vec4(0.f) })
            .SetDepthStencil({ .name = "depth", .view = _offscreenDepth })
            .Build();
        if (!_framebuffer.Valid())
            throw std::runtime_error("the offscreen window's framebuffer could not be made: "
                                     + (_framebuffer.Failure() ? _framebuffer.Failure()->message : std::string("no reason given")));
    }

    Window::Format Window::PixelFormat() const
    {
        if (_offscreen) return _offscreenFormat;
        return dynamic_cast<const vk::Surface&>(*_surface).swapChain().getWindowFormat();
    }

    ResourceRef<const kor::Image> Window::Image() const
    {
        // A ref to the Resource that owns the image — which notices the window going — rather than
        // the framebuffer's (a view's pointer back to its image, which does not).
        if (_offscreen) return ResourceRef<const kor::Image>(_offscreenColor);
        if (_surface) return dynamic_cast<const vk::Surface&>(*_surface).swapChain().image();
        return {};
    }

    void Window::Resize(const glm::uvec2 extent)
    {
        if (extent.x == 0 || extent.y == 0) return;
        if (_offscreen) {
            if (extent != _extent) _requestedExtent = extent;
            else _requestedExtent.reset();
            return;
        }
        if (_window) glfwSetWindowSize(_window, static_cast<int>(extent.x), static_cast<int>(extent.y));
    }

    void Window::ApplyResize()
    {
        if (!_requestedExtent) return;
        _extent = *_requestedExtent;
        _requestedExtent.reset();
        if (_framebuffer.Valid()) _framebuffer->Resize(_extent);
        _hasResized = true;
    }

    Window::~Window() {
        framed().erase(_window);
        _framebuffer.Reset();
        _surface.reset();
        if (_window) glfwDestroyWindow(_window);
        if (_icon != nullptr) {
            stbi_image_free(_icon->pixels);
            delete _icon;
        }
    }

    void Window::Retire(std::shared_ptr<kor::Surface>& surface, GLFWwindow*& window) {
        _framebuffer.Reset();
        surface = std::move(_surface);
        framed().erase(_window);
        window = _window;
        _window = nullptr;
    }

    // Out of line: returning the ResourceRef by value instantiates kor::Framebuffer's destructor,
    // which needs it complete, and window.h does not include framebuffer.h.
    ResourceRef<Framebuffer> Window::DefaultFramebuffer() const {
        return _framebuffer;
    }

    bool Window::ShouldClose() const { return _closeRequested || (_window && glfwWindowShouldClose(_window)); }

    void Window::Close() {
        _closeRequested = true;
    }

    void Window::SetTitle(const std::string& title)
    {
        _title = title;
        if (_window) glfwSetWindowTitle(_window, title.c_str());
    }

    void Window::SetIcon(const std::filesystem::path& iconPath)
    {
        if (!_window) return;   // offscreen: nothing to put an icon on
        const auto image = new GLFWimage;
        const std::string iconPathStr = iconPath.string();
        image->pixels = stbi_load(iconPathStr.c_str(), &image->width, &image->height, nullptr, 4);
        if (image->pixels == nullptr) {
            std::cerr << "Failed to load window icon" << std::endl;
            delete image;
            return;
        }
        glfwSetWindowIcon(_window, 1, image);

        if (_icon != nullptr) {
            stbi_image_free(_icon->pixels);
            delete _icon;
        }
        _icon = image;
    }

    void Window::LateUpdate() {
        _hasResized = false;
#ifdef _WIN32
        if (_frameChanged && _window) {
            _frameChanged = false;
            // Heard of as any resize is: by the frame after this one.
            SetWindowPos(glfwGetWin32Window(_window), nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
#endif
    }

    void Window::FramebufferResize(GLFWwindow* handle, const int width, const int height) {
    	const auto window = static_cast<Window*>(glfwGetWindowUserPointer(handle));
        if (!window) return;
        window->_hasResized = true;
    	window->_extent = { static_cast<glm::u32>(width), static_cast<glm::u32>(height) };
    	if (width == 0 || height == 0) window->Pause();
    	else window->Unpause();
    }

    // A minimized window is not drawn into. Where minimizing leaves a window its size (X11) nothing else
    // says so — and presenting to a window that is not shown waits for a display that never asks for it.
    void Window::Iconified(GLFWwindow* handle, const int iconified) {
        const auto window = static_cast<Window*>(glfwGetWindowUserPointer(handle));
        if (!window) return;
        // Its own flag: a minimized window is still told its size, which would say it can be drawn into.
        window->_iconified = iconified == GLFW_TRUE;
    }

    void Window::CloseRequested(GLFWwindow* handle) {
        if (const auto window = static_cast<Window*>(glfwGetWindowUserPointer(handle))) window->_closeRequested = true;
    }
}
