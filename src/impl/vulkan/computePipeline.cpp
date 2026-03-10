//
// Created by radue on 3/6/2026.
//

#include "computePipeline.h"

#include <ranges>

#include "commandBuffer.h"
#include "descriptorSetLayout.h"
#include "device.h"
#include "shader.h"
#include "vulkanContext.h"

namespace gfx::vk
{
    ComputePipeline::ComputePipeline(const Builder& createInfo): gfx::ComputePipeline(createInfo)
    {
        if (!createInfo.computeShader.has_value())
            throw std::runtime_error("You can not create a compute pipeline without a compute shader!");

        const auto& shader = dynamic_cast<const Shader&>(createInfo.computeShader.value().get());
        if (shader.getStage() != Shader::Stage::eCompute) {
            throw std::runtime_error("The shader provided to the compute pipeline must be a compute shader!");
        }

        std::vector<::vk::DescriptorSetLayout> setLayouts = {};
        for (const auto& layout : _setLayouts | std::views::values) {
            setLayouts.push_back(**dynamic_cast<const DescriptorSetLayout*>(layout.get()));
        }

        const auto pipelineLayoutCreateInfo = ::vk::PipelineLayoutCreateInfo()
            .setSetLayouts(setLayouts);

        _pipelineLayout = Context::Device()->createPipelineLayout(pipelineLayoutCreateInfo);

        const auto stageCreateInfo = ::vk::PipelineShaderStageCreateInfo()
                                     .setStage(::vk::ShaderStageFlagBits::eCompute)
                                     .setModule(*shader)
                                     .setPName("main");

        const auto pipelineCreateInfo = ::vk::ComputePipelineCreateInfo()
                                        .setStage(stageCreateInfo)
                                        .setLayout(_pipelineLayout);

        _handle = Context::Device()->createComputePipeline(nullptr, pipelineCreateInfo).value;
    }

    ComputePipeline::~ComputePipeline()
    {
        const Device& device = Context::Device();
        device->waitIdle();
        if (_pipelineLayout)
            device->destroyPipelineLayout(_pipelineLayout);
        if (_handle)
            device->destroyPipeline(_handle);
    }

    void ComputePipeline::Bind(const gfx::CommandBuffer& commandBuffer) const
    {
        gfx::ComputePipeline::Bind(commandBuffer);
        const auto& vkCommandBuffer = dynamic_cast<const CommandBuffer&>(commandBuffer);
        vkCommandBuffer->bindPipeline(::vk::PipelineBindPoint::eCompute, _handle);
    }
}
