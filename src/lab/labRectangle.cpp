//
// Created by radue on 2/27/2026.
//

#include "labRectangle.h"

#include <stb_image_write.h>

#include "core/comandBuffer.h"
#include "core/computePipeline.h"
#include "core/descriptorBinding.h"
#include "core/image.h"
#include "core/shader.h"
#include "io/window.h"

void LabRectangle::Initialize()
{
    const auto buffer = gfx::Buffer::Builder()
        .setSize(64)
        .setUsage(gfx::Buffer::Usage::eUniform)
        .setLayout(gfx::Buffer::Layout::eStd430)
        .addMemoryProperty(gfx::Buffer::MemoryProperty::eHostVisible)
        .addMemoryProperty(gfx::Buffer::MemoryProperty::eHostCoherent)
        .build();

    constexpr struct {
        glm::vec3 position = glm::vec3(.5f, .5f, 0.0f);
        float size = 0.75f;
        glm::vec4 color = glm::vec4(1.0f, .5f, 1.0f, 1.0f);
    } rectangleData {};

    buffer->Map();
    buffer->Write(rectangleData);
    buffer->Unmap();

    const auto image = gfx::Image::Builder()
        .setType(gfx::Image::Type::e2D)
        .setFormat(gfx::Image::Format::eRGBA8_UNORM)
        .setExtent({ 512, 512 })
        .setMipLevels(1)
        .setArrayLayers(1)
        .setMSAA(gfx::MSAA::eNone)
        .setUsage(gfx::Image::Usage::eStorage)
        .addUsage(gfx::Image::Usage::eTransferSrc)
        .build();

    const auto imageView = gfx::ImageView::Builder(*image).build();
    const auto sampler = gfx::Sampler::Builder().build();

    const auto computeShader = gfx::Shader::Builder()
        .setStage(gfx::Shader::Stage::eCompute)
        .setPath(gfx::shader("test.comp.glsl"))
        .build();

    const auto computePipeline = gfx::ComputePipeline::Builder()
        .setComputeShader(*computeShader)
        .build();

    const auto descriptorSet = gfx::DescriptorSet::Builder(computePipeline->getSetLayout(0))
        .write(0, gfx::Descriptor(buffer.get()))
        .write(1, gfx::Descriptor(imageView.get(), sampler.get()))
        .build();

    const auto commandBuffer = gfx::CommandBuffer::Create(gfx::CommandBuffer::Usage::eCompute);

    commandBuffer->Begin()
        .BindPipeline(computePipeline.get())
        .BindDescriptorSet(0, descriptorSet.get())
        .Dispatch(512 / 16, 512 / 16, 1)
        .End();
    commandBuffer->Submit();

    const auto data = image->ReadData(0, 0);
    stbi_write_png("output.png", static_cast<int>(image->getExtent().x), static_cast<int>(image->getExtent().y), 4, data.data(), static_cast<int>(image->getExtent().x) * 4);

    gfx::Context::Window().close();
}