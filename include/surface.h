//
// Created by Eduard Andrei Radu on 14.03.2026.
//

#pragma once

#include <memory>
#include "api.h"

namespace kor {
    class Window;

    /**
     * @brief The presentable surface of a window: what the swap chain draws into.
     *
     * Created by the window as it is built and owned by it — reach the live one through
     * Window::surface(). What it wraps depends on the backend and the platform, and none of that
     * is exposed here: a scene never has to name a surface to render to the screen.
     */
    class KORAL_API Surface {
    public:
        /**
         * @brief Constructs the surface for a window. Prefer Create().
         *
         * The parameter is unnamed because the base keeps nothing: what a surface *is* differs
         * entirely between APIs — a VkSurfaceKHR on Vulkan, nothing at all on OpenGL, where the
         * context is the window — so the backend subclass takes what it needs and this holds no
         * state to share.
         */
        explicit Surface(const kor::Window&) {}
        virtual ~Surface() = default;

        /**
         * @brief Creates the surface the active graphics API needs for @p window.
         * @param window The window to present to.
         * @return The surface, owned by the caller.
         */
        static std::unique_ptr<kor::Surface> Create(const kor::Window &window);
    protected:
    };
}
