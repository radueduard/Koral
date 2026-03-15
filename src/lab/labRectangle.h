//
// Created by radue on 2/27/2026.
//

#pragma once
#include <scene.h>

class LabRectangle : public gfx::Scene
{
public:
    void Initialize() override;
    void Update() override {};
    void Render(gfx::CommandBuffer& commandBuffer) override {};
};
