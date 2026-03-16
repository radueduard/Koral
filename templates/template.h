#pragma once

#include <scene.h>

class @NAME@ final : public gfx::Scene
{
public:
    void Initialize() override;
    void Update() override;
    void Render(gfx::CommandBuffer& commandBuffer) override;
};
