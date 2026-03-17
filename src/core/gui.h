//
// Created by radue on 3/17/2026.
//

#pragma once
#include "commandBuffer.h"
#include "scene.h"

namespace gfx
{
    class GUI
    {
    public:
        static void Init();
        static void Render(CommandBuffer& commandBuffer, const Scene& scene);
        static void Shutdown();
    };
}
