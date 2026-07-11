//
// Created by eduard on 10.03.2026.
//

#include "graphicsPipeline.h"

#include <ranges>

#include "commandBuffer.h"
#include "descriptorSetLayout.h"
#include "device.h"
#include "shader.h"
#include "vulkanContext.h"
#include "commandBuffer.h"
#include "framebuffer.h"
#include "vk_enum_conversions.h"

namespace gfx::vk
{
    GraphicsPipeline::GraphicsPipeline(const Builder& createInfo): gfx::GraphicsPipeline(createInfo)
    {
        Setup();
    }

    GraphicsPipeline::~GraphicsPipeline()
    {
        Teardown();
    }

    void GraphicsPipeline::Bind(const gfx::CommandBuffer& commandBuffer) const
    {
        const auto& vkCommandBuffer = dynamic_cast<const vk::CommandBuffer&>(commandBuffer);
        vkCommandBuffer->bindPipeline(::vk::PipelineBindPoint::eGraphics, **this);
    }

    void GraphicsPipeline::Setup()
    {
        std::vector<::vk::Format> colorAttachmentFormats;
        for (const auto& format : _framebuffer->getColorAttachments()) {
            colorAttachmentFormats.push_back(getVkFormat(format.get().getImage()->getFormat()));
        }
        auto pipelineRenderingCreateInfo = ::vk::PipelineRenderingCreateInfo()
            .setColorAttachmentFormats(colorAttachmentFormats);
        if (_framebuffer->hasDepthAttachment()) {
            const auto depthFormat = getVkFormat(_framebuffer->getDepthAttachment().getImage()->getFormat());
            pipelineRenderingCreateInfo.setDepthAttachmentFormat(depthFormat);
        }
        if (_framebuffer->hasStencilAttachment()) {
            const auto stencilFormat = getVkFormat(_framebuffer->getStencilAttachment().getImage()->getFormat());
            // Only set stencil format if the image actually has a stencil aspect
            const auto aspectFlags = getVkImageAspectFlags(_framebuffer->getStencilAttachment().getImage()->getFormat());
            if (aspectFlags & ::vk::ImageAspectFlagBits::eStencil) {
                pipelineRenderingCreateInfo.setStencilAttachmentFormat(stencilFormat);
            }
        }

        std::vector<::vk::DescriptorSetLayout> setLayouts;
        if (!_setLayouts.empty()) {
            setLayouts = { std::ranges::max(_setLayouts | std::views::keys) + 1, nullptr };
            for (const auto& [set, layout] : _setLayouts) {
                setLayouts[set] = **dynamic_cast<const DescriptorSetLayout*>(layout.get());
            }
        }

        std::vector<::vk::PushConstantRange> pushConstantRanges = {};
        for (const auto& [offset, pushConstant] : _pushConstantRanges) {
            pushConstantRanges.push_back(::vk::PushConstantRange()
                .setStageFlags(getVkShaderStageFlags(pushConstant.stages))
                .setOffset(offset)
                .setSize(pushConstant.size));
        }

        const auto pipelineLayoutCreateInfo = ::vk::PipelineLayoutCreateInfo()
            .setSetLayouts(setLayouts)
            .setPushConstantRanges(pushConstantRanges);

        _pipelineLayout = Context::Device()->createPipelineLayout(pipelineLayoutCreateInfo);

        std::vector<::vk::PipelineShaderStageCreateInfo> shaderStages = {};
        if (_vertexShader.has_value()) {
            const auto& shader = dynamic_cast<const vk::Shader&>(**_vertexShader);
            shaderStages.push_back(::vk::PipelineShaderStageCreateInfo()
                                      .setStage(::vk::ShaderStageFlagBits::eVertex)
                                      .setModule(*shader)
                                      .setPName("main"));
            _pipelineStageFlags |= ::vk::ShaderStageFlagBits::eVertex;
        }
        if (_tessellationState.has_value())
        {
            const auto& controlShader = dynamic_cast<const vk::Shader&>(*_tessellationState->controlShader);
            shaderStages.push_back(::vk::PipelineShaderStageCreateInfo()
                                      .setStage(::vk::ShaderStageFlagBits::eTessellationControl)
                                      .setModule(*controlShader)
                                      .setPName("main"));
            const auto& evalShader = dynamic_cast<const vk::Shader&>(*_tessellationState->evalShader);
            shaderStages.push_back(::vk::PipelineShaderStageCreateInfo()
                                      .setStage(::vk::ShaderStageFlagBits::eTessellationEvaluation)
                                      .setModule(*evalShader)
                                      .setPName("main"));
            _pipelineStageFlags |= ::vk::ShaderStageFlagBits::eTessellationControl | ::vk::ShaderStageFlagBits::eTessellationEvaluation;
        }
        if (_geometryShader.has_value()) {
            const auto& shader = dynamic_cast<const vk::Shader&>(**_geometryShader);
            shaderStages.push_back(::vk::PipelineShaderStageCreateInfo()
                                      .setStage(::vk::ShaderStageFlagBits::eGeometry)
                                      .setModule(*shader)
                                      .setPName("main"));
            _pipelineStageFlags |= ::vk::ShaderStageFlagBits::eGeometry;
        }
        if (_fragmentShader.has_value())
        {
            const auto& shader = dynamic_cast<const vk::Shader&>(**_fragmentShader);
            shaderStages.push_back(::vk::PipelineShaderStageCreateInfo()
                                      .setStage(::vk::ShaderStageFlagBits::eFragment)
                                      .setModule(*shader)
                                      .setPName("main"));
            _pipelineStageFlags |= ::vk::ShaderStageFlagBits::eFragment;
        }
        if (_taskShader.has_value())
        {
            const auto& shader = dynamic_cast<const vk::Shader&>(**_taskShader);
            shaderStages.push_back(::vk::PipelineShaderStageCreateInfo()
                                      .setStage(::vk::ShaderStageFlagBits::eTaskEXT)
                                      .setModule(*shader)
                                      .setPName("main"));
            _pipelineStageFlags |= ::vk::ShaderStageFlagBits::eTaskEXT;
        }
        if (_meshShader.has_value())
        {
            const auto& shader = dynamic_cast<const vk::Shader&>(**_meshShader);
            shaderStages.push_back(::vk::PipelineShaderStageCreateInfo()
                                      .setStage(::vk::ShaderStageFlagBits::eMeshEXT)
                                      .setModule(*shader)
                                      .setPName("main"));
            _pipelineStageFlags |= ::vk::ShaderStageFlagBits::eMeshEXT;
        }

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
        // Share one specialization block across every stage; stages that don't declare a
        // given constant id ignore it, so this matches the beginner-facing Builder API.
        if (!_specConstantsMetadata.empty()) {
            for (auto& stage : shaderStages) {
                stage.setPSpecializationInfo(&specializationInfo);
            }
        }

        auto dynamicStateCreateInfo = ::vk::PipelineDynamicStateCreateInfo()
            .setDynamicStates(_dynamicStates);

        auto pipelineCreateInfo = ::vk::GraphicsPipelineCreateInfo()
            .setPNext(&pipelineRenderingCreateInfo)
            .setPDynamicState(&dynamicStateCreateInfo)
            .setStages(shaderStages)
            .setLayout(_pipelineLayout);

        auto rasterizationStateCreateInfo = ::vk::PipelineRasterizationStateCreateInfo()
            .setPolygonMode(getVkPolygonMode(_rasterizationState.polygonMode))
            .setCullMode(getVkCullMode(_rasterizationState.cullMode))
            .setFrontFace(getVkFrontFace(_rasterizationState.frontFace))
            .setDepthClampEnable(_rasterizationState.depthClampEnable)
            .setLineWidth(_rasterizationState.lineWidth)
            .setRasterizerDiscardEnable(_rasterizationState.rasterizerDiscardEnable);
        pipelineCreateInfo.setPRasterizationState(&rasterizationStateCreateInfo);

        const auto multisampleStateCreateInfo = ::vk::PipelineMultisampleStateCreateInfo()
            .setRasterizationSamples(getVkSampleCount(_multisampleState.sampleCount))
            .setSampleShadingEnable(_multisampleState.sampleShadingEnable)
            .setMinSampleShading(_multisampleState.minSampleShading);
        pipelineCreateInfo.setPMultisampleState(&multisampleStateCreateInfo);

        auto vertexInputStateCreateInfo = ::vk::PipelineVertexInputStateCreateInfo();
        std::vector<::vk::VertexInputBindingDescription> vkVertexInputBindingDescriptions = {};
        std::vector<::vk::VertexInputAttributeDescription> vkVertexInputAttributeDescriptions = {};

        if (_vertexBindingDescriptions && _vertexAttributeDescriptions) {
            for (const auto& [binding, stride] : *_vertexBindingDescriptions) {
                vkVertexInputBindingDescriptions.push_back(::vk::VertexInputBindingDescription()
                                                              .setBinding(binding)
                                                              .setStride(stride)
                                                              .setInputRate(::vk::VertexInputRate::eVertex));
            }
            vertexInputStateCreateInfo.setVertexBindingDescriptions(vkVertexInputBindingDescriptions);

            for (const auto& [location, binding, channelCount, channelType, offset] : *_vertexAttributeDescriptions) {
                vkVertexInputAttributeDescriptions.push_back(::vk::VertexInputAttributeDescription()
                                                              .setLocation(location)
                                                              .setBinding(binding)
                                                              .setFormat(getVkFormat(channelType, channelCount))
                                                              .setOffset(offset));
            }
            vertexInputStateCreateInfo.setVertexAttributeDescriptions(vkVertexInputAttributeDescriptions);
        }
        pipelineCreateInfo.setPVertexInputState(&vertexInputStateCreateInfo);

        auto depthStencilStateCreateInfo = ::vk::PipelineDepthStencilStateCreateInfo()
            .setDepthTestEnable(_depthStencilState.depthTestEnable)
            .setDepthWriteEnable(_depthStencilState.depthWriteEnable)
            .setDepthCompareOp(getVkCompareOp(_depthStencilState.depthCompareOp))
            .setStencilTestEnable(_depthStencilState.stencilEnable);
        pipelineCreateInfo.setPDepthStencilState(&depthStencilStateCreateInfo);

        std::vector<::vk::PipelineColorBlendAttachmentState> vkColorBlendAttachmentStates;
        int index = 0;
        for (const auto& _ : _framebuffer->getColorAttachments()) {
            if (_colorBlendState.attachments.empty()) {
                vkColorBlendAttachmentStates.push_back(::vk::PipelineColorBlendAttachmentState()
                    .setBlendEnable(false)
                    .setColorWriteMask(::vk::ColorComponentFlagBits::eR | ::vk::ColorComponentFlagBits::eG | ::vk::ColorComponentFlagBits::eB | ::vk::ColorComponentFlagBits::eA));
            } else {
                const auto& attachmentBlendState = _colorBlendState.attachments[index];
                vkColorBlendAttachmentStates.push_back(::vk::PipelineColorBlendAttachmentState()
                    .setBlendEnable(attachmentBlendState.blendEnable)
                    .setSrcColorBlendFactor(getVkBlendFactor(attachmentBlendState.srcColorBlendFactor))
                    .setDstColorBlendFactor(getVkBlendFactor(attachmentBlendState.dstColorBlendFactor))
                    .setColorBlendOp(getVkBlendOp(attachmentBlendState.colorBlendOp))
                    .setSrcAlphaBlendFactor(getVkBlendFactor(attachmentBlendState.srcAlphaBlendFactor))
                    .setDstAlphaBlendFactor(getVkBlendFactor(attachmentBlendState.dstAlphaBlendFactor))
                    .setAlphaBlendOp(getVkBlendOp(attachmentBlendState.alphaBlendOp))
                    .setColorWriteMask(getVkColorComponentFlags(attachmentBlendState.colorWriteMask)));
            }
            index++;
        }
        auto colorBlendStateCreateInfo = ::vk::PipelineColorBlendStateCreateInfo()
            .setLogicOpEnable(_colorBlendState.enableLogicOp)
            .setLogicOp(getVkLogicOp(_colorBlendState.logicOp))
            .setAttachments(vkColorBlendAttachmentStates);
        pipelineCreateInfo.setPColorBlendState(&colorBlendStateCreateInfo);

        auto inputAssemblyStateCreateInfo = ::vk::PipelineInputAssemblyStateCreateInfo()
            .setTopology(getVkPrimitiveTopology(_inputAssemblyState.topology))
            .setPrimitiveRestartEnable(_inputAssemblyState.primitiveRestartEnable);
        pipelineCreateInfo.setPInputAssemblyState(&inputAssemblyStateCreateInfo);

        auto viewportStateCreateInfo = ::vk::PipelineViewportStateCreateInfo()
            .setViewportCount(1)
            .setScissorCount(1);
        pipelineCreateInfo.setPViewportState(&viewportStateCreateInfo);

        const auto result = Context::Device()->createGraphicsPipeline(nullptr, pipelineCreateInfo);
        if (result.result != ::vk::Result::eSuccess) {
            throw std::runtime_error("Failed to create graphics pipeline: " + ::vk::to_string(result.result));
        }
        _handle = result.value;
    }

    void GraphicsPipeline::Teardown()
    {
        const Device& device = Context::Device();
        device.queuesWaitIdle();
        if (_pipelineLayout)
            device->destroyPipelineLayout(_pipelineLayout);
        if (_handle)
            device->destroyPipeline(_handle);
    }
}
