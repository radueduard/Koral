//
// Created by radue on 2/23/2026.
//

#pragma once
#include <memory>

#include "core/descriptorBinding.h"
#include "core/graphicsPipeline.h"
#include "core/mesh.h"
#include "scenes/scene.h"

class Lab01 : public gfx::Scene
{
public:
    void Initialize() override;
    void Update() override;
    void Render(gfx::CommandBuffer& commandBuffer) override;
private:
    std::unique_ptr<gfx::Mesh> _mesh = nullptr;
    std::unique_ptr<gfx::GraphicsPipeline> _pipeline = nullptr;

    std::unique_ptr<gfx::Buffer> _uniformBufferCamera = nullptr;
    std::unique_ptr<gfx::Buffer> _uniformBufferModel = nullptr;

    std::unique_ptr<gfx::Image> _albedoImage = nullptr;
    std::unique_ptr<gfx::Image> _normalImage = nullptr;

    std::unique_ptr<gfx::ImageView> _albedoImageView = nullptr;
    std::unique_ptr<gfx::ImageView> _normalImageView = nullptr;

    std::unique_ptr<gfx::Sampler> _albedoSampler = nullptr;
    std::unique_ptr<gfx::Sampler> _normalSampler = nullptr;

    std::unique_ptr<gfx::DescriptorSet> _cameraDescriptorSet = nullptr;
    std::unique_ptr<gfx::DescriptorSet> _meshDescriptorSet = nullptr;


    glm::vec3 _cameraPosition = glm::vec3(0.f, 0.f, -5.f);
    glm::vec3 _cameraForward = glm::vec3(0.f, 0.f, 1.f);
    glm::vec3 _cameraUp = glm::vec3(0.f, 1.f, 0.f);
    glm::vec3 _cameraRight = glm::vec3(1.f, 0.f, 0.f);
};
