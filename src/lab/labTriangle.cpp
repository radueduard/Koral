//
// Created by radue on 2/21/2026.
//

#include "labTriangle.h"

#include <stb_image_write.h>

#include "core/resources/buffer.h"
#include "core/resources/comandBuffer.h"
#include "core/resources/image.h"
#include "core/resources/sampler.h"
#include "core/resources/shader.h"
#include "core/resources/descriptorBinding.h"
#include "core/resources/computePipeline.h"
#include "io/window.h"

using namespace gfx;

void LabTriangle::Initialize()
{
    const auto vertexShader = Shader::Builder()
        .setStage(Shader::Stage::eVertex)
        .setPath(shader("triangle.vert.glsl"))
        .build();

    const auto fragmentShader = Shader::Builder()
        .setStage(Shader::Stage::eFragment)
        .setPath(shader("triangle.frag.glsl"))
        .build();

    _graphicsPipeline = GraphicsPipeline::Builder()
        .setVertexShader(*vertexShader)
        .setFragmentShader(*fragmentShader)
        .build();

    _timeBuffer = Buffer::Builder()
        .setSize(sizeof(float))
        .setUsage(Buffer::Usage::eUniform)
        .setLayout(Buffer::Layout::eStd430)
        .addMemoryProperty(Buffer::MemoryProperty::eHostCoherent)
        .addMemoryProperty(Buffer::MemoryProperty::eHostVisible)
        .build();

    _timeDescriptor = Descriptor::Builder()
        .setType(Descriptor::Type::eUniformBuffer)
        .setBuffer(_timeBuffer.get())
        .build();

    _commandBuffer = CommandBuffer::Create(CommandBuffer::Usage::eGraphics);
}

void LabTriangle::Update() {
    const float time = io::Time::WindowTime();
    _timeBuffer->Map();
    _timeBuffer->Write(time);
    _timeBuffer->Unmap();

    _commandBuffer->Reset();
}

void LabTriangle::Render()
{
    _commandBuffer->Begin()
        .BeginRendering(Context::DefaultFramebuffer())
        .SetViewport(0.f, 0.f, Context::Window().getExtent().x, Context::Window().getExtent().y)
        .BindPipeline(_graphicsPipeline.get())
        .BindDescriptor(0, _timeDescriptor.get())
        .Draw(3, 1, 0, 0)
        .EndRendering()
        .End();
    _commandBuffer->Submit();
}
