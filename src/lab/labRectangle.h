//
// Created by radue on 2/27/2026.
//

#pragma once
#include <memory>
#include <scene.h>

#include "image.h"

class LabRectangle : public gfx::Scene
{
public:
    void Initialize() override;
    void Update() override {};
    void Render(gfx::CommandBuffer& commandBuffer) override;
private:
    std::unique_ptr<gfx::Image> _image = nullptr;
};
