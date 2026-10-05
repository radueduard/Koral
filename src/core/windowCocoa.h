//
// What kor::Window asks of AppKit itself, where GLFW has no way to ask it. Plain types only, so that
// window.cpp stays C++: the other side is windowCocoa.mm, built on macOS alone.
//
#pragma once

struct GLFWwindow;

namespace kor::cocoa {
    /**
     * Gives the window's title bar to what is drawn in it (true), or back. Unlike elsewhere the window
     * stays titled: the bar goes transparent and the content runs under it, so the window is still
     * resized, minimized, zoomed and tiled by the system — and its own three buttons stay at the left.
     */
    void SetCustomTitleBar(GLFWwindow* window, bool custom);

    /** Lets the system move the window with the pointer, whose button is down; the window is told it is up. */
    void BeginMove(GLFWwindow* window);

    /** How much of the title bar's left the system's own buttons take, margins included, in screen coordinates. */
    [[nodiscard]] float WindowButtonsWidth(GLFWwindow* window);
}
