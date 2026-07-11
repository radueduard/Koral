//
// Created by Eduard Andrei Radu on 14.03.2026.
//

#pragma once

#include <memory>
#include "api.h"

namespace gfx {
    class Window;

    class GFX_API Surface {
    public:
        explicit Surface(const gfx::Window& window) {}
        virtual ~Surface() {};
        static std::unique_ptr<gfx::Surface> Create(const gfx::Window &window);
    protected:
    };
}