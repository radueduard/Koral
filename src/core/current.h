//
// The window a command means when it names none: the current scene's.
//

#pragma once

namespace kor
{
    class Window;
}

namespace kor::detail
{
    /**
     * @brief The current scene's window; with no scene current, the application's first. Null when
     *        there is no window at all.
     */
    Window* CurrentWindowOrNull();

    /** @brief As CurrentWindowOrNull, throwing a message naming @p what when there is none. */
    Window& CurrentWindow(const char* what);
}
