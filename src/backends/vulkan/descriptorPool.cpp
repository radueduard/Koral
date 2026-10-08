//
// Created by radue on 2/28/2026.
//

#include "descriptorPool.h"

#include <ranges>

#include "descriptorSetLayout.h"
#include "device.h"
#include "vulkanContext.h"
#include "vk_enum_conversions.h"

namespace kor::vk
{
    DescriptorPool::Builder & DescriptorPool::Builder::addPoolSize(const ::vk::DescriptorType type, const kor::u32 count) {
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

    DescriptorPool::Builder & DescriptorPool::Builder::setMaxSets(const kor::u32 count) {
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
        _pools.push_back(_handle);
    }

    DescriptorPool::~DescriptorPool() {
        for (const auto& pool : _pools) Context::Device()->destroyDescriptorPool(pool);
    }

    ::vk::DescriptorPool DescriptorPool::Grow() const
    {
        const auto poolCreateInfo = ::vk::DescriptorPoolCreateInfo()
            .setPoolSizes(_poolSizes)
            .setMaxSets(_maxSets)
            .setFlags(_flags);
        _pools.push_back(vk::Context::Device()->createDescriptorPool(poolCreateInfo));
        return _pools.back();
    }

    std::size_t DescriptorPool::PoolCount() const
    {
        std::lock_guard lock(_mutex);
        return _pools.size();
    }

    ::vk::DescriptorSet DescriptorPool::AllocateLocked(::vk::DescriptorSetAllocateInfo info) const
    {
        // The pool that last had room, then the others (sets freed since may have made some), then a new one.
        const auto full = [](const ::vk::Result result) {
            return result == ::vk::Result::eErrorOutOfPoolMemory || result == ::vk::Result::eErrorFragmentedPool;
        };
        ::vk::DescriptorSet set;
        ::vk::Result result = ::vk::Result::eErrorOutOfPoolMemory;
        for (std::size_t tried = 0; tried <= _pools.size() && full(result); ++tried) {
            const std::size_t index = tried < _pools.size() ? (_current + tried) % _pools.size() : _pools.size();
            const ::vk::DescriptorPool pool = index < _pools.size() ? _pools[index] : Grow();
            info.setDescriptorPool(pool);
            result = Context::Device()->allocateDescriptorSets(&info, &set);
            if (result == ::vk::Result::eSuccess) {
                _current = index;
                _owners[static_cast<VkDescriptorSet>(set)] = pool;
            }
        }
        if (result != ::vk::Result::eSuccess)
            throw std::runtime_error("vk::Device::allocateDescriptorSets: " + ::vk::to_string(result));
        return set;
    }

    ::vk::DescriptorSet DescriptorPool::Allocate(const kor::vk::DescriptorSetLayout& layout) const
    {
        const auto layoutHandle = *layout;

        kor::u32 variableDescriptorCount = 0;
        for (const auto& description : layout.Bindings() | std::views::values) {
            if (description.count == 0) {
                // Unbounded (bindless) array: allocate up to the layout's cap.
                // Must match descriptorSetLayout.cpp's bindless max (256).
                variableDescriptorCount = 256;
                break;
            }
        }
        const auto variableCountInfo = ::vk::DescriptorSetVariableDescriptorCountAllocateInfo()
            .setDescriptorCounts(variableDescriptorCount);

        // The pool, and the counts below, are shared by every thread that builds a descriptor set.
        std::lock_guard lock(_mutex);
        const auto allocateInfo = ::vk::DescriptorSetAllocateInfo()
            .setPNext(&variableCountInfo)
            .setDescriptorPool(_handle)
            .setSetLayouts(layoutHandle);

        for (const auto& description : layout.Bindings() | std::views::values) {
            _allocatedBindingCounts[description.type]++;
        }
        const ::vk::DescriptorSet set = AllocateLocked(allocateInfo);
        _allocatedSetCount++;
        return set;
    }

    std::vector<::vk::DescriptorSet> DescriptorPool::Allocate(const std::vector<kor::vk::DescriptorSetLayout>& layouts) const
    {
        auto layoutHandles = std::vector<::vk::DescriptorSetLayout>();
        for (const auto &layout: layouts) {
            layoutHandles.emplace_back(*layout);
        }

        // The pool, and the counts below, are shared by every thread that builds a descriptor set.
        std::lock_guard lock(_mutex);
        const auto allocateInfo = ::vk::DescriptorSetAllocateInfo()
            .setDescriptorPool(_handle)
            .setSetLayouts(layoutHandles);

        for (const auto &layout : layouts) {
            for (const auto& description : layout.Bindings() | std::views::values) {
                _allocatedBindingCounts[description.type]++;
            }
        }
        // One at a time: a pool with room for some of them and not the rest is not one to give up on.
        std::vector<::vk::DescriptorSet> allocatedSets;
        for (const auto& layoutHandle : layoutHandles)
            allocatedSets.push_back(AllocateLocked(::vk::DescriptorSetAllocateInfo().setSetLayouts(layoutHandle)));
        _allocatedSetCount += static_cast<kor::u32>(layouts.size());
        return allocatedSets;
    }

    void DescriptorPool::Free(const ::vk::DescriptorSet& descriptorSet) const
    {
        std::lock_guard lock(_mutex);
        const auto owner = _owners.find(static_cast<VkDescriptorSet>(descriptorSet));
        if (owner == _owners.end()) return;     // not one of ours, or freed already
        Context::Device()->freeDescriptorSets(owner->second, descriptorSet);
        _owners.erase(owner);
    }

    void DescriptorPool::Free(const std::vector<::vk::DescriptorSet>& descriptorSets) const
    {
        for (const auto& set : descriptorSets) Free(set);
    }

    void DescriptorPool::Reset() const
    {
        std::lock_guard lock(_mutex);
        for (const auto& pool : _pools) Context::Device()->resetDescriptorPool(pool);
        _owners.clear();
        _current = 0;
    }
}
