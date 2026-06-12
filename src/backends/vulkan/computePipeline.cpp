//
// Created by radue on 3/6/2026.
//

module;

#include <ranges>
#include <stdexcept>
#include <vector>
#include <vulkan/vulkan.hpp>
#include "vulkanContext.h"

module vk.computePipeline;

namespace gfx::vk
{
    ComputePipeline::ComputePipeline(const Builder& createInfo): gfx::ComputePipeline(createInfo)
    {
        if (!createInfo.computeShader.has_value())
            throw std::runtime_error("You can not create a compute pipeline without a compute shader!");

        const auto& shader = dynamic_cast<const gfx::vk::Shader&>(*createInfo.computeShader.value());
        if (shader.getStage() != gfx::Shader::Stage::eCompute) {
            throw std::runtime_error("The shader provided to the compute pipeline must be a compute shader!");
        }

        std::vector<::vk::DescriptorSetLayout> setLayouts = {};
        for (const auto& layout : _setLayouts | std::views::values) {
            setLayouts.push_back(**dynamic_cast<const gfx::vk::DescriptorSetLayout*>(layout.get()));
        }

        std::vector<::vk::PushConstantRange> pushConstantRanges = {};
        for (const auto& [offset, pushConstant] : _pushConstantRanges) {
            pushConstantRanges.push_back(::vk::PushConstantRange()
                 .setStageFlags(::vk::ShaderStageFlagBits::eCompute)
                 .setOffset(offset)
                 .setSize(pushConstant.size));
        }

        const auto pipelineLayoutCreateInfo = ::vk::PipelineLayoutCreateInfo()
            .setSetLayouts(setLayouts)
            .setPushConstantRanges(pushConstantRanges);

        _pipelineLayout = Context::Device()->createPipelineLayout(pipelineLayoutCreateInfo);


        std::vector<::vk::SpecializationMapEntry> specializationMapEntries = {};
        for (const auto& [id, offset, size] : createInfo.specConstantsMetadata) {
            specializationMapEntries.emplace_back(::vk::SpecializationMapEntry()
                .setConstantID(id)
                .setOffset(offset)
                .setSize(size));
        }
        auto specializationInfo = ::vk::SpecializationInfo()
            .setMapEntries(specializationMapEntries)
            .setDataSize(createInfo.specConstantsData.size())
            .setPData(createInfo.specConstantsData.data());

        const auto stageCreateInfo = ::vk::PipelineShaderStageCreateInfo()
            .setStage(::vk::ShaderStageFlagBits::eCompute)
            .setModule(*shader)
            .setPSpecializationInfo(createInfo.specConstantsMetadata.empty() ? nullptr : &specializationInfo)
            .setPName("main");

        const auto pipelineCreateInfo = ::vk::ComputePipelineCreateInfo()
                                        .setStage(stageCreateInfo)
                                        .setLayout(_pipelineLayout);

        _handle = Context::Device()->createComputePipeline(nullptr, pipelineCreateInfo).value;
    }

    ComputePipeline::~ComputePipeline()
    {
        const Device& device = Context::Device();
        device.queuesWaitIdle();
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
