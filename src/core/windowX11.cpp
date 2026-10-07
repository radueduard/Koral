//
// kor::Window on X11: what GLFW has no call for. See windowX11.h.
//
#include "windowX11.h"

#if defined(__linux__) && !defined(__ANDROID__)

#include <vector>

#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <X11/Xatom.h>
#include <X11/extensions/shape.h>

namespace kor::x11 {
    bool Active() { return glfwGetPlatform() == GLFW_PLATFORM_X11; }

    void BeginMoveResize(GLFWwindow* window, const Grip grip, const bool releaseHere)
    {
        if (!Active() || window == nullptr) return;
        Display* display = glfwGetX11Display();
        const ::Window handle = glfwGetX11Window(window);
        const ::Window root = DefaultRootWindow(display);

        ::Window pointerRoot = 0, child = 0;
        int rootX = 0, rootY = 0, x = 0, y = 0;
        unsigned int mask = 0;
        XQueryPointer(display, handle, &pointerRoot, &child, &rootX, &rootY, &x, &y, &mask);

        // The press grabbed the pointer for this window; the window manager cannot take it until it is let go.
        XUngrabPointer(display, CurrentTime);

        if (releaseHere) {
            // In the window's own queue: the button is up, as far as the window knows. The real release
            // goes to the window manager, and would otherwise never be heard of.
            XEvent release {};
            release.xbutton.type = ButtonRelease;
            release.xbutton.display = display;
            release.xbutton.window = handle;
            release.xbutton.root = root;
            release.xbutton.time = CurrentTime;
            release.xbutton.x = x;
            release.xbutton.y = y;
            release.xbutton.x_root = rootX;
            release.xbutton.y_root = rootY;
            release.xbutton.state = Button1Mask;
            release.xbutton.button = Button1;
            release.xbutton.same_screen = True;
            XSendEvent(display, handle, False, ButtonReleaseMask, &release);
        }

        XEvent message {};
        message.xclient.type = ClientMessage;
        message.xclient.window = handle;
        message.xclient.message_type = XInternAtom(display, "_NET_WM_MOVERESIZE", False);
        message.xclient.format = 32;
        message.xclient.data.l[0] = rootX;
        message.xclient.data.l[1] = rootY;
        message.xclient.data.l[2] = static_cast<long>(grip);
        message.xclient.data.l[3] = Button1;
        message.xclient.data.l[4] = 1;   // from an application, not a pager
        XSendEvent(display, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &message);
        XFlush(display);
    }

    void SetInputRegion(GLFWwindow* window, const std::span<const kor::IVec4> rects)
    {
        if (!Active() || window == nullptr) return;
        Display* display = glfwGetX11Display();
        int event = 0, error = 0;
        if (!XShapeQueryExtension(display, &event, &error)) return;
        std::vector<XRectangle> shape;
        shape.reserve(rects.size());
        for (const auto& r : rects) {
            if (r.z <= 0 || r.w <= 0) continue;
            shape.push_back({ static_cast<short>(r.x), static_cast<short>(r.y),
                              static_cast<unsigned short>(r.z), static_cast<unsigned short>(r.w) });
        }
        XShapeCombineRectangles(display, glfwGetX11Window(window), ShapeInput, 0, 0,
                                shape.data(), static_cast<int>(shape.size()), ShapeSet, Unsorted);
        XFlush(display);
    }

    kor::IVec2 CursorPosition()
    {
        if (!Active()) return {};
        Display* display = glfwGetX11Display();
        ::Window root = 0, child = 0;
        int rootX = 0, rootY = 0, x = 0, y = 0;
        unsigned int mask = 0;
        XQueryPointer(display, DefaultRootWindow(display), &root, &child, &rootX, &rootY, &x, &y, &mask);
        return { rootX, rootY };
    }

    void ShowWithoutFocus(GLFWwindow* window)
    {
        if (!Active() || window == nullptr) return;
        Display* display = glfwGetX11Display();
        // A user time of zero: the window was not asked for by anything the user just did.
        const unsigned long never = 0;
        XChangeProperty(display, glfwGetX11Window(window), XInternAtom(display, "_NET_WM_USER_TIME", False), XA_CARDINAL, 32,
                        PropModeReplace, reinterpret_cast<const unsigned char*>(&never), 1);
        XFlush(display);
    }

    void SkipTaskbar(GLFWwindow* window)
    {
        if (!Active() || window == nullptr) return;
        Display* display = glfwGetX11Display();
        const ::Window handle = glfwGetX11Window(window);
        const Atom state = XInternAtom(display, "_NET_WM_STATE", False);
        const Atom taskbar = XInternAtom(display, "_NET_WM_STATE_SKIP_TASKBAR", False);
        const Atom pager = XInternAtom(display, "_NET_WM_STATE_SKIP_PAGER", False);
        XWindowAttributes attributes {};
        if (XGetWindowAttributes(display, handle, &attributes) && attributes.map_state == IsUnmapped) {
            // Not shown yet: its states are a property the window manager reads when it is.
            const Atom states[3] = { taskbar, pager, XInternAtom(display, "_KDE_NET_WM_STATE_SKIP_SWITCHER", False) };
            XChangeProperty(display, handle, state, XA_ATOM, 32, PropModeAppend, reinterpret_cast<const unsigned char*>(states), 3);
            XFlush(display);
            return;
        }
        // A window already on screen is the window manager's to change: it is asked. (GLFW shows a
        // window as it makes it.) Two states to a message is all one holds.
        XEvent message {};
        message.xclient.type = ClientMessage;
        message.xclient.window = handle;
        message.xclient.message_type = state;
        message.xclient.format = 32;
        message.xclient.data.l[0] = 1;   // _NET_WM_STATE_ADD
        message.xclient.data.l[1] = static_cast<long>(taskbar);
        message.xclient.data.l[2] = static_cast<long>(pager);
        message.xclient.data.l[3] = 1;
        XSendEvent(display, DefaultRootWindow(display), False, SubstructureRedirectMask | SubstructureNotifyMask, &message);
        // KDE's window switcher is a state of its own.
        message.xclient.data.l[1] = static_cast<long>(XInternAtom(display, "_KDE_NET_WM_STATE_SKIP_SWITCHER", False));
        message.xclient.data.l[2] = 0;
        XSendEvent(display, DefaultRootWindow(display), False, SubstructureRedirectMask | SubstructureNotifyMask, &message);
        XFlush(display);
    }
}

#else

namespace kor::x11 {
    bool Active() { return false; }
    void BeginMoveResize(GLFWwindow*, Grip, bool) {}
    void SetInputRegion(GLFWwindow*, std::span<const kor::IVec4>) {}
    void SkipTaskbar(GLFWwindow*) {}
    void ShowWithoutFocus(GLFWwindow*) {}
    kor::IVec2 CursorPosition() { return {}; }
}

#endif
