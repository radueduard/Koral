//
// Created by radue on 2/21/2026.
//

#pragma once

#include "api.h"

struct ImGuiContext;

namespace gfx
{
    class CommandBuffer;

    class GFX_API Scene {
    public:
        virtual ~Scene() = default;

        virtual void Initialize() = 0;
        virtual void Update() {}
        virtual void Render(gfx::CommandBuffer& commandBuffer) = 0;
        virtual void RenderUI(ImGuiContext* context) {}
    };
}

