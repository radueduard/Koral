//
// Created by radue on 2/23/2026.
//

#include "Lab01.h"

#define GLM_ENABLE_EXPERIMENTAL
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_RADIANS
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/type_ptr.inl>

#include <comandBuffer.h>
#include <importer.h>
#include <window.h>

#include "buffer.h"
#include "descriptorBinding.h"
#include "descriptorSet.h"
#include "imageView.h"
#include "mesh.h"
#include "sampler.h"
#include "shader.h"

void Lab01::Initialize()
{
    _mesh = gfx::LabMesh::ImportFromFile(gfx::asset("DamagedHelmet/DamagedHelmet.gltf"));
    _albedoImage = gfx::Importer::LoadImage(gfx::asset("DamagedHelmet/Default_albedo.jpg"));
    _normalImage = gfx::Importer::LoadImage(gfx::asset("DamagedHelmet/Default_normal.jpg"));

    const auto vertexShader = gfx::Shader::Builder()
        .setPath(gfx::shader("albedo.vert.glsl"))
        .setStage(gfx::Shader::Stage::eVertex)
        .build();

    const auto fragmentShader = gfx::Shader::Builder()
        .setPath(gfx::shader("albedo.frag.glsl"))
        .setStage(gfx::Shader::Stage::eFragment)
        .build();

    _pipeline = gfx::GraphicsPipeline::Builder()
        .setVertexShader<gfx::LabMesh>(*vertexShader)
        .setFragmentShader(*fragmentShader)
        .build();

    _uniformBufferCamera = gfx::Buffer::Builder()
        .setSize(128)
        .setUsage(gfx::Buffer::Usage::eUniform)
        .build();

    _uniformBufferModel = gfx::Buffer::Builder()
        .setSize(64)
        .setUsage(gfx::Buffer::Usage::eUniform)
        .build();

    const glm::mat4 viewMatrix = glm::lookAt(
        _cameraPosition,
        _cameraPosition + _cameraForward,
        glm::vec3(0.f, 1.f, 0.f)
    );

    const glm::mat4 projectionMatrix = glm::perspective(
        glm::radians(45.f),
        static_cast<float>(gfx::Context::Window().getExtent().x) / static_cast<float>(gfx::Context::Window().getExtent().y),
        0.1f,
        100.f
    );

    const glm::mat4 modelMatrix = glm::rotate(glm::mat4(1.f), glm::radians(-90.f), glm::vec3(1.f, 0.f, 0.f)) *
                            glm::rotate(glm::mat4(1.f), glm::radians(180.f), glm::vec3(0.f, 1.f, 0.f));

    _uniformBufferCamera->Map();
    _uniformBufferCamera->Write(viewMatrix);
    _uniformBufferCamera->Write(projectionMatrix, 64);
    _uniformBufferCamera->Unmap();

    _uniformBufferModel->Map();
    _uniformBufferModel->Write(modelMatrix);
    _uniformBufferModel->Unmap();

    _albedoImageView = gfx::ImageView::Builder(*_albedoImage).build();
    _albedoSampler = gfx::Sampler::Builder().build();

    _normalImageView = gfx::ImageView::Builder(*_normalImage).build();
    _normalSampler = gfx::Sampler::Builder().build();

    _cameraDescriptorSet = gfx::DescriptorSet::Builder(_pipeline->getSetLayout(0))
        .write(0, gfx::Descriptor(_uniformBufferCamera.get()))
        .build();

    _meshDescriptorSet = gfx::DescriptorSet::Builder(_pipeline->getSetLayout(1))
        .write(0, gfx::Descriptor(_uniformBufferModel.get()))
        .write(1, gfx::Descriptor(_albedoImageView.get(), _albedoSampler.get()))
        .write(2, gfx::Descriptor(_normalImageView.get(), _normalSampler.get()))
        .build();
}

void Lab01::Update()
{
    if (!gfx::io::Input::isMouseButtonHeld(gfx::io::MouseButton::eRight)) {
        return;
    }

    bool changed = false;
    // move
    {
        glm::vec3 delta = { 0.0f, 0.0f, 0.0f };
        if (gfx::io::Input::isKeyHeld(gfx::io::Key::eW)) {
            delta.z += 1.f;
        }
        if (gfx::io::Input::isKeyHeld(gfx::io::Key::eS)) {
            delta.z -= 1.f;
        }
        if (gfx::io::Input::isKeyHeld(gfx::io::Key::eA)) {
            delta.x -= 1.f;
        }
        if (gfx::io::Input::isKeyHeld(gfx::io::Key::eD)) {
            delta.x += 1.f;
        }
        if (gfx::io::Input::isKeyHeld(gfx::io::Key::eQ)) {
            delta.y -= 1.f;
        }
        if (gfx::io::Input::isKeyHeld(gfx::io::Key::eE)) {
            delta.y += 1.f;
        }

        if (delta != glm::vec3(0.f)) {
            delta = glm::normalize(delta) * 5.f * gfx::io::Time::FrameTime();
            _cameraPosition += delta.x * _cameraRight + delta.y * glm::vec3(0.f, 1.f, 0.f) + delta.z * _cameraForward;
            changed = true;
        }
    }

    // rotate
    {
        if (const glm::vec2 mouseDelta = gfx::io::Input::getMousePositionDelta(); mouseDelta != glm::vec2(0.f))
        {
            constexpr float sensitivity = 0.2f;
            _cameraForward = glm::rotate(glm::mat4(1.f), glm::radians(-mouseDelta.x * sensitivity), glm::vec3(0.f, 1.f, 0.f)) *
                             glm::rotate(glm::mat4(1.f), glm::radians(-mouseDelta.y * sensitivity), _cameraRight) *
                             glm::vec4(_cameraForward, 0.f);

            _cameraRight = glm::normalize(glm::cross(_cameraForward, glm::vec3(0.f, 1.f, 0.f)));
            changed = true;
        }
    }

    if (!changed) {
        return;
    }

    const glm::mat4 viewMatrix = glm::lookAt(
        _cameraPosition,
        _cameraPosition + _cameraForward,
        glm::vec3(0.f, 1.f, 0.f)
    );

    _uniformBufferCamera->Map();
    _uniformBufferCamera->Write(viewMatrix);
    _uniformBufferCamera->Unmap();
}

void Lab01::Render(gfx::CommandBuffer& commandBuffer)
{
    commandBuffer
        .BeginRendering()
        .SetViewport(0, 0, gfx::Context::Window().getExtent().x, gfx::Context::Window().getExtent().y)
        .SetScissor(0, 0, gfx::Context::Window().getExtent().x, gfx::Context::Window().getExtent().y)
        .BindPipeline(_pipeline.get())
        .BindDescriptorSet(0, _cameraDescriptorSet.get())
        .BindDescriptorSet(1, _meshDescriptorSet.get())
        .DrawMesh(_mesh.get(), 1, 0)
        .EndRendering();
}
