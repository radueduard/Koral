//
// Created by radue on 3/6/2026.
//

#include "descriptorSetLayout.h"

#include "device.h"
#include "vulkanContext.h"

#include "vk_enum_conversions.h"

namespace gfx::vk
{
    DescriptorSetLayout::DescriptorSetLayout(const Builder& builder): gfx::DescriptorSetLayout(builder)
    {
        auto flags = std::vector<::vk::DescriptorBindingFlags>(_bindings.size());
        bool anyUpdateAfterBind = false;
        for (size_t i = 0; i < _bindings.size(); i++) {
            // if the count is unknown at pipeline creation time, we need to set the variable descriptor count flag
            flags[i] = ::vk::DescriptorBindingFlags();
            if (_bindings[i].second == 0) {
                flags[i] |= ::vk::DescriptorBindingFlagBits::eVariableDescriptorCount
                    | ::vk::DescriptorBindingFlagBits::ePartiallyBound
                    | ::vk::DescriptorBindingFlagBits::eUpdateAfterBind;
                anyUpdateAfterBind = true;
            }
        }

        const auto descriptorSetLayoutBindingCreateInfo = ::vk::DescriptorSetLayoutBindingFlagsCreateInfo()
            .setBindingCount(static_cast<uint32_t>(_bindings.size()))
            .setBindingFlags(flags);

        std::vector<::vk::DescriptorSetLayoutBinding> bindings(_bindings.size());
        for (auto [binding, typeAndCount] : _bindings) {
            const auto& [type, count] = typeAndCount;
            bindings[binding] = ::vk::DescriptorSetLayoutBinding()
                .setBinding(binding)
                .setDescriptorType(getVkDescriptorType(type))
                .setDescriptorCount(count == 0 ? 1000 : count)
                .setStageFlags(::vk::ShaderStageFlagBits::eAll);
        }

        const auto layoutCreateInfo = ::vk::DescriptorSetLayoutCreateInfo()
            .setBindings(bindings)
            .setFlags(anyUpdateAfterBind ? ::vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool : ::vk::DescriptorSetLayoutCreateFlags())
            .setPNext(&descriptorSetLayoutBindingCreateInfo);

        _handle = Context::Device()->createDescriptorSetLayout(layoutCreateInfo);
    }

    DescriptorSetLayout::~DescriptorSetLayout()
    {
        if (_handle) {
            Context::Device()->destroyDescriptorSetLayout(_handle);
        }
    }
}
