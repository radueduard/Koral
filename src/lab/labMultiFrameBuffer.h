//
// Created by radue on 2/26/2026.
//

#pragma once
#include <memory>

#include "core/buffer.h"
#include "core/mesh.h"
#include "core/descriptorBinding.h"
#include "core/comandBuffer.h"
#include "core/framebufferImage.h"

#include "scenes/scene.h"

class LabMultiFrameBuffer : public gfx::Scene
{
public:
    void Initialize() override;
    void Update() override {};
    void Render(gfx::CommandBuffer& commandBuffer) override;

private:
    std::unique_ptr<gfx::Mesh> _mesh = nullptr;
    std::unique_ptr<gfx::GraphicsPipeline> _pipeline1 = nullptr;
    std::unique_ptr<gfx::GraphicsPipeline> _pipeline2 = nullptr;

    std::unique_ptr<gfx::Buffer> _uniformBufferCamera = nullptr;
    std::unique_ptr<gfx::Buffer> _uniformBufferModel = nullptr;

    std::unique_ptr<gfx::Image> _albedoImage = nullptr;
    std::unique_ptr<gfx::ImageView> _albedoImageView = nullptr;
    std::unique_ptr<gfx::Sampler> _sampler = nullptr;

    std::unique_ptr<gfx::DescriptorSet> _descriptorSet = nullptr;

    std::unique_ptr<gfx::FramebufferImage> _positionsImage = nullptr;
    std::unique_ptr<gfx::FramebufferImage> _colorsImage = nullptr;
    std::unique_ptr<gfx::FramebufferImage> _normalsImage = nullptr;
    std::unique_ptr<gfx::FramebufferImage> _depthImage = nullptr;

    std::unique_ptr<gfx::Framebuffer> _framebuffer = nullptr;
};
