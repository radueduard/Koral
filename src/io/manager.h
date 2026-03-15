//
// Created by radue on 2/17/2026.
//

#pragma once
#include <vector>
#include <memory>
#include <thread>

#include "window.h"

int main();

namespace gfx
{
    class Engine;
}

namespace gfx::io
{
    class GFX_API Manager
    {
        friend int ::main();
        friend class gfx::Engine;
    public:
        static Window& createWindow(Window::Builder& createInfo);
        static void removeWindow(Window* window);

        ~Manager();

    private:
        Manager();
        static void update();

        inline static std::vector<std::pair<std::jthread, std::unique_ptr<Window>>> _windows;
    };
}
