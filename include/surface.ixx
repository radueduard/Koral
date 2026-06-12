//
// Created by Eduard Andrei Radu on 14.03.2026.
//

module;

#include "api.h"

export module gfx.surface;

import std;
import gfx.window;

namespace gfx {
    export class GFX_API Surface {
    public:
        explicit Surface(const gfx::io::Window& window) {}
        virtual ~Surface() = default;
        static std::unique_ptr<gfx::Surface> Create(const gfx::io::Window &window);
    protected:
    };
}
