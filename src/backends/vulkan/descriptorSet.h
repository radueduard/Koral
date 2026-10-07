//
// Created by radue on 3/6/2026.
//

#pragma once

#include <descriptorSet.h>
#include <vulkan/vulkan.hpp>

namespace kor::vk
{
    class DescriptorSet final : public kor::DescriptorSet
    {
    public:

        explicit DescriptorSet(const Builder& builder);
        ~DescriptorSet() override;

        void Rebind(kor::u32 binding, const Descriptor &descriptor, kor::u32 index) override;

        void DebugPrint() const override;

        ::vk::DescriptorSet operator*() const;
    private:
        std::vector<::vk::DescriptorSet> _descriptorSets;
    };
}
