//
// Created by radue on 3/4/2026.
//

#pragma once
#include <map>
#include <memory>
#include <vector>
#include <glm/fwd.hpp>

#include "descriptorSetLayout.h"
#include "descriptorBinding.h"
#include "api.h"

namespace gfx
{
    class CommandBuffer;
    class Descriptor;

    class GFX_API DescriptorSet
    {
    public:
        struct GFX_API Builder
        {
            explicit Builder(const DescriptorSetLayout& layout);

            const DescriptorSetLayout& layout;
            std::map<glm::u32, std::vector<Descriptor>> writes;

            Builder& write(glm::u32 binding, const Descriptor& descriptor, glm::u32 index = 0);
            std::unique_ptr<DescriptorSet> build();
        };

        virtual ~DescriptorSet() = default;

        virtual void bind(const CommandBuffer& commandBuffer, glm::u32 index) const {};

    protected:
        explicit DescriptorSet(const Builder &builder);
        bool _isPerFrame = false;
        const gfx::DescriptorSetLayout& _layout;
        std::map<glm::u32, std::vector<Descriptor>> _writes;
    };
}
