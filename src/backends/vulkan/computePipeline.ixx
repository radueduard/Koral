//
// Created by radue on 3/6/2026.
//

module;

#include <vulkan/vulkan.hpp>
#include "vk_wrapper.h"

export module gfx:vk_computePipeline;
import :vk_types;

import :computePipeline;

namespace gfx::vk
{
    export class ComputePipeline final : public gfx::ComputePipeline, Wrapper<::vk::Pipeline>
    {
    public:
        explicit ComputePipeline(const Builder& createInfo);

        ~ComputePipeline() override;
        void Bind(const gfx::CommandBuffer& commandBuffer) const override;

        ::vk::PipelineLayout getPipelineLayout() const { return _pipelineLayout; }

    private:
        ::vk::PipelineLayout _pipelineLayout;
    };
}
