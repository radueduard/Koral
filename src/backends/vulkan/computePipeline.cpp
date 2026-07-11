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
        Setup();
    }

    ComputePipeline::~ComputePipeline()
    {
        Teardown();
    }

    void ComputePipeline::Setup()
    {
        const auto& shader = dynamic_cast<const Shader&>(**_shader);

        std::vector<::vk::DescriptorSetLayout> setLayouts = {};
        for (const auto& layout : _setLayouts | std::views::values) {
            setLayouts.push_back(**dynamic_cast<const DescriptorSetLayout*>(layout.get()));
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

        std::vector<::vk::SpecializationMapEntry> specializationMapEntries;
        for (const auto& [id, offset, size] : _specConstantsMetadata) {
            specializationMapEntries.emplace_back(::vk::SpecializationMapEntry()
                .setConstantID(id)
                .setOffset(offset)
                .setSize(size));
        }
        auto specializationInfo = ::vk::SpecializationInfo()
            .setMapEntries(specializationMapEntries)
            .setDataSize(_specConstantsData.size())
            .setPData(_specConstantsData.data());

        const auto stageCreateInfo = ::vk::PipelineShaderStageCreateInfo()
                                     .setStage(::vk::ShaderStageFlagBits::eCompute)
                                     .setModule(*shader)
                                     .setPSpecializationInfo(_specConstantsMetadata.empty() ? nullptr : &specializationInfo)
                                     .setPName("main");

        const auto pipelineCreateInfo = ::vk::ComputePipelineCreateInfo()
                                        .setStage(stageCreateInfo)
                                        .setLayout(_pipelineLayout);

        _handle = Context::Device()->createComputePipeline(nullptr, pipelineCreateInfo).value;
    }

    void ComputePipeline::Teardown()
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
