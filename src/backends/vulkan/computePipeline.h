//
// Created by radue on 3/6/2026.
//

#pragma once

#include <computePipeline.h>

#include <vulkan/vulkan.hpp>

#include "vk_wrapper.h"


namespace gfx
{
    class CommandBuffer;
}

namespace gfx::vk
{
    class ComputePipeline final : public gfx::ComputePipeline, Wrapper<::vk::Pipeline>
    {
    public:
        explicit ComputePipeline(const Builder& createInfo);

        ~ComputePipeline() override;
        void Bind(const gfx::CommandBuffer& commandBuffer) const override;

        ::vk::PipelineLayout getPipelineLayout() const { return _pipelineLayout; }

    protected:
        void Setup() override;
        void Teardown() override;

    private:
        ::vk::PipelineLayout _pipelineLayout;
    };
}
