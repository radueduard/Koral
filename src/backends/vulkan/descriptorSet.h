//
// Created by radue on 3/6/2026.
//

#pragma once

#include <descriptorSet.h>
#include <vulkan/vulkan.hpp>

namespace gfx::vk
{
    class DescriptorSet final : public gfx::DescriptorSet
    {
    public:

        explicit DescriptorSet(const Builder& builder);
        ~DescriptorSet() override;

        void Write(glm::u32 binding, const Descriptor &descriptor, glm::u32 index) override;

        void DebugPrint() const override;

        ::vk::DescriptorSet operator*() const;
    private:
        std::vector<::vk::DescriptorSet> _descriptorSets;
    };
}
