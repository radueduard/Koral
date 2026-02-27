//
// Created by radue on 2/27/2026.
//

#include "labRectangle.h"

#include <stb_image_write.h>

#include "core/resources/descriptorBinding.h"
#include "core/resources/image.h"
#include "core/resources/shader.h"
#include "impl/open_gl/resources/buffer.h"
#include "impl/open_gl/resources/commandBuffer.h"
#include "impl/open_gl/resources/computePipeline.h"
#include "impl/open_gl/resources/imageView.h"
#include "impl/open_gl/resources/sampler.h"
#include "io/window.h"

void LabRectangle::Initialize()
{
    const auto buffer = gfx::ogl::Buffer::Builder()
        .setSize(64)
        .setUsage(gfx::ogl::Buffer::Usage::eUniform)
        .setLayout(gfx::ogl::Buffer::Layout::eStd430)
        .addMemoryProperty(gfx::ogl::Buffer::MemoryProperty::eHostVisible)
        .addMemoryProperty(gfx::ogl::Buffer::MemoryProperty::eHostCoherent)
        .build();

    struct {
        glm::vec3 position = glm::vec3(.5f, .5f, 0.0f);
        float size = 0.75f;
        glm::vec4 color = glm::vec4(1.0f, .5f, 1.0f, 1.0f);
    } rectangleData {};

    buffer->Map();
    buffer->Write(0, sizeof(rectangleData), reinterpret_cast<std::byte*>(&rectangleData));
    buffer->Unmap();

    const auto image = gfx::Image::Builder()
        .setType(gfx::Image::Type::e2D)
        .setFormat(gfx::Image::Format::eRGBA8_UNORM)
        .setExtent({ 512, 512 })
        .setMipLevels(1)
        .setArrayLayers(1)
        .setMSAA(gfx::Image::MSAA::eNone)
        .setUsage(gfx::Image::Usage::eStorage)
        .addUsage(gfx::Image::Usage::eTransferSrc)
        .build();

    const auto imageView = gfx::ogl::ImageView::Builder(*image).build();
    const auto sampler = gfx::ogl::Sampler::CreateInfo().build();

    const auto binding0 = gfx::Descriptor::Builder()
        .setType(gfx::Descriptor::Type::eUniformBuffer)
        .setBuffer(buffer.get())
        .build();

    const auto binding1 = gfx::Descriptor::Builder()
        .setType(gfx::Descriptor::Type::eStorageImage)
        .setImage(imageView.get())
        .build();

    const auto computeShader = gfx::Shader::Builder()
        .setStage(gfx::Shader::Stage::eCompute)
        .setPath(gfx::shader("test.comp.glsl"))
        .build();

    const auto computePipeline = gfx::ogl::ComputePipeline::Builder()
        .setComputeShader(*computeShader)
        .build();

    const auto commandBuffer = gfx::ogl::CommandBuffer::Create(gfx::ogl::CommandBuffer::Usage::eCompute);

    commandBuffer->Begin()
        .BindPipeline(computePipeline.get())
        .BindDescriptor(0, binding0.get())
        .BindDescriptor(1, binding1.get())
        .Dispatch(512 / 16, 512 / 16, 1)
        .End();
    commandBuffer->Submit();

    const auto data = image->ReadData();
    stbi_write_png("output.png", static_cast<int>(image->getExtent().x), static_cast<int>(image->getExtent().y), 4, data.data(), static_cast<int>(image->getExtent().x) * 4);

    gfx::Context::Window().close();
}