//
// Created by radue on 3/6/2026.
//

#pragma once

#include <computePipeline.h>

#include <vulkan/vulkan.hpp>

#include "vk_wrapper.h"


namespace kor
{
    class CommandBuffer;
}

namespace kor::vk
{
    class ComputePipeline final : public kor::ComputePipeline, Wrapper<::vk::Pipeline>
    {
    public:
        explicit ComputePipeline(const Builder& createInfo);

        ~ComputePipeline() override;
        void Bind(const kor::CommandBuffer& commandBuffer) const override;

        ::vk::PipelineLayout getPipelineLayout() const { return _pipelineLayout; }

    protected:
        void Setup() override;
        void Teardown() override;

    private:
        ::vk::PipelineLayout _pipelineLayout;
    };
}
