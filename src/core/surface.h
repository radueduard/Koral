//
// Created by Eduard Andrei Radu on 14.03.2026.
//

#pragma once

#include <memory>

namespace gfx {
    namespace io {
        class Window;
    }

    class Surface {
    public:
        explicit Surface(const gfx::io::Window& window) {}
        virtual ~Surface() {};
        static std::unique_ptr<gfx::Surface> Create(const gfx::io::Window &window);
    protected:
    };
}