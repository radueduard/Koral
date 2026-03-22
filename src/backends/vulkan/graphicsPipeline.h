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
    private:
        ::vk::PipelineLayout _pipelineLayout;
        inline static std::array<::vk::DynamicState, 3> _dynamicStates = {
            ::vk::DynamicState::eViewport,
            ::vk::DynamicState::eScissor,
            ::vk::DynamicState::eFrontFace
        };
    };
}
