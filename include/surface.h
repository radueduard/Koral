//
// Created by Eduard Andrei Radu on 14.03.2026.
//

#pragma once

#include <memory>
#include "api.h"

namespace kor {
    class Window;

    class KORAL_API Surface {
    public:
        explicit Surface(const kor::Window& window) {}
        virtual ~Surface() {};
        static std::unique_ptr<kor::Surface> Create(const kor::Window &window);
    protected:
    };
}