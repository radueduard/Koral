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
        // One entry a binding, in the same order in both arrays. The numbers need not run 0, 1, 2: a
        // pipeline may put a descriptor at binding 3 of a set with nothing else in it.
        std::vector<::vk::DescriptorBindingFlags> flags;
        std::vector<::vk::DescriptorSetLayoutBinding> bindings;
        flags.reserve(_bindings.size());
        bindings.reserve(_bindings.size());
        bool anyUpdateAfterBind = false;
        for (const auto& [binding, description] : _bindings) {
            // A count unknown when the pipeline is made (0) is a variable-count, partly bound array.
            auto flag = ::vk::DescriptorBindingFlags();
            if (description.count == 0) {
                flag |= ::vk::DescriptorBindingFlagBits::eVariableDescriptorCount
                    | ::vk::DescriptorBindingFlagBits::ePartiallyBound
                    | ::vk::DescriptorBindingFlagBits::eUpdateAfterBind;
                anyUpdateAfterBind = true;
            }
            flags.push_back(flag);
            bindings.push_back(::vk::DescriptorSetLayoutBinding()
                .setBinding(binding)
                .setDescriptorType(getVkDescriptorType(description.type))
                .setDescriptorCount(description.count == 0 ? 256 : description.count)
                .setStageFlags(::vk::ShaderStageFlagBits::eAll));
        }

        const auto descriptorSetLayoutBindingCreateInfo = ::vk::DescriptorSetLayoutBindingFlagsCreateInfo()
            .setBindingFlags(flags);

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
