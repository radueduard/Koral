//
// Created by eduard on 10.03.2026.
//

#pragma once
#include <graphicsPipeline.h>
#include "vk_wrapper.h"
#include <vulkan/vulkan.hpp>

namespace gfx
{
    class CommandBuffer;
}

namespace gfx::vk
{
    class GraphicsPipeline final : public gfx::GraphicsPipeline, public Wrapper<::vk::Pipeline>
    {
    public:
        explicit GraphicsPipeline(const Builder& createInfo);

        ~GraphicsPipeline() override;

        void Bind(const gfx::CommandBuffer& commandBuffer) const override;
        void Unbind() const override {}

        [[nodiscard]] const ::vk::PipelineLayout& getPipelineLayout() const { return _pipelineLayout; }
        [[nodiscard]] ::vk::ShaderStageFlags getPipelineStageFlags() const { return _pipelineStageFlags; }

    protected:
        void Setup() override;
        void Teardown() override;

    private:
        ::vk::PipelineLayout _pipelineLayout;
        ::vk::ShaderStageFlags _pipelineStageFlags = ::vk::ShaderStageFlags();

        static constexpr std::array _dynamicStates = std::to_array<::vk::DynamicState>({
            // Vulkan 1.0 core
            ::vk::DynamicState::eViewport,
            ::vk::DynamicState::eScissor,
            ::vk::DynamicState::eLineWidth,
            ::vk::DynamicState::eDepthBias,
            ::vk::DynamicState::eBlendConstants,
            ::vk::DynamicState::eStencilCompareMask,
            ::vk::DynamicState::eStencilWriteMask,
            ::vk::DynamicState::eStencilReference,
            // Extended dynamic state 1
            ::vk::DynamicState::eCullMode,
            ::vk::DynamicState::eFrontFace,
            ::vk::DynamicState::eDepthTestEnable,
            ::vk::DynamicState::eDepthWriteEnable,
            ::vk::DynamicState::eDepthCompareOp,
            ::vk::DynamicState::eStencilTestEnable,
            ::vk::DynamicState::eStencilOp,
            // Extended dynamic state 2
            ::vk::DynamicState::eDepthBiasEnable,
            ::vk::DynamicState::eRasterizerDiscardEnable,
            ::vk::DynamicState::ePrimitiveRestartEnable,
        });
    };
}
