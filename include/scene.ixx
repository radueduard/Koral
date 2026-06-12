//
// Created by radue on 2/21/2026.
//

module;

#include "api.h"

export module gfx.scene;
import gfx.commandBuffer;

struct ImGuiContext;

namespace gfx
{
    export class GFX_API Scene {
    public:
        virtual ~Scene() = default;

        virtual void Initialize() = 0;
        virtual void Update() {}
        virtual void Render(gfx::CommandBuffer& commandBuffer) = 0;
        virtual void RenderUI(ImGuiContext* context) {}
    };
}

