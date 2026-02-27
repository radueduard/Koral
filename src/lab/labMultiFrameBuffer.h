//
// Created by radue on 2/26/2026.
//

#pragma once
#include <memory>

#include "core/buffer.h"
#include "core/mesh.h"
#include "core/descriptorBinding.h"
#include "core/comandBuffer.h"

#include "scenes/scene.h"

class LabMultiFrameBuffer : public gfx::Scene
{
public:
    void Initialize() override;
    void Update() override;
    void Render() override;

private:
    std::unique_ptr<gfx::Mesh> _mesh = nullptr;
    std::unique_ptr<gfx::GraphicsPipeline> _pipeline1 = nullptr;
    std::unique_ptr<gfx::GraphicsPipeline> _pipeline2 = nullptr;

    std::unique_ptr<gfx::Buffer> _uniformBufferCamera = nullptr;
    std::unique_ptr<gfx::Buffer> _uniformBufferModel = nullptr;

    std::unique_ptr<gfx::Image> _albedoImage = nullptr;
    std::unique_ptr<gfx::ImageView> _albedoImageView = nullptr;
    std::unique_ptr<gfx::Sampler> _sampler = nullptr;

    std::unique_ptr<gfx::Descriptor> _descriptorBinding0 = nullptr;
    std::unique_ptr<gfx::Descriptor> _descriptorBinding1 = nullptr;
    std::unique_ptr<gfx::Descriptor> _descriptorBinding2 = nullptr;

    std::unique_ptr<gfx::CommandBuffer> _commandBuffer = nullptr;

    std::unique_ptr<gfx::Image> _positionsImage = nullptr;
    std::unique_ptr<gfx::ImageView> _positionsImageView = nullptr;

    std::unique_ptr<gfx::Image> _colorsImage = nullptr;
    std::unique_ptr<gfx::ImageView> _colorsImageView = nullptr;

    std::unique_ptr<gfx::Image> _normalsImage = nullptr;
    std::unique_ptr<gfx::ImageView> _normalsImageView = nullptr;

    std::unique_ptr<gfx::Image> _depthImage = nullptr;
    std::unique_ptr<gfx::ImageView> _depthImageView = nullptr;

    std::unique_ptr<gfx::Framebuffer> _framebuffer = nullptr;
};
