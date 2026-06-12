//
// Created by radue on 2/28/2026.
//

module;

#include <ranges>
#include <glm/fwd.hpp>

#include "vulkanContext.h"
#include "vk_enum_conversions.h"

module vk.descriptorPool;

namespace gfx::vk
{
    DescriptorPool::Builder & DescriptorPool::Builder::addPoolSize(const ::vk::DescriptorType type, const glm::u32 count) {
        const auto DescriptorPoolSize = ::vk::DescriptorPoolSize()
            .setType(type)
            .setDescriptorCount(count);
        _poolSizes.emplace_back(DescriptorPoolSize);
        return *this;
    }

    DescriptorPool::Builder & DescriptorPool::Builder::setPoolFlags(const ::vk::DescriptorPoolCreateFlags flags) {
        _flags = flags;
        return *this;
    }

    DescriptorPool::Builder & DescriptorPool::Builder::setMaxSets(const glm::u32 count) {
        _maxSets = count;
        return *this;
    }

    DescriptorPool* DescriptorPool::Builder::build() const {
        return new DescriptorPool(*this);
    }

    DescriptorPool::DescriptorPool(const Builder &builder) :
        _poolSizes(builder._poolSizes),
        _flags(builder._flags),
        _maxSets(builder._maxSets)
    {
        const auto poolCreateInfo = ::vk::DescriptorPoolCreateInfo()
            .setPoolSizes(_poolSizes)
            .setMaxSets(_maxSets)
            .setFlags(_flags);

    	for (const auto& size : _poolSizes) {
			_allocatedBindingCounts[getDescriptorType(size.type)] = 0;
		}

        _handle = vk::Context::Device()->createDescriptorPool(poolCreateInfo);
    }

    DescriptorPool::~DescriptorPool() {
        Context::Device()->destroyDescriptorPool(_handle);
    }

    ::vk::DescriptorSet DescriptorPool::Allocate(const gfx::vk::DescriptorSetLayout& layout) const
    {
        const auto layoutHandle = *layout;

        glm::u32 descriptorCount = 0;
        for (const auto& [_a, _b, count] : layout.getBindings()) {
            if (count == 0) {
                // Unbounded (bindless) array: reserve a capacity that matches the
                // renderer's material ceiling (Material::propertiesBuffer holds
                // 256). 64 overflowed on material-heavy scenes (e.g. Bistro, 132
                // materials), corrupting descriptor memory.
                descriptorCount += 256;
                break;
            }
            descriptorCount += count;
        }
        const auto variableCountInfo = ::vk::DescriptorSetVariableDescriptorCountAllocateInfo()
            .setDescriptorCounts(descriptorCount);

        const auto allocateInfo = ::vk::DescriptorSetAllocateInfo()
            .setPNext(&variableCountInfo)
            .setDescriptorPool(_handle)
            .setSetLayouts({layoutHandle});

        for (const auto& binding : layout.getBindings() | std::views::values) {
            _allocatedBindingCounts[binding]++;
        }
        const auto allocatedSets = Context::Device()->allocateDescriptorSets(allocateInfo);
        if (allocatedSets.empty()) {
            throw std::runtime_error("Failed to allocate descriptor set!");
        }

        _allocatedSetCount++;
        return allocatedSets[0];
    }

    std::vector<::vk::DescriptorSet> DescriptorPool::Allocate(const std::vector<gfx::vk::DescriptorSetLayout>& layouts) const
    {
        auto layoutHandles = std::vector<::vk::DescriptorSetLayout>();
        for (const auto &layout: layouts) {
            layoutHandles.emplace_back(*layout);
        }

        const auto allocateInfo = ::vk::DescriptorSetAllocateInfo()
            .setDescriptorPool(_handle)
            .setSetLayouts(layoutHandles);

        for (const auto &layout : layouts) {
            for (const auto& binding : layout.getBindings() | std::views::values) {
                _allocatedBindingCounts[binding]++;
            }
        }
        const auto allocatedSets = Context::Device()->allocateDescriptorSets(allocateInfo);
        if (allocatedSets.size() != layouts.size()) {
            throw std::runtime_error("Failed to allocate descriptor sets!");
        }
        _allocatedSetCount += static_cast<glm::u32>(layouts.size());
        return allocatedSets;
    }

    void DescriptorPool::Free(const ::vk::DescriptorSet& descriptorSet) const
    {
        Context::Device()->freeDescriptorSets(_handle, descriptorSet);
    }

    void DescriptorPool::Free(const std::vector<::vk::DescriptorSet>& descriptorSets) const
    {
        Context::Device()->freeDescriptorSets(_handle, descriptorSets);
    }

    void DescriptorPool::Reset() const { Context::Device()->resetDescriptorPool(_handle); }
}
