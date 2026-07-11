//
// Created by radue on 2/21/2026.
//

#pragma once

#include <commandBuffer.h>
#include <glm/glm.hpp>

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

        // Window framebuffer was resized. Dispatched once per frame after a change.
        // Other input (keys, mouse, scroll) is available through gfx::Input.
        virtual void OnResize(glm::uvec2 extent) {}
    };
}

