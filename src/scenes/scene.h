//
// Created by radue on 2/21/2026.
//

#pragma once

namespace gfx
{
    class CommandBuffer;

    class Scene {
    public:
        virtual ~Scene() = default;

        virtual void Initialize() = 0;
        virtual void Update() = 0;
        virtual void FixedUpdate() {};
        virtual void Render(gfx::CommandBuffer& commandBuffer) = 0;
    };
}

