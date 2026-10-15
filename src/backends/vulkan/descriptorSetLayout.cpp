//
// Created by radue on 3/6/2026.
//

#include "descriptorSetLayout.h"

#include "device.h"
#include "heaps.h"
#include "sampler.h"
#include "vulkanContext.h"

#include "vk_enum_conversions.h"

namespace kor::vk
{
    DescriptorSetLayout::DescriptorSetLayout(const Builder& builder): kor::DescriptorSetLayout(builder)
    {
        if (Context::Heaps()) {
            // Under the heap: one run of slots per set, a binding after the other in binding order, an
            // array taking one slot per element. A combined image sampler takes a slot in each heap — its
            // image in one, its sampler in the other — unless the pipeline embeds the sampler.
            for (const auto& [binding, description] : _bindings) {
                HeapBinding slots;
                slots.count = description.count == 0 ? UnboundedArrayCap : description.count;
                const bool embedded = description.embeddedSampler.Valid();
                const bool resource = description.type != DescriptorType::eSampler;
                const bool sampler = !embedded && (description.type == DescriptorType::eSampler
                                                   || description.type == DescriptorType::eCombinedImageSampler);
                if (resource) { slots.resource = _resourceSlots; _resourceSlots += slots.count; }
                if (sampler) { slots.sampler = _samplerSlots; _samplerSlots += slots.count; }
                _heapBindings[binding] = slots;
            }
            return;
        }

        // One entry a binding, in the same order in both arrays. The numbers need not run 0, 1, 2: a
        // pipeline may put a descriptor at binding 3 of a set with nothing else in it.
        std::vector<::vk::DescriptorBindingFlags> flags;
        std::vector<::vk::DescriptorSetLayoutBinding> bindings;
        // Kept alive until the layout is made: an embedded sampler is the same handle once per element.
        std::vector<std::vector<::vk::Sampler>> immutableSamplers;
        flags.reserve(_bindings.size());
        bindings.reserve(_bindings.size());
        immutableSamplers.reserve(_bindings.size());
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
            const auto count = description.count == 0 ? UnboundedArrayCap : description.count;
            auto layoutBinding = ::vk::DescriptorSetLayoutBinding()
                .setBinding(binding)
                .setDescriptorType(getVkDescriptorType(description.type))
                .setDescriptorCount(count)
                .setStageFlags(::vk::ShaderStageFlagBits::eAll);
            // An embedded sampler is an immutable one here: written into the layout, never into a set.
            if (description.embeddedSampler.Valid()) {
                const auto handle = *dynamic_cast<const vk::Sampler&>(*description.embeddedSampler);
                layoutBinding.setImmutableSamplers(immutableSamplers.emplace_back(count, handle));
            }
            bindings.push_back(layoutBinding);
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
