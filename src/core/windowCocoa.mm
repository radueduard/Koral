//
// kor::Window on macOS: what GLFW has no call for. See windowCocoa.h.
//
#include "windowCocoa.h"

#import <AppKit/AppKit.h>

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

namespace kor::cocoa {
    void SetCustomTitleBar(GLFWwindow* window, const bool custom)
    {
        NSWindow* handle = window ? glfwGetCocoaWindow(window) : nil;
        if (handle == nil) return;
        // Not borderless, which is what taking the decoration away makes here: AppKit will not
        // miniaturize a borderless window, nor let it be resized by its edges.
        if (custom) handle.styleMask |= NSWindowStyleMaskFullSizeContentView;
        else handle.styleMask &= ~NSWindowStyleMaskFullSizeContentView;
        handle.titlebarAppearsTransparent = custom;
        handle.titleVisibility = custom ? NSWindowTitleHidden : NSWindowTitleVisible;
    }

    void BeginMove(GLFWwindow* window)
    {
        NSWindow* handle = window ? glfwGetCocoaWindow(window) : nil;
        if (handle == nil) return;
        const NSPoint at = [handle mouseLocationOutsideOfEventStream];
        const auto mouse = [&](const NSEventType type) {
            return [NSEvent mouseEventWithType:type location:at modifierFlags:0
                                     timestamp:NSProcessInfo.processInfo.systemUptime
                                  windowNumber:handle.windowNumber context:nil eventNumber:0 clickCount:1 pressure:1.f];
        };
        // The press was taken from the queue frames ago; the system wants one to begin the drag with.
        [handle performWindowDragWithEvent:mouse(NSEventTypeLeftMouseDown)];
        // The drag takes the real release, which the window would otherwise never hear of: GLFW, and all
        // that reads it, would hold the button down for ever.
        [NSApp postEvent:mouse(NSEventTypeLeftMouseUp) atStart:NO];
    }

    float WindowButtonsWidth(GLFWwindow* window)
    {
        NSWindow* handle = window ? glfwGetCocoaWindow(window) : nil;
        if (handle == nil) return 0.f;
        NSButton* first = [handle standardWindowButton:NSWindowCloseButton];
        NSButton* last = [handle standardWindowButton:NSWindowZoomButton];
        if (first == nil || last == nil || last.hidden) return 0.f;
        const NSRect from = [first convertRect:first.bounds toView:nil];
        const NSRect to = [last convertRect:last.bounds toView:nil];
        // As much room after the last as before the first.
        return static_cast<float>(NSMaxX(to) + NSMinX(from));
    }
}
