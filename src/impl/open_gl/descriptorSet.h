//
// Created by radue on 3/5/2026.
//

#pragma once
#include "core/descriptorSet.h"

namespace gfx::ogl
{
    class DescriptorSet final : public gfx::DescriptorSet
    {
    public:
        explicit DescriptorSet(const Builder& builder);
        void bind(const CommandBuffer& commandBuffer, glm::u32 index) const override;
    };
}
