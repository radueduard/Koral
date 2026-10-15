//
// Created by radue on 3/6/2026.
//

#include "computePipeline.h"

#include <ranges>

#include "commandBuffer.h"
#include "descriptorSetLayout.h"
#include "pipelineInterface.h"
#include "device.h"
#include "shader.h"
#include "shaderModules.h"
#include "vulkanContext.h"

namespace kor::vk
{
    ComputePipeline::ComputePipeline(const Builder& createInfo): kor::ComputePipeline(createInfo)
    {
        Setup();
    }

    ComputePipeline::~ComputePipeline()
    {
        Teardown();
    }

    void ComputePipeline::Setup()
    {
        ShaderModules modules(_bindings);

        PipelineInterface descriptors(_setLayouts, _pushConstantRanges, _bindlessBindings);
        _pipelineLayout = descriptors.TakeLayout();

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
                                     .setModule(modules.For(**_shader))
                                     .setPSpecializationInfo(_specConstantsMetadata.empty() ? nullptr : &specializationInfo)
                                     .setPName("main")
                                     .setPNext(descriptors.StageNext());

        const auto pipelineCreateInfo = ::vk::ComputePipelineCreateInfo()
                                        .setPNext(descriptors.CreateNext(nullptr))
                                        .setStage(stageCreateInfo)
                                        .setLayout(_pipelineLayout);

        _handle = Context::Device()->createComputePipeline(nullptr, pipelineCreateInfo).value;
    }

    void ComputePipeline::Teardown()
    {
        // Torn down on destruction and on every hot reload, while frames in flight may still be
        // bound to it — so it goes once the GPU is done, rather than after stalling every queue.
        Context::DestroyWhenUnused([layout = _pipelineLayout, pipeline = _handle] {
            if (layout) Context::Device()->destroyPipelineLayout(layout);
            if (pipeline) Context::Device()->destroyPipeline(pipeline);
        });
        _pipelineLayout = nullptr;
        _handle = nullptr;
    }

    void ComputePipeline::Bind(const kor::CommandBuffer& commandBuffer) const
    {
        kor::ComputePipeline::Bind(commandBuffer);
        const auto& vkCommandBuffer = dynamic_cast<const CommandBuffer&>(commandBuffer);
        vkCommandBuffer->bindPipeline(::vk::PipelineBindPoint::eCompute, _handle);
    }
}
