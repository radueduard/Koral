//
// Created by radue on 3/6/2026.
//

#include "descriptorSetLayout.h"

#include "device.h"
#include "vulkanContext.h"

#include "vk_enum_conversions.h"

namespace kor::vk
{
    DescriptorSetLayout::DescriptorSetLayout(const Builder& builder): kor::DescriptorSetLayout(builder)
    {
        auto flags = std::vector<::vk::DescriptorBindingFlags>(_bindings.size());
        bool anyUpdateAfterBind = false;
        for (size_t i = 0; i < _bindings.size(); i++) {
            // if the count is unknown at pipeline creation time, we need to set the variable descriptor count flag
            flags[i] = ::vk::DescriptorBindingFlags();
            if (_bindings[i].count == 0) {
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
        for (const auto& [binding, description] : _bindings) {
            bindings[binding] = ::vk::DescriptorSetLayoutBinding()
                .setBinding(binding)
                .setDescriptorType(getVkDescriptorType(description.type))
                .setDescriptorCount(description.count == 0 ? 256 : description.count)
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
