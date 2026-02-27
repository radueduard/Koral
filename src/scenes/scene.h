//
// Created by radue on 2/21/2026.
//

#pragma once

namespace gfx
{
    class Scene {
    public:
        virtual ~Scene() = default;

        virtual void Initialize() = 0;
        virtual void Update() = 0;
        virtual void FixedUpdate() {};
        virtual void Render() = 0;
    };
}

