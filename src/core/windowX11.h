//
// What kor::Window asks of X11 itself, where GLFW has no way to ask it. Plain types only: Xlib's
// headers define names (None, Bool, Status, Always) that nothing else should have to live with.
//
#pragma once

#include <span>
#include <kmath/matrix.h>

struct GLFWwindow;

namespace kor::x11 {
    /** Which part of a window the pointer has hold of: _NET_WM_MOVERESIZE's directions. */
    enum class Grip : int { eTopLeft = 0, eTop, eTopRight, eRight, eBottomRight, eBottom, eBottomLeft, eLeft, eMove };

    /** Whether GLFW is running on X11 (XWayland included) in a build that can talk to it. */
    [[nodiscard]] bool Active();

    /**
     * Hands a press that is still down to the window manager, to move the window or to resize it by
     * an edge. The release is the window manager's from then on; with @p releaseHere the window is
     * told of one itself, so that what saw the press is not left holding it.
     */
    void BeginMoveResize(GLFWwindow* window, Grip grip, bool releaseHere);

    /** The only places of the window the pointer lands on, in pixels (x, y, width, height); elsewhere it goes through. */
    void SetInputRegion(GLFWwindow* window, std::span<const kor::IVec4> rects);

    /** Where the pointer is on the desktop, in pixels: asked of the server, whatever window it is over. */
    [[nodiscard]] kor::IVec2 CursorPosition();

    /**
     * Says of a window not yet shown that showing it is not the user's doing, so the window manager
     * leaves the keyboard where it is. (A window that takes it takes it from one with a button held
     * down — and GLFW lets go of every button of a window that loses the keyboard.)
     */
    void ShowWithoutFocus(GLFWwindow* window);

    /** Keeps the window out of the taskbar, the pager and the window switcher. Before it is shown, or after. */
    void SkipTaskbar(GLFWwindow* window);
}
