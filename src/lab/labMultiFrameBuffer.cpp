//
// Created by radue on 2/26/2026.
//

#include "labMultiFrameBuffer.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/type_ptr.inl>

#include "../core/importer.h"
#include "../impl/open_gl/image.h"
#include "io/window.h"

void LabMultiFrameBuffer::Initialize()
{
    // _mesh = gfx::Importer::LoadMesh(gfx::asset("DamagedHelmet/DamagedHelmet.gltf"));
    _albedoImage = gfx::Importer::LoadImage(gfx::asset("DamagedHelmet/Default_albedo.jpg"));

    const auto geometryPassVertexShader = gfx::Shader::Builder()
        .setPath(gfx::shader("geometryPass.vert.glsl"))
        .setStage(gfx::Shader::Stage::eVertex)
        .build();

    const auto geometryPassFragmentShader = gfx::Shader::Builder()
        .setPath(gfx::shader("geometryPass.frag.glsl"))
        .setStage(gfx::Shader::Stage::eFragment)
        .build();

    _pipeline1 = gfx::GraphicsPipeline::Builder()
        .setVertexShader(*geometryPassVertexShader)
        .setFragmentShader(*geometryPassFragmentShader)
        .build();

    const auto vertexShader = gfx::Shader::Builder()
        .setPath(gfx::shader("albedo.vert.glsl"))
        .setStage(gfx::Shader::Stage::eVertex)
        .build();

    const auto fragmentShader = gfx::Shader::Builder()
        .setPath(gfx::shader("albedo.frag.glsl"))
        .setStage(gfx::Shader::Stage::eFragment)
        .build();

    _pipeline2 = gfx::GraphicsPipeline::Builder()
        .setVertexShader(*vertexShader)
        .setFragmentShader(*fragmentShader)
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
    _sampler = gfx::Sampler::Builder().build();

    _descriptorSet = gfx::DescriptorSet::Builder(_pipeline1->getSetLayout(0))
        .write(0, gfx::Descriptor(_uniformBufferCamera.get()))
        .write(1, gfx::Descriptor(_uniformBufferModel.get()))
        .write(2, gfx::Descriptor(_albedoImageView.get(), _sampler.get()))
        .build();

    _positionsImage = gfx::Image::Builder()
        .setType(gfx::Image::Type::e2D)
        .setExtent(glm::uvec2 { gfx::Context::Window().getExtent().x, gfx::Context::Window().getExtent().y })
        .addUsage(gfx::Image::Usage::eColorAttachment)
        .setFormat(gfx::Image::Format::eRGB32_SFLOAT)
        .buildFramebufferImage();


    _colorsImage = gfx::Image::Builder()
        .setType(gfx::Image::Type::e2D)
        .setExtent(glm::uvec2 { gfx::Context::Window().getExtent().x, gfx::Context::Window().getExtent().y })
        .addUsage(gfx::Image::Usage::eColorAttachment)
        .setFormat(gfx::Image::Format::eRGBA8_UNORM)
        .buildFramebufferImage();


    _normalsImage = gfx::Image::Builder()
        .setType(gfx::Image::Type::e2D)
        .setExtent(glm::uvec2 { gfx::Context::Window().getExtent().x, gfx::Context::Window().getExtent().y })
        .addUsage(gfx::Image::Usage::eColorAttachment)
        .setFormat(gfx::Image::Format::eRGB32_SFLOAT)
        .buildFramebufferImage();


    _depthImage = gfx::Image::Builder()
        .setType(gfx::Image::Type::e2D)
        .setExtent(glm::uvec2 { gfx::Context::Window().getExtent().x, gfx::Context::Window().getExtent().y })
        .addUsage(gfx::Image::Usage::eDepthStencilAttachment)
        .setFormat(gfx::Image::Format::eD24_UNORM_S8_UINT)
        .buildFramebufferImage();



    _framebuffer = gfx::Framebuffer::Builder()
        .addColorAttachment(*_positionsImage, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f))
        .addColorAttachment(*_colorsImage, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f))
        .addColorAttachment(*_normalsImage, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f))
        .setDepthStencilAttachment(*_depthImage, 1.f, 0)
        .build();
}

void LabMultiFrameBuffer::Render(gfx::CommandBuffer& commandBuffer)
{
    commandBuffer
        .BeginRendering(_framebuffer.get())
        .SetViewport(0, 0, gfx::Context::Window().getExtent().x, gfx::Context::Window().getExtent().y)
        .BindPipeline(_pipeline1.get())
        .BindDescriptorSet(0, _descriptorSet.get())
        .DrawMesh(_mesh.get(), 1, 0)
        .EndRendering()
        .BeginRendering()
        .BindPipeline(_pipeline2.get())
        .DrawMesh(_mesh.get(), 1, 0)
        .EndRendering();
}
