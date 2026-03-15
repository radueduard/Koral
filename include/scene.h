//
// Created by radue on 2/21/2026.
//

#pragma once

#include "api.h"

namespace gfx
{
    class CommandBuffer;

    class GFX_API Scene {
    public:
        virtual ~Scene() = default;

        virtual void Initialize() = 0;
        virtual void Update() = 0;
        virtual void FixedUpdate() {};
        virtual void Render(gfx::CommandBuffer& commandBuffer) = 0;
    };
}

