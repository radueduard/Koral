//
// Created by radue on 2/28/2026.
//

#include "descriptorPool.h"

#include "device.h"
#include "vulkanContext.h"

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

    std::unique_ptr<DescriptorPool> DescriptorPool::Builder::build() const {
        return std::make_unique<DescriptorPool>(*this);
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
			_allocatedBindingCounts[size.type] = 0;
		}

        _handle = vk::Context::Device()->createDescriptorPool(poolCreateInfo);
    }

    DescriptorPool::~DescriptorPool() {
        Context::Device()->destroyDescriptorPool(_handle);
    }

  //   vk::DescriptorSet DescriptorPool::Allocate(const SetLayout &layout) {
  //       const auto layoutHandle = *layout;
  //       const auto allocateInfo = vk::DescriptorSetAllocateInfo()
  //           .setDescriptorDescriptorPool(m_DescriptorPool)
  //           .setSetLayouts({layoutHandle});
  //
  //   	for (const auto& binding : layout.Bindings()| std::views::values) {
  //   		m_allocatedBindingCounts[binding.descriptorType]++;
  //   	}
		// const auto allocatedSets = Context::Device()->allocateDescriptorSets(allocateInfo);
  //   	if (allocatedSets.empty()) {
  //   		throw std::runtime_error("Failed to allocate descriptor set!");
  //   	}
  //
  //   	m_allocatedSetCount++;
  //
  //       return allocatedSets[0];
  //   }
  //
  //   std::vector<vk::DescriptorSet> DescriptorPool::Allocate(const std::vector<SetLayout> &layouts) {
  //       auto layoutHandles = std::vector<vk::DescriptorSetLayout>();
  //       for (const auto &layout: layouts) {
  //           layoutHandles.emplace_back(*layout);
  //       }
  //
  //       const auto allocateInfo = vk::DescriptorSetAllocateInfo()
  //           .setDescriptorDescriptorPool(m_DescriptorPool)
  //           .setSetLayouts(layoutHandles);
  //
  //   	for (const auto &layout : layouts) {
		// 	for (const auto& binding : layout.Bindings()| std::views::values) {
		// 		m_allocatedBindingCounts[binding.descriptorType]++;
		// 	}
		// }
		// const auto allocatedSets = Context::Device()->allocateDescriptorSets(allocateInfo);
  //   	if (allocatedSets.size() != layouts.size()) {
  //   		throw std::runtime_error("Failed to allocate descriptor sets!");
  //   	}
  //   	m_allocatedSetCount += static_cast<u32>(layouts.size());
  //       return allocatedSets;
  //   }

    // void DescriptorPool::Free(const vk::DescriptorSet &descriptorSet) const {
    //     Context::Device()->freeDescriptorSets(m_DescriptorPool, descriptorSet);
    // }
    //
    // void DescriptorPool::Free(const std::vector<vk::DescriptorSet> &descriptorSets) const {
    //     Context::Device()->freeDescriptorSets(m_DescriptorPool, descriptorSets);
    // }

	void DescriptorPool::Reset() const { Context::Device()->resetDescriptorPool(_handle); }
}
