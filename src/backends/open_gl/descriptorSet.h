//
// Created by radue on 3/5/2026.
//

#pragma once
#include <descriptorSet.h>

namespace kor::ogl
{
    class DescriptorSet final : public kor::DescriptorSet
    {
    public:
        explicit DescriptorSet(const Builder& builder);
        void Rebind(glm::u32 binding, const Descriptor &descriptor, glm::u32 index) override;
        void Bind(const CommandBuffer& commandBuffer, glm::u32 index) const override;
    };
}
