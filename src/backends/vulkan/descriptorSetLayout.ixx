//
// Created by radue on 3/6/2026.
//

module;

#include "vk_wrapper.h"
#include <vulkan/vulkan.hpp>

export module gfx:vk_descriptorSetLayout;
import :vk_types;

import :descriptorSetLayout;

namespace gfx::vk
{
    class DescriptorSetLayout final : public gfx::DescriptorSetLayout, public Wrapper<::vk::DescriptorSetLayout>
    {
    public:
        explicit DescriptorSetLayout(const Builder& builder);
        ~DescriptorSetLayout() override;
    };
}
