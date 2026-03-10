//
// Created by radue on 3/6/2026.
//

#pragma once
#include "core/descriptorSet.h"
#include "utils/vk_wrapper.h"

#include <vulkan/vulkan.hpp>

namespace gfx::vk
{
    class DescriptorSet final : public gfx::DescriptorSet, public Wrapper<::vk::DescriptorSet>
    {
    public:
        explicit DescriptorSet(const Builder& builder);
        ~DescriptorSet() override;

        void bind(const gfx::CommandBuffer& commandBuffer, glm::u32 index) const override;
    };
}
