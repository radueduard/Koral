//
// Created by radue on 2/26/2026.
//

#pragma once
#include <array>
#include <memory>

#include "core/buffer.h"
#include "core/mesh.h"
#include "core/descriptorBinding.h"
#include "core/comandBuffer.h"

#include "scenes/scene.h"

struct Light
{
    glm::vec4 position;
    glm::vec4 color;
    float intensity;
    float radius;
    float padding[2];
};

class LabMultiFrameBuffer : public gfx::Scene
{
public:
    void Initialize() override;
    void Update() override {};
    void Render(gfx::CommandBuffer& commandBuffer) override;

private:
    std::unique_ptr<gfx::Mesh> _mesh = nullptr;
    std::unique_ptr<gfx::GraphicsPipeline> _graphicsPipeline = nullptr;
    std::unique_ptr<gfx::ComputePipeline> _compositePipeline = nullptr;

    std::unique_ptr<gfx::Buffer> _uniformBufferCamera = nullptr;
    std::unique_ptr<gfx::Buffer> _uniformBufferModel = nullptr;

    std::unique_ptr<gfx::Image> _albedoImage = nullptr;
    std::unique_ptr<gfx::Image> _normalImage = nullptr;
    std::unique_ptr<gfx::Image> _metalicRoughnessImage = nullptr;
    std::unique_ptr<gfx::Image> _aoImage = nullptr;
    std::unique_ptr<gfx::Image> _emissiveImage = nullptr;

    std::unique_ptr<gfx::ImageView> _albedoImageView = nullptr;
    std::unique_ptr<gfx::ImageView> _normalImageView = nullptr;
    std::unique_ptr<gfx::ImageView> _metalicRoughnessImageView = nullptr;
    std::unique_ptr<gfx::ImageView> _aoImageView = nullptr;
    std::unique_ptr<gfx::ImageView> _emissiveImageView = nullptr;

    std::unique_ptr<gfx::DescriptorSet> _descriptorSet0 = nullptr;
    std::unique_ptr<gfx::DescriptorSet> _descriptorSet1 = nullptr;

    std::unique_ptr<gfx::Image> _positionsImage = nullptr;
    std::unique_ptr<gfx::Image> _colorsImage = nullptr;
    std::unique_ptr<gfx::Image> _normalsImage = nullptr;
    std::unique_ptr<gfx::Image> _metallicRoughnessAoImage = nullptr;
    std::unique_ptr<gfx::Image> _emissivesImage = nullptr;
    std::unique_ptr<gfx::Image> _depthImage = nullptr;

    std::map<std::string, std::unique_ptr<gfx::Image>> framebufferImages;
    std::map<std::string, std::unique_ptr<gfx::ImageView>> framebufferImageViews;

    std::unique_ptr<gfx::Image> _compositeImage = nullptr;
    std::unique_ptr<gfx::ImageView> _compositeImageView = nullptr;

    std::array<std::unique_ptr<gfx::Sampler>, 5> _samplers;

    std::unique_ptr<gfx::Framebuffer> _framebuffer = nullptr;

    std::unique_ptr<gfx::Buffer> _lightsBuffer = nullptr;
    std::unique_ptr<gfx::DescriptorSet> _compositeDescriptorSet = nullptr;
};
