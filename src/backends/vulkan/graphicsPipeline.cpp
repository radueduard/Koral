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
        // dynamic rendering structs

        std::vector<::vk::Format> colorAttachmentFormats;
        for (const auto& format : _framebuffer.get().getColorAttachments()) {
            colorAttachmentFormats.push_back(getVkFormat(format.get().getImage().getFormat()));
        }
        auto pipelineRenderingCreateInfo = ::vk::PipelineRenderingCreateInfo()
            .setColorAttachmentFormats(colorAttachmentFormats);
        if (_framebuffer.get().hasDepthStencilAttachment()) {
            pipelineRenderingCreateInfo.setDepthAttachmentFormat(getVkFormat(_framebuffer.get().getDepthStencilAttachment().getImage().getFormat()));
            pipelineRenderingCreateInfo.setStencilAttachmentFormat(getVkFormat(_framebuffer.get().getDepthStencilAttachment().getImage().getFormat()));
        }

        std::vector<::vk::DescriptorSetLayout> setLayouts(std::ranges::max(_setLayouts | std::views::keys) + 1, nullptr);
        for (const auto& [set, layout] : _setLayouts) {
            setLayouts[set] = **dynamic_cast<const DescriptorSetLayout*>(layout.get());
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
            const auto& shader = dynamic_cast<const vk::Shader&>(_vertexShader.value().get());
            shaderStages.push_back(::vk::PipelineShaderStageCreateInfo()
                                      .setStage(::vk::ShaderStageFlagBits::eVertex)
                                      .setModule(*shader)
                                      .setPName("main"));
            _pipelineStageFlags |= ::vk::ShaderStageFlagBits::eVertex;
        }
        if (_tessellationState.has_value())
        {
            const auto& controlShader = dynamic_cast<const vk::Shader&>(_tessellationState->controlShader.get());
            shaderStages.push_back(::vk::PipelineShaderStageCreateInfo()
                                      .setStage(::vk::ShaderStageFlagBits::eTessellationControl)
                                      .setModule(*controlShader)
                                      .setPName("main"));
            const auto& evalShader = dynamic_cast<const vk::Shader&>(_tessellationState->evalShader.get());
            shaderStages.push_back(::vk::PipelineShaderStageCreateInfo()
                                      .setStage(::vk::ShaderStageFlagBits::eTessellationEvaluation)
                                      .setModule(*evalShader)
                                      .setPName("main"));
            _pipelineStageFlags |= ::vk::ShaderStageFlagBits::eTessellationControl | ::vk::ShaderStageFlagBits::eTessellationEvaluation;
        }
        if (_geometryShader.has_value()) {
            const auto& shader = dynamic_cast<const vk::Shader&>(_geometryShader.value().get());
            shaderStages.push_back(::vk::PipelineShaderStageCreateInfo()
                                      .setStage(::vk::ShaderStageFlagBits::eGeometry)
                                      .setModule(*shader)
                                      .setPName("main"));
            _pipelineStageFlags |= ::vk::ShaderStageFlagBits::eGeometry;
        }
        if (_fragmentShader.has_value())
        {
            const auto& shader = dynamic_cast<const vk::Shader&>(_fragmentShader.value().get());
            shaderStages.push_back(::vk::PipelineShaderStageCreateInfo()
                                      .setStage(::vk::ShaderStageFlagBits::eFragment)
                                      .setModule(*shader)
                                      .setPName("main"));
            _pipelineStageFlags |= ::vk::ShaderStageFlagBits::eFragment;
        }
        if (_taskShader.has_value())
        {
            const auto& shader = dynamic_cast<const vk::Shader&>(_taskShader.value().get());
            shaderStages.push_back(::vk::PipelineShaderStageCreateInfo()
                                      .setStage(::vk::ShaderStageFlagBits::eTaskEXT)
                                      .setModule(*shader)
                                      .setPName("main"));
            _pipelineStageFlags |= ::vk::ShaderStageFlagBits::eTaskEXT;
        }
        if (_meshShader.has_value())
        {
            const auto& shader = dynamic_cast<const vk::Shader&>(_meshShader.value().get());
            shaderStages.push_back(::vk::PipelineShaderStageCreateInfo()
                                      .setStage(::vk::ShaderStageFlagBits::eMeshEXT)
                                      .setModule(*shader)
                                      .setPName("main"));
            _pipelineStageFlags |= ::vk::ShaderStageFlagBits::eMeshEXT;
        }

        auto dynamicStateCreateInfo = ::vk::PipelineDynamicStateCreateInfo()
            .setDynamicStates(_dynamicStates);

        auto pipelineCreateInfo = ::vk::GraphicsPipelineCreateInfo()
            .setPNext(&pipelineRenderingCreateInfo)
            .setPDynamicState(&dynamicStateCreateInfo)
            .setStages(shaderStages)
            .setLayout(_pipelineLayout);

        auto rasterizationStateCreateInfo = ::vk::PipelineRasterizationStateCreateInfo()
            .setPolygonMode(getVkPolygonMode(createInfo.rasterizationState.polygonMode))
            .setCullMode(getVkCullMode(createInfo.rasterizationState.cullMode))
            .setFrontFace(getVkFrontFace(createInfo.rasterizationState.frontFace))
            .setDepthClampEnable(createInfo.rasterizationState.depthClampEnable)
            .setLineWidth(createInfo.rasterizationState.lineWidth)
            .setRasterizerDiscardEnable(createInfo.rasterizationState.rasterizerDiscardEnable);
        pipelineCreateInfo.setPRasterizationState(&rasterizationStateCreateInfo);

        const auto multisampleStateCreateInfo = ::vk::PipelineMultisampleStateCreateInfo()
            .setRasterizationSamples(getVkSampleCount(createInfo.multisampleState.sampleCount))
            .setSampleShadingEnable(createInfo.multisampleState.sampleShadingEnable)
            .setMinSampleShading(createInfo.multisampleState.minSampleShading);
        pipelineCreateInfo.setPMultisampleState(&multisampleStateCreateInfo);

        auto vertexInputStateCreateInfo = ::vk::PipelineVertexInputStateCreateInfo();
        std::vector<::vk::VertexInputBindingDescription> vkVertexInputBindingDescriptions = {};
        std::vector<::vk::VertexInputAttributeDescription> vkVertexInputAttributeDescriptions = {};

        if (!createInfo.vertexBindingDescriptions.empty() && !createInfo.vertexAttributeDescriptions.empty()) {
            for (const auto& [binding, stride] : createInfo.vertexBindingDescriptions) {
                vkVertexInputBindingDescriptions.push_back(::vk::VertexInputBindingDescription()
                                                              .setBinding(binding)
                                                              .setStride(stride)
                                                              .setInputRate(::vk::VertexInputRate::eVertex));
            }
            vertexInputStateCreateInfo.setVertexBindingDescriptions(vkVertexInputBindingDescriptions);

            for (const auto& [location, binding, channelCount, channelType, offset] : createInfo.vertexAttributeDescriptions) {
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
            .setDepthTestEnable(createInfo.depthStencilState.depthTestEnable)
            .setDepthWriteEnable(createInfo.depthStencilState.depthWriteEnable)
            .setDepthCompareOp(getVkCompareOp(createInfo.depthStencilState.depthCompareOp))
            .setStencilTestEnable(createInfo.depthStencilState.stencilEnable);
        pipelineCreateInfo.setPDepthStencilState(&depthStencilStateCreateInfo);

        std::vector<::vk::PipelineColorBlendAttachmentState> vkColorBlendAttachmentStates;
        int index = 0;
        for (const auto& _ : _framebuffer.get().getColorAttachments()) {
            if (createInfo.colorBlendState.attachments.empty()) {
                vkColorBlendAttachmentStates.push_back(::vk::PipelineColorBlendAttachmentState()
                    .setBlendEnable(false)
                    .setColorWriteMask(::vk::ColorComponentFlagBits::eR | ::vk::ColorComponentFlagBits::eG | ::vk::ColorComponentFlagBits::eB | ::vk::ColorComponentFlagBits::eA));
            } else {
                const auto& attachmentBlendState = createInfo.colorBlendState.attachments[index];
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
            .setLogicOpEnable(createInfo.colorBlendState.enableLogicOp)
            .setLogicOp(getVkLogicOp(createInfo.colorBlendState.logicOp))
            .setAttachments(vkColorBlendAttachmentStates);
        pipelineCreateInfo.setPColorBlendState(&colorBlendStateCreateInfo);

        auto inputAssemblyStateCreateInfo = ::vk::PipelineInputAssemblyStateCreateInfo()
            .setTopology(getVkPrimitiveTopology(createInfo.inputAssemblyState.topology))
            .setPrimitiveRestartEnable(createInfo.inputAssemblyState.primitiveRestartEnable);
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

    GraphicsPipeline::~GraphicsPipeline()
    {
        const Device& device = Context::Device();
        device.queuesWaitIdle();
        if (_pipelineLayout)
            device->destroyPipelineLayout(_pipelineLayout);
        if (_handle)
            device->destroyPipeline(_handle);
    }

    void GraphicsPipeline::Bind(const gfx::CommandBuffer& commandBuffer) const
    {
        const auto& vkCommandBuffer = dynamic_cast<const vk::CommandBuffer&>(commandBuffer);
        vkCommandBuffer->bindPipeline(::vk::PipelineBindPoint::eGraphics, **this);
    }
}
