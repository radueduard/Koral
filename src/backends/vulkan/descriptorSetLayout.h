//
// Created by radue on 3/6/2026.
//

#pragma once

#include <descriptorSetLayout.h>

#include "vk_wrapper.h"
#include <vulkan/vulkan.hpp>

namespace gfx::vk
{
    class DescriptorSetLayout final : public gfx::DescriptorSetLayout, public Wrapper<::vk::DescriptorSetLayout>
    {
    public:
        explicit DescriptorSetLayout(const Builder& builder);
        ~DescriptorSetLayout() override;
    };
}
