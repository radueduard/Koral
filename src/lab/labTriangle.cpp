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
    const auto buffer = Buffer::Builder(Buffer::CreateInfo()
        .setSize(64)
        .setUsage(Buffer::Usage::eUniform)
        .setLayout(Buffer::Layout::eStd430)
        .addMemoryProperty(Buffer::MemoryProperty::eDeviceLocal)
    )
        .addUniform("position", glm::vec3(.5f, .5f, 0.f))
        .addUniform("color", glm::vec4(1.f, .5f, 0.f, 1.f))
        .addUniform("size", .75f)
        .build();

    const auto image = Image::Create(Image::CreateInfo()
        .setType(Image::Type::e2D)
        .setFormat(Image::Format::eRGBA8_UNORM)
        .setExtent({ 512, 512 })
        .setMipLevels(1)
        .setArrayLayers(1)
        .setMSAA(Image::MSAA::eNone)
        .setUsage(Image::Usage::eStorage)
        .addUsage(Image::Usage::eTransferSrc));

    const auto imageView = ImageView::Create(*image, ImageView::CreateInfo());
    const auto sampler = Sampler::Create(Sampler::CreateInfo());

    const auto binding0 = Descriptor::Create(Descriptor::CreateInfo()
        .setType(Descriptor::Type::eUniformBuffer)
        .setBuffer(buffer.get()));

    const auto binding1 = Descriptor::Create(Descriptor::CreateInfo()
        .setType(Descriptor::Type::eStorageImage)
        .setImage(imageView.get()));

    const auto computeShader = Shader::Create(Shader::CreateInfo()
        .setStage(Shader::Stage::eCompute)
        .setLang(Shader::Lang::eGLSL)
        .setPath(shader("test.comp.glsl")));

    const auto computePipeline = ComputePipeline::Create(ComputePipeline::CreateInfo(*computeShader));
    const auto commandBuffer = CommandBuffer::Create(CommandBuffer::Usage::eCompute);

    commandBuffer->Begin()
        .BindPipeline(computePipeline.get())
        .BindDescriptor(0, binding0.get())
        .BindDescriptor(1, binding1.get())
        .Dispatch(512 / 16, 512 / 16, 1)
        .End();
    commandBuffer->Submit();

    const auto data = image->ReadData();
    stbi_write_png("output.png", static_cast<int>(image->getExtent().x), static_cast<int>(image->getExtent().y), 4, data.data(), static_cast<int>(image->getExtent().x) * 4);

    const auto vertexShader = Shader::Create(Shader::CreateInfo()
        .setStage(Shader::Stage::eVertex)
        .setPath(shader("triangle.vert.glsl")));

    const auto fragmentShader = Shader::Create(Shader::CreateInfo()
        .setStage(Shader::Stage::eFragment)
        .setPath(shader("triangle.frag.glsl")));

    _graphicsPipeline = GraphicsPipeline::Create(GraphicsPipeline::Builder()
        .setVertexStage(VertexState { .shader = *vertexShader } )
        .setFragmentShader(*fragmentShader));

    _timeBuffer = Buffer::Builder(Buffer::CreateInfo()
        .setSize(sizeof(float))
        .setUsage(Buffer::Usage::eUniform)
        .setLayout(Buffer::Layout::eStd430)
        .addMemoryProperty(Buffer::MemoryProperty::eHostCoherent)
        .addMemoryProperty(Buffer::MemoryProperty::eHostVisible))

        .addUniform("time", 0.f)
        .build();

    _timeDescriptor = Descriptor::Create(Descriptor::CreateInfo()
        .setType(Descriptor::Type::eUniformBuffer)
        .setBuffer(_timeBuffer.get()));

    _commandBuffer = CommandBuffer::Create(CommandBuffer::Usage::eGraphics);
}

void LabTriangle::Update() {
    const float time = io::Time::WindowTime();
    _timeBuffer->Map();
    _timeBuffer->Write(0, sizeof(float), reinterpret_cast<const std::byte*>(&time));
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
