//
// Created by radue on 3/5/2026.
//

module;

#include <glm/glm.hpp>

export module ogl.descriptorSet;

import gfx.descriptorSet;
import gfx.descriptorBinding;
import gfx.commandBuffer;

namespace gfx::ogl
{
    class DescriptorSet final : public gfx::DescriptorSet
    {
    public:
        explicit DescriptorSet(const Builder& builder);
        void Write(glm::u32 binding, const gfx::Descriptor &descriptor, glm::u32 index) override;
        void bind(const gfx::CommandBuffer& commandBuffer, glm::u32 index) const override;
    };
}
