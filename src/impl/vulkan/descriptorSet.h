//
// Created by radue on 3/6/2026.
//

#pragma once
#include "core/descriptorSet.h"
#include "utils/vk_wrapper.h"

#include <vulkan/vulkan.hpp>

namespace gfx::vk
{
    class DescriptorSet final : public gfx::DescriptorSet
    {
    public:
        explicit DescriptorSet(const Builder& builder);
        ~DescriptorSet() override;
        ::vk::DescriptorSet operator*() const;

    private:
        std::vector<::vk::DescriptorSet> _descriptorSets;
    };
}
