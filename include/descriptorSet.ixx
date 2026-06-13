//
// Created by radue on 3/4/2026.
//

module;

#include <glm/fwd.hpp>
#include "api.h"

export module gfx:descriptorSet;
import :types;
import :descriptor;

import std;
import resource;

namespace gfx
{
    struct DescriptorWrites;  // defined in descriptorSet.cpp — pimpl to avoid GCC serializer bug

    class GFX_API DescriptorSet
    {
    public:
        struct GFX_API Builder
        {
            explicit Builder(const DescriptorSetLayout& layout);

            const DescriptorSetLayout* layout;
            std::map<glm::u32, std::vector<Descriptor>> writes;

            Builder& write(glm::u32 binding, const Descriptor& descriptor, glm::u32 index = 0);
            Resource<DescriptorSet> build();
        };

        virtual ~DescriptorSet();  // user-declared so unique_ptr<DescriptorWrites> destructor fires in .cpp

        virtual void bind(const CommandBuffer& commandBuffer, glm::u32 index) const;
        virtual void Write(glm::u32 binding, const Descriptor& descriptor, glm::u32 index) = 0;

        virtual void DebugPrint() const;

    protected:
        explicit DescriptorSet(const Builder &builder);
        bool _isPerFrame = false;
        const gfx::DescriptorSetLayout* _layout;
        std::unique_ptr<DescriptorWrites> _writes;

        std::map<glm::u32, std::vector<Descriptor>>& getWrites();
        const std::map<glm::u32, std::vector<Descriptor>>& getWrites() const;
    };
}
