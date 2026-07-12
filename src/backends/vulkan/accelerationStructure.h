//
// Created by radue on 6/23/2026.
//

#pragma once

#include <accelerationStructure.h>

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>

#include "vk_wrapper.h"

namespace kor::vk
{
    class AccelerationStructure final : public kor::AccelerationStructure, public Wrapper<::vk::AccelerationStructureKHR>
    {
    public:
        explicit AccelerationStructure(const Builder& createInfo);

        ~AccelerationStructure() override;

        /** @brief GPU device address of this acceleration structure. */
        [[nodiscard]] ::vk::DeviceAddress getDeviceAddress() const { return _deviceAddress; }

    private:
        void buildBottomLevel(const Builder& createInfo);
        void buildTopLevel(const Builder& createInfo);

        // Records the build on a one-time compute command and stores the resulting
        // device address. Frees @p scratchBuffer (and @p instanceBuffer if any) after.
        void buildAndStore(
            ::vk::AccelerationStructureBuildGeometryInfoKHR buildInfo,
            const ::vk::AccelerationStructureBuildRangeInfoKHR* rangeInfos,
            const ::vk::AccelerationStructureBuildSizesInfoKHR& sizeInfo,
            const std::vector<glm::u32>& primitiveCounts);

        ::vk::Buffer _asBuffer;
        VmaAllocation _asAllocation = nullptr;
        ::vk::DeviceAddress _deviceAddress = 0;
    };
}
