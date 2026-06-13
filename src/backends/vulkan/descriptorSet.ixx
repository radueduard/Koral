//
// Created by radue on 3/6/2026.
//

module;

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>

export module gfx:vk_descriptorSet;
import :vk_types;

import :descriptorSet;

namespace gfx::vk
{
    export class DescriptorSet final : public gfx::DescriptorSet
    {
    public:

        explicit DescriptorSet(const Builder& builder);
        ~DescriptorSet() override;

        void Write(glm::u32 binding, const gfx::Descriptor &descriptor, glm::u32 index) override;

        void DebugPrint() const override;

        ::vk::DescriptorSet operator*() const;
    private:
        std::vector<::vk::DescriptorSet> _descriptorSets;
    };
}
