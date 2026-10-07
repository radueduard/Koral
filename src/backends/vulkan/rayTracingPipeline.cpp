//
// Created by radue on 6/23/2026.
//
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include "rayTracingPipeline.h"

#include <ranges>
#include <vector>

#include "allocator.h"
#include "commandBuffer.h"
#include "descriptorSetLayout.h"
#include "device.h"
#include "physicalDevice.h"
#include "runtime.h"
#include "shader.h"
#include "vk_enum_conversions.h"
#include "vulkanContext.h"

namespace kor::vk
{
    namespace
    {
        constexpr kor::u64 alignUp(const kor::u64 value, const kor::u64 alignment)
        {
            return (value + alignment - 1) & ~(alignment - 1);
        }

        ::vk::PhysicalDeviceRayTracingPipelinePropertiesKHR getRayTracingProperties()
        {
            ::vk::PhysicalDeviceRayTracingPipelinePropertiesKHR rtProperties;
            ::vk::PhysicalDeviceProperties2 properties2;
            properties2.pNext = &rtProperties;
            (*Context::Runtime().getPhysicalDevice()).getProperties2(&properties2);
            return rtProperties;
        }
    }

    RayTracingPipeline::RayTracingPipeline(const Builder& createInfo) : kor::RayTracingPipeline(createInfo)
    {
        Setup();
    }

    RayTracingPipeline::~RayTracingPipeline()
    {
        Teardown();
    }

    void RayTracingPipeline::Bind(const kor::CommandBuffer& commandBuffer) const
    {
        const auto& vkCommandBuffer = dynamic_cast<const vk::CommandBuffer&>(commandBuffer);
        vkCommandBuffer->bindPipeline(::vk::PipelineBindPoint::eRayTracingKHR, _handle);
    }

