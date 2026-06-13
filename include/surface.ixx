//
// Created by Eduard Andrei Radu on 14.03.2026.
//

module;

#include "api.h"

export module gfx:surface;
import :types;

import std;

namespace gfx {
    class GFX_API Surface {
    public:
        explicit Surface(const Window& window) {}
        virtual ~Surface() = default;
        static std::unique_ptr<Surface> Create(const Window &window);
    };
}
