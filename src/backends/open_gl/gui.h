//
// Created by radue on 3/17/2026.
//

#pragma once
#include <commandBuffer.h>

namespace gfx::ogl
{
    class GUI
    {
    public:
        static void Init();
        static void NewFrame();
        static void Render(CommandBuffer& commandBuffer);
        static void Shutdown();
    };
}
