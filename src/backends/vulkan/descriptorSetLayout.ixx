//
// Created by radue on 3/6/2026.
//

module;

#include "vk_wrapper.h"
#include <vulkan/vulkan.hpp>

export module vk.descriptorSetLayout;
import gfx.descriptorSetLayout;

namespace gfx::vk
{
    export class DescriptorSetLayout final : public gfx::DescriptorSetLayout, public Wrapper<::vk::DescriptorSetLayout>
    {
    public:
        explicit DescriptorSetLayout(const Builder& builder);
        ~DescriptorSetLayout() override;
    };
}
