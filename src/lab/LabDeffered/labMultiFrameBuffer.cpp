//
// Created by radue on 2/26/2026.
//

#include "core/framebuffer.h"
#include "labMultiFrameBuffer.h"

#include <array>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/type_ptr.inl>

#include "pbrMesh.h"
#include "core/importer.h"
#include "io/window.h"
#include "../LabMesh/mesh.h"

void LabMultiFrameBuffer::Initialize()
{
    _mesh = gfx::PBRMesh::ImportFromFile(gfx::asset("DamagedHelmet/DamagedHelmet.gltf"));
    _albedoImage = gfx::Importer::LoadImage(gfx::asset("DamagedHelmet/Default_albedo.jpg"));
    _normalImage = gfx::Importer::LoadImage(gfx::asset("DamagedHelmet/Default_normal.jpg"));
    _metalicRoughnessImage = gfx::Importer::LoadImage(gfx::asset("DamagedHelmet/Default_metalRoughness.jpg"));
    _aoImage = gfx::Importer::LoadImage(gfx::asset("DamagedHelmet/Default_AO.jpg"));
    _emissiveImage = gfx::Importer::LoadImage(gfx::asset("DamagedHelmet/Default_emissive.jpg"));

    framebufferImages["position"] = gfx::Image::Builder()
        .setIsPerFrame(true)
        .setType(gfx::Image::Type::e2D)
        .setExtent(glm::uvec2 { gfx::Context::Window().getExtent().x, gfx::Context::Window().getExtent().y })
        .setUsage(gfx::Image::Usage::eColorAttachment)
        .addUsage(gfx::Image::Usage::eSampled)
        .setFormat(gfx::Image::Format::eRGBA32_SFLOAT)
        .build();


    framebufferImages["color"] = gfx::Image::Builder()
        .setIsPerFrame(true)
        .setType(gfx::Image::Type::e2D)
        .setExtent(glm::uvec2 { gfx::Context::Window().getExtent().x, gfx::Context::Window().getExtent().y })
        .setUsage(gfx::Image::Usage::eColorAttachment)
        .addUsage(gfx::Image::Usage::eSampled)
        .setFormat(gfx::Image::Format::eRGBA8_UNORM)
        .build();


    framebufferImages["normal"] = gfx::Image::Builder()
        .setIsPerFrame(true)
        .setType(gfx::Image::Type::e2D)
        .setExtent(glm::uvec2 { gfx::Context::Window().getExtent().x, gfx::Context::Window().getExtent().y })
        .setUsage(gfx::Image::Usage::eColorAttachment)
        .addUsage(gfx::Image::Usage::eSampled)
        .setFormat(gfx::Image::Format::eRGBA8_UNORM)
        .build();

    framebufferImages["metalicRoughnessAo"] = gfx::Image::Builder()
        .setIsPerFrame(true)
        .setType(gfx::Image::Type::e2D)
        .setExtent(glm::uvec2 { gfx::Context::Window().getExtent().x, gfx::Context::Window().getExtent().y })
        .setUsage(gfx::Image::Usage::eColorAttachment)
        .addUsage(gfx::Image::Usage::eSampled)
        .setFormat(gfx::Image::Format::eRGBA8_UNORM)
        .build();

    framebufferImages["emissive"] = gfx::Image::Builder()
        .setIsPerFrame(true)
        .setType(gfx::Image::Type::e2D)
        .setExtent(glm::uvec2 { gfx::Context::Window().getExtent().x, gfx::Context::Window().getExtent().y })
        .setUsage(gfx::Image::Usage::eColorAttachment)
        .addUsage(gfx::Image::Usage::eSampled)
        .setFormat(gfx::Image::Format::eRGBA8_UNORM)
        .build();

    framebufferImages["depth"] = gfx::Image::Builder()
        .setIsPerFrame(true)
        .setType(gfx::Image::Type::e2D)
        .setExtent(glm::uvec2 { gfx::Context::Window().getExtent().x, gfx::Context::Window().getExtent().y })
        .addUsage(gfx::Image::Usage::eDepthStencilAttachment)
        .setFormat(gfx::Image::Format::eD32_SFLOAT_S8_UINT)
        .build();

    for (const auto& [name, image] : framebufferImages)
    {
        framebufferImageViews[name] = gfx::ImageView::Builder(*image).build();
    }

    _framebuffer = gfx::Framebuffer::Builder()
        .addColorAttachment(*framebufferImageViews["position"], glm::vec4(0.0f, 0.0f, 0.0f, 1.0f))
        .addColorAttachment(*framebufferImageViews["color"], glm::vec4(0.0f, 0.0f, 0.0f, 1.0f))
        .addColorAttachment(*framebufferImageViews["normal"], glm::vec4(0.0f, 0.0f, 0.0f, 1.0f))
        .addColorAttachment(*framebufferImageViews["metalicRoughnessAo"], glm::vec4(0.0f, 0.0f, 0.0f, 1.0f))
        .addColorAttachment(*framebufferImageViews["emissive"], glm::vec4(0.0f, 0.0f, 0.0f, 1.0f))
        .setDepthStencilAttachment(*framebufferImageViews["depth"], 1.f, 0)
        .build();

    const auto geometryPassVertexShader = gfx::Shader::Builder()
        .setPath(gfx::shader("geometryPass.vert.glsl"))
        .setStage(gfx::Shader::Stage::eVertex)
        .build();

    const auto geometryPassFragmentShader = gfx::Shader::Builder()
        .setPath(gfx::shader("geometryPass.frag.glsl"))
        .setStage(gfx::Shader::Stage::eFragment)
        .build();

    _graphicsPipeline = gfx::GraphicsPipeline::Builder()
        .setFramebuffer(*_framebuffer)
        .setVertexShader<gfx::PBRMesh>(*geometryPassVertexShader)
        .setFragmentShader(*geometryPassFragmentShader)
        .build();

    _uniformBufferCamera = gfx::Buffer::Builder()
        .setSize(128)
        .setUsage(gfx::Buffer::Usage::eUniform)
        .addMemoryProperty(gfx::Buffer::MemoryProperty::eHostVisible)
        .addMemoryProperty(gfx::Buffer::MemoryProperty::eHostCoherent)
        .build();

    _uniformBufferModel = gfx::Buffer::Builder()
        .setSize(64)
        .setUsage(gfx::Buffer::Usage::eUniform)
        .addMemoryProperty(gfx::Buffer::MemoryProperty::eHostVisible)
        .addMemoryProperty(gfx::Buffer::MemoryProperty::eHostCoherent)
        .build();

    const glm::mat4 viewMatrix = glm::lookAt(
        glm::vec3(0.0f, 0.0f, -5.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
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
    _normalImageView = gfx::ImageView::Builder(*_normalImage).build();
    _metalicRoughnessImageView = gfx::ImageView::Builder(*_metalicRoughnessImage).build();
    _aoImageView = gfx::ImageView::Builder(*_aoImage).build();
    _emissiveImageView = gfx::ImageView::Builder(*_emissiveImage).build();

    for (int i = 0; i < 5; ++i)
        _samplers[i] = gfx::Sampler::Builder().build();

    _descriptorSet0 = gfx::DescriptorSet::Builder(_graphicsPipeline->getSetLayout(0))
        .write(0, gfx::Descriptor(_uniformBufferCamera.get()))
        .build();

    _descriptorSet1 = gfx::DescriptorSet::Builder(_graphicsPipeline->getSetLayout(1))
        .write(0, gfx::Descriptor(_uniformBufferModel.get()))
        .write(1, gfx::Descriptor(_albedoImageView.get(), _samplers[0].get()))
        .write(2, gfx::Descriptor(_normalImageView.get(), _samplers[1].get()))
        .write(3, gfx::Descriptor(_metalicRoughnessImageView.get(), _samplers[2].get()))
        .write(4, gfx::Descriptor(_aoImageView.get(), _samplers[3].get()))
        .write(5, gfx::Descriptor(_emissiveImageView.get(), _samplers[4].get()))
        .build();

    const auto computeShader = gfx::Shader::Builder()
        .setPath(gfx::shader("compositePass.comp.glsl"))
        .setStage(gfx::Shader::Stage::eCompute)
        .build();

    _compositePipeline = gfx::ComputePipeline::Builder()
        .setComputeShader(*computeShader)
        .build();

    _compositeImage = gfx::Image::Builder()
        .setIsPerFrame(true)
        .setType(gfx::Image::Type::e2D)
        .setExtent(glm::uvec2 { gfx::Context::Window().getExtent().x, gfx::Context::Window().getExtent().y })
        .addUsage(gfx::Image::Usage::eStorage)
        .addUsage(gfx::Image::Usage::eTransferSrc)
        .setFormat(gfx::Image::Format::eRGBA8_UNORM)
        .build();
    _compositeImageView = gfx::ImageView::Builder(*_compositeImage).build();

    _lightsBuffer = gfx::Buffer::Builder()
        .setSize(sizeof(Light) * 3)
        .setUsage(gfx::Buffer::Usage::eStorage)
        .addMemoryProperty(gfx::Buffer::MemoryProperty::eHostVisible)
        .addMemoryProperty(gfx::Buffer::MemoryProperty::eHostCoherent)
        .build();

    std::array lights = {
        Light { glm::vec4(0.f, 0.f, -5.f, 1.f), glm::vec4(1.f, .5f, 1.f, 1.f) },
        Light { glm::vec4(5.f, 5.f, -5.f, 1.f), glm::vec4(1.f, 1.f, .5f, 1.f) },
        Light { glm::vec4(-5.f, 5.f, -5.f, 1.f), glm::vec4(.5f, 1.f, 1.f, 1.f) },
    };

    _lightsBuffer->Map();
    _lightsBuffer->Write(std::span<Light> { lights } );
    _lightsBuffer->Unmap();

    _compositeDescriptorSet = gfx::DescriptorSet::Builder(_compositePipeline->getSetLayout(0))
        .write(0, gfx::Descriptor(framebufferImageViews["position"].get(), _samplers[0].get()))
        .write(1, gfx::Descriptor(framebufferImageViews["color"].get(), _samplers[1].get()))
        .write(2, gfx::Descriptor(framebufferImageViews["normal"].get(), _samplers[2].get()))
        .write(3, gfx::Descriptor(framebufferImageViews["metalicRoughnessAo"].get(), _samplers[3].get()))
        .write(4, gfx::Descriptor(framebufferImageViews["emissive"].get(), _samplers[4].get()))
        .write(5, gfx::Descriptor(_compositeImageView.get(), nullptr))
        .write(6, gfx::Descriptor(_uniformBufferCamera.get()))
        .write(7, gfx::Descriptor(_lightsBuffer.get()))
        .build();
}

void LabMultiFrameBuffer::Update()
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

void LabMultiFrameBuffer::Render(gfx::CommandBuffer& commandBuffer)
{
    commandBuffer
        .BeginRendering(_framebuffer.get())
        .SetViewport(0, 0, gfx::Context::Window().getExtent().x, gfx::Context::Window().getExtent().y)
        .SetScissor(0, 0, gfx::Context::Window().getExtent().x, gfx::Context::Window().getExtent().y)
        .BindPipeline(_graphicsPipeline.get())
        .BindDescriptorSet(0, _descriptorSet0.get())
        .BindDescriptorSet(1, _descriptorSet1.get())
        .DrawMesh(_mesh.get(), 1, 0)
        .EndRendering()
        .BindPipeline(_compositePipeline.get())
        .BindDescriptorSet(0, _compositeDescriptorSet.get())
        .Dispatch(gfx::Context::Window().getExtent().x / 16, gfx::Context::Window().getExtent().y / 16, 1)
        .Blit(_compositeImage.get());
}
