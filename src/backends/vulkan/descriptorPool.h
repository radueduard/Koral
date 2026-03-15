//
// Created by radue on 2/28/2026.
//

#pragma once

#include <unordered_map>

#include <glm/fwd.hpp>

#include <vulkan/vulkan.hpp>

#include "vk_wrapper.h"

namespace gfx
{
    enum class DescriptorType;
}

namespace gfx::vk
{
    class DescriptorSetLayout;

    class DescriptorPool : public Wrapper<::vk::DescriptorPool> {
    public:
        class Builder {
            friend class DescriptorPool;
        public:
            Builder &addPoolSize(::vk::DescriptorType type, glm::u32 count);
            Builder &setPoolFlags(::vk::DescriptorPoolCreateFlags flags);
            Builder &setMaxSets(glm::u32 count);
            [[nodiscard]] DescriptorPool* build() const;

        private:
            std::vector<::vk::DescriptorPoolSize> _poolSizes = {};
            ::vk::DescriptorPoolCreateFlags _flags = {};
            glm::u32 _maxSets = 1000;
        };

        explicit DescriptorPool(const Builder &builder);
        ~DescriptorPool() override;
        DescriptorPool(const DescriptorPool &) = delete;
        DescriptorPool &operator=(const DescriptorPool &) = delete;

        [[nodiscard]] ::vk::DescriptorSet Allocate(const gfx::vk::DescriptorSetLayout &layout) const;
        [[nodiscard]] std::vector<::vk::DescriptorSet> Allocate(const std::vector<gfx::vk::DescriptorSetLayout> &layouts) const;
        void Free(const ::vk::DescriptorSet &descriptorSet) const;
        void Free(const std::vector<::vk::DescriptorSet> &descriptorSets) const;
        void Reset() const;

    private:
        mutable glm::u32 _allocatedSetCount = 0;
        mutable std::unordered_map<DescriptorType, glm::u32> _allocatedBindingCounts;

        std::vector<::vk::DescriptorPoolSize> _poolSizes;
        ::vk::DescriptorPoolCreateFlags _flags;
        glm::u32 _maxSets;
    };
}
