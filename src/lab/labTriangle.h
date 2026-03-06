//
// Created by radue on 2/21/2026.
//
#pragma once

#include "../core/buffer.h"
#include "../core/descriptorBinding.h"
#include "../core/graphicsPipeline.h"
#include "scenes/scene.h"

namespace gfx
{
    class CommandBuffer;
}

struct LabTriangle : gfx::Scene
{
    void Initialize() override;
    void Update() override;
    void Render() override;

private:
    std::unique_ptr<gfx::Buffer> _timeBuffer = nullptr;
    std::unique_ptr<gfx::DescriptorSet> _timeDescriptorSet = nullptr;
    std::unique_ptr<gfx::GraphicsPipeline> _graphicsPipeline = nullptr;

    std::unique_ptr<gfx::CommandBuffer> _commandBuffer = nullptr;
};
