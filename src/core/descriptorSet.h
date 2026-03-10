//
// Created by radue on 3/4/2026.
//

#pragma once
#include <map>
#include <memory>
#include <vector>
#include <glm/fwd.hpp>

namespace gfx
{
    class CommandBuffer;
    class DescriptorSetLayout;
    class Descriptor;

    class DescriptorSet
    {
    public:
        struct Builder
        {
            explicit Builder(const DescriptorSetLayout& layout);

            const DescriptorSetLayout& layout;
            std::map<glm::u32, std::vector<Descriptor>> writes;

            Builder& write(glm::u32 binding, const Descriptor& descriptor, glm::u32 index = 0);
            std::unique_ptr<DescriptorSet> build();
        };

        virtual ~DescriptorSet() = default;

        virtual void bind(const CommandBuffer& commandBuffer, glm::u32 index) const = 0;

    protected:
        explicit DescriptorSet(const Builder &builder);

        const gfx::DescriptorSetLayout& _layout;
        std::map<glm::u32, std::vector<Descriptor>> _writes;
    };
}
