//
// Created by radue on 3/5/2026.
//

#pragma once
#include <descriptorSet.h>

namespace gfx::ogl
{
    class DescriptorSet final : public gfx::DescriptorSet
    {
    public:
        explicit DescriptorSet(const Builder& builder);
        void Write(glm::u32 binding, const Descriptor &descriptor, glm::u32 index) override;
        void bind(const CommandBuffer& commandBuffer, glm::u32 index) const override;
    };
}
