//
// Created by radue on 6/23/2026.
//

#pragma once

#include <rayTracingPipeline.h>

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>

#include "vk_wrapper.h"

namespace gfx
{
    class CommandBuffer;
}

namespace gfx::vk
{
    class RayTracingPipeline final : public gfx::RayTracingPipeline, public Wrapper<::vk::Pipeline>
    {
    public:
        explicit RayTracingPipeline(const Builder& createInfo);

        ~RayTracingPipeline() override;

        void Bind(const gfx::CommandBuffer& commandBuffer) const override;
        void Unbind() const override {}

        [[nodiscard]] const ::vk::PipelineLayout& getPipelineLayout() const { return _pipelineLayout; }

        /** @brief Shader binding table regions, indexed for vkCmdTraceRaysKHR. */
        [[nodiscard]] const ::vk::StridedDeviceAddressRegionKHR& getRaygenRegion() const { return _raygenRegion; }
        [[nodiscard]] const ::vk::StridedDeviceAddressRegionKHR& getMissRegion() const { return _missRegion; }
        [[nodiscard]] const ::vk::StridedDeviceAddressRegionKHR& getHitRegion() const { return _hitRegion; }
        [[nodiscard]] const ::vk::StridedDeviceAddressRegionKHR& getCallableRegion() const { return _callableRegion; }

    protected:
        void Setup() override;
        void Teardown() override;

    private:
        void buildShaderBindingTable(glm::u32 raygenCount, glm::u32 missCount, glm::u32 hitCount, glm::u32 callableCount);

        ::vk::PipelineLayout _pipelineLayout;

        ::vk::Buffer _sbtBuffer;
        VmaAllocation _sbtAllocation = nullptr;

        ::vk::StridedDeviceAddressRegionKHR _raygenRegion;
        ::vk::StridedDeviceAddressRegionKHR _missRegion;
        ::vk::StridedDeviceAddressRegionKHR _hitRegion;
        ::vk::StridedDeviceAddressRegionKHR _callableRegion;
    };
}
