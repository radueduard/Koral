//
// Created by eduard on 10.03.2026.
//

module;

#include "vk_wrapper.h"
#include <vulkan/vulkan.hpp>

export module gfx:vk_graphicsPipeline;
import :vk_types;

import :graphicsPipeline;

namespace gfx::vk
{
    export class GraphicsPipeline final : public gfx::GraphicsPipeline, public Wrapper<::vk::Pipeline>
    {
    public:
        explicit GraphicsPipeline(const Builder& createInfo);

        ~GraphicsPipeline() override;

        void Bind(const gfx::CommandBuffer& commandBuffer) const override;
        void Unbind() const override {}

        [[nodiscard]] const ::vk::PipelineLayout& getPipelineLayout() const { return _pipelineLayout; }
        [[nodiscard]] ::vk::ShaderStageFlags getPipelineStageFlags() const { return _pipelineStageFlags; }
    private:
        ::vk::PipelineLayout _pipelineLayout;
        ::vk::ShaderStageFlags _pipelineStageFlags = ::vk::ShaderStageFlags();
        inline static std::array<::vk::DynamicState, 3> _dynamicStates = {
            ::vk::DynamicState::eViewport,
            ::vk::DynamicState::eScissor,
            ::vk::DynamicState::eFrontFace
        };
    };
}