    void RayTracingPipeline::Setup()
    {
        // Not every device advertises the ray tracing extensions (see runtime.h); on one that
        // doesn't, vkCreateRayTracingPipelinesKHR was never resolved by the loader, so calling it
        // below would be a null function pointer rather than a clean Vulkan error.
        if (!Context::Device().supportsRayTracing()) {
            throw std::runtime_error("Cannot create a ray tracing pipeline: this device has no "
                                      "ray tracing support (VK_KHR_ray_tracing_pipeline / "
                                      "VK_KHR_acceleration_structure are not available).");
        }

        // Pipeline layout from the merged descriptor set layouts and push constants.
        std::vector<::vk::DescriptorSetLayout> setLayouts;
        if (!_setLayouts.empty()) {
            setLayouts = { std::ranges::max(_setLayouts | std::views::keys) + 1, nullptr };
            for (const auto& [set, layout] : _setLayouts) {
                setLayouts[set] = **dynamic_cast<const DescriptorSetLayout*>(layout.Get());
            }
        }

        std::vector<::vk::PushConstantRange> pushConstantRanges;
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

        // Build the shader stages and groups. The group order is:
        // [raygen][miss...][hit...][callable...], which mirrors the SBT layout.
        std::vector<::vk::PipelineShaderStageCreateInfo> shaderStages;
        std::vector<::vk::RayTracingShaderGroupCreateInfoKHR> shaderGroups;

        auto addStage = [&](const ResourceRef<const kor::Shader>& shaderRef, const ::vk::ShaderStageFlagBits stage) {
            const auto& shader = dynamic_cast<const vk::Shader&>(*shaderRef);
            const auto index = static_cast<kor::u32>(shaderStages.size());
            shaderStages.push_back(::vk::PipelineShaderStageCreateInfo()
                .setStage(stage)
                .setModule(*shader)
                .setPName("main"));
            return index;
        };

        auto generalGroup = [&](const kor::u32 stageIndex) {
            shaderGroups.push_back(::vk::RayTracingShaderGroupCreateInfoKHR()
                .setType(::vk::RayTracingShaderGroupTypeKHR::eGeneral)
                .setGeneralShader(stageIndex)
                .setClosestHitShader(VK_SHADER_UNUSED_KHR)
                .setAnyHitShader(VK_SHADER_UNUSED_KHR)
                .setIntersectionShader(VK_SHADER_UNUSED_KHR));
        };

        // Raygen (exactly one, validated by the core layer).
        generalGroup(addStage(*_raygenShader, ::vk::ShaderStageFlagBits::eRaygenKHR));

        // Miss shaders.
        for (const auto& missShader : _missShaders) {
            generalGroup(addStage(missShader, ::vk::ShaderStageFlagBits::eMissKHR));
        }

        // Hit groups.
        for (const auto& hitGroup : _hitGroups) {
            auto group = ::vk::RayTracingShaderGroupCreateInfoKHR()
                .setType(hitGroup.intersectionShader.has_value()
                    ? ::vk::RayTracingShaderGroupTypeKHR::eProceduralHitGroup
                    : ::vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup)
                .setGeneralShader(VK_SHADER_UNUSED_KHR)
                .setClosestHitShader(hitGroup.closestHitShader.has_value()
                    ? addStage(*hitGroup.closestHitShader, ::vk::ShaderStageFlagBits::eClosestHitKHR)
                    : VK_SHADER_UNUSED_KHR)
                .setAnyHitShader(hitGroup.anyHitShader.has_value()
                    ? addStage(*hitGroup.anyHitShader, ::vk::ShaderStageFlagBits::eAnyHitKHR)
                    : VK_SHADER_UNUSED_KHR)
                .setIntersectionShader(hitGroup.intersectionShader.has_value()
                    ? addStage(*hitGroup.intersectionShader, ::vk::ShaderStageFlagBits::eIntersectionKHR)
                    : VK_SHADER_UNUSED_KHR);
            shaderGroups.push_back(group);
        }

        // Callable shaders.
        for (const auto& callableShader : _callableShaders) {
            generalGroup(addStage(callableShader, ::vk::ShaderStageFlagBits::eCallableKHR));
        }

        const auto pipelineCreateInfo = ::vk::RayTracingPipelineCreateInfoKHR()
            .setStages(shaderStages)
            .setGroups(shaderGroups)
            .setMaxPipelineRayRecursionDepth(_maxRecursionDepth)
            .setLayout(_pipelineLayout);

        const auto result = Context::Device()->createRayTracingPipelineKHR(nullptr, nullptr, pipelineCreateInfo);
        if (result.result != ::vk::Result::eSuccess) {
            throw std::runtime_error("Failed to create ray tracing pipeline: " + ::vk::to_string(result.result));
        }
        _handle = result.value;

        buildShaderBindingTable(1, static_cast<kor::u32>(_missShaders.size()),
            static_cast<kor::u32>(_hitGroups.size()), static_cast<kor::u32>(_callableShaders.size()));
    }

