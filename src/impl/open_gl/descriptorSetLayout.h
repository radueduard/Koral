//
// Created by radue on 3/6/2026.
//

#pragma once
#include "core/descriptorSetLayout.h"

namespace gfx::ogl
{
    class DescriptorSetLayout final : public gfx::DescriptorSetLayout
    {
    public:
        explicit DescriptorSetLayout(const Builder& builder)
            : gfx::DescriptorSetLayout(builder)
        {

        }
    };
}