    void RayTracingPipeline::buildShaderBindingTable(const kor::u32 raygenCount, const kor::u32 missCount,
        const kor::u32 hitCount, const kor::u32 callableCount)
    {
        const auto rtProperties = getRayTracingProperties();
        const kor::u64 handleSize = rtProperties.shaderGroupHandleSize;
        const kor::u64 handleAlignment = rtProperties.shaderGroupHandleAlignment;
        const kor::u64 baseAlignment = rtProperties.shaderGroupBaseAlignment;
        const kor::u64 handleSizeAligned = alignUp(handleSize, handleAlignment);

        const kor::u32 groupCount = raygenCount + missCount + hitCount + callableCount;

        // Region strides/sizes. The raygen region must have size == stride.
        _raygenRegion.stride = alignUp(handleSizeAligned, baseAlignment);
        _raygenRegion.size = _raygenRegion.stride;
        _missRegion.stride = handleSizeAligned;
        _missRegion.size = alignUp(missCount * handleSizeAligned, baseAlignment);
        _hitRegion.stride = handleSizeAligned;
        _hitRegion.size = alignUp(hitCount * handleSizeAligned, baseAlignment);
        _callableRegion.stride = handleSizeAligned;
        _callableRegion.size = alignUp(callableCount * handleSizeAligned, baseAlignment);

        // Fetch the opaque group handles (tightly packed, unaligned).
        const kor::u64 handlesDataSize = groupCount * handleSize;
        std::vector<std::byte> handles(handlesDataSize);
        const auto handlesResult = Context::Device()->getRayTracingShaderGroupHandlesKHR(
            _handle, 0, groupCount, handlesDataSize, handles.data());
        if (handlesResult != ::vk::Result::eSuccess) {
            throw std::runtime_error("Failed to fetch ray tracing shader group handles: " + ::vk::to_string(handlesResult));
        }

        const kor::u64 sbtSize = _raygenRegion.size + _missRegion.size + _hitRegion.size + _callableRegion.size;

        const auto bufferCreateInfo = ::vk::BufferCreateInfo()
            .setSize(sbtSize)
            .setUsage(::vk::BufferUsageFlagBits::eShaderBindingTableKHR | ::vk::BufferUsageFlagBits::eShaderDeviceAddress)
            .setSharingMode(::vk::SharingMode::eExclusive);
        std::tie(_sbtBuffer, _sbtAllocation) = Context::Allocator().AllocateBuffer(
            bufferCreateInfo, VMA_MEMORY_USAGE_AUTO,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);

        const ::vk::DeviceAddress sbtAddress = Context::Device()->getBufferAddress(
            ::vk::BufferDeviceAddressInfo().setBuffer(_sbtBuffer));

        kor::u64 raygenOffset = 0;
        kor::u64 missOffset = raygenOffset + _raygenRegion.size;
        kor::u64 hitOffset = missOffset + _missRegion.size;
        kor::u64 callableOffset = hitOffset + _hitRegion.size;

        _raygenRegion.deviceAddress = missCount + hitCount + callableCount + raygenCount > 0 ? sbtAddress + raygenOffset : 0;
        _missRegion.deviceAddress = missCount > 0 ? sbtAddress + missOffset : 0;
        _hitRegion.deviceAddress = hitCount > 0 ? sbtAddress + hitOffset : 0;
        _callableRegion.deviceAddress = callableCount > 0 ? sbtAddress + callableOffset : 0;

        // Copy each opaque handle into its slot in the SBT buffer.
        auto* sbt = static_cast<std::byte*>(Context::Allocator().MapMemory(_sbtAllocation));
        auto handleAt = [&](const kor::u32 groupIndex) {
            return handles.data() + groupIndex * handleSize;
        };

        kor::u32 group = 0;
        // Raygen.
        std::memcpy(sbt + raygenOffset, handleAt(group++), handleSize);
        // Miss.
        for (kor::u32 i = 0; i < missCount; ++i) {
            std::memcpy(sbt + missOffset + i * _missRegion.stride, handleAt(group++), handleSize);
        }
        // Hit.
        for (kor::u32 i = 0; i < hitCount; ++i) {
            std::memcpy(sbt + hitOffset + i * _hitRegion.stride, handleAt(group++), handleSize);
        }
        // Callable.
        for (kor::u32 i = 0; i < callableCount; ++i) {
            std::memcpy(sbt + callableOffset + i * _callableRegion.stride, handleAt(group++), handleSize);
        }

        Context::Allocator().FlushAllocation(_sbtAllocation);
        Context::Allocator().UnmapMemory(_sbtAllocation);
    }

    void RayTracingPipeline::Teardown()
    {
        // Torn down on destruction and on every hot reload, while frames in flight may still be
        // bound to it — so it goes once the GPU is done, rather than after stalling every queue.
        Context::DestroyWhenUnused([sbt = _sbtBuffer, sbtAllocation = _sbtAllocation,
                                    layout = _pipelineLayout, pipeline = _handle] {
            if (sbt) Context::Allocator().FreeBuffer(sbt, sbtAllocation);
            if (layout) Context::Device()->destroyPipelineLayout(layout);
            if (pipeline) Context::Device()->destroyPipeline(pipeline);
        });

        _sbtBuffer = nullptr;
        _sbtAllocation = nullptr;
        _pipelineLayout = nullptr;
        _handle = nullptr;
        _raygenRegion = ::vk::StridedDeviceAddressRegionKHR();
        _missRegion = ::vk::StridedDeviceAddressRegionKHR();
        _hitRegion = ::vk::StridedDeviceAddressRegionKHR();
        _callableRegion = ::vk::StridedDeviceAddressRegionKHR();
    }
}
