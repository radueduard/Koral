//
// How a pipeline's shaders reach their descriptors. See pipelineInterface.h.
//

#include "pipelineInterface.h"

#include <format>
#include <ranges>
#include <stdexcept>

#include <bindless.h>

#include "bindlessTables.h"
#include "descriptorSetLayout.h"
#include "device.h"
#include "heaps.h"
#include "sampler.h"
#include "vk_enum_conversions.h"
#include "vulkanContext.h"

namespace kor::vk
{
    kor::u32 HeapSetPushOffset(const kor::u32 set)
    {
        const auto* heaps = Context::Heaps();
        const kor::u32 size = heaps ? heaps->PushDataSize() : 256;
        return size - 8 * (set + 1);
    }

    PipelineInterface::PipelineInterface(const std::map<kor::u32, Resource<kor::DescriptorSetLayout>>& sets,
                                         const std::map<kor::u32, kor::Shader::PushConstant>& pushConstants,
                                         const std::map<kor::u32, DescriptorType>& bindless)
    {
        const kor::u32 highestSet = sets.empty() ? 0 : std::ranges::max(sets | std::views::keys);
        if (!bindless.empty() && !sets.empty() && highestSet >= BindlessSet)
            throw std::runtime_error(std::format(
                "This pipeline declares the bindless tables at set {} and a set of its own at {}: its own sets "
                "have to stay below kor::BindlessSet.", BindlessSet, highestSet));

        auto* heaps = Context::Heaps();
        if (!heaps) {
            // ---- descriptor sets: a pipeline layout ------------------------------------------------
            std::vector<::vk::DescriptorSetLayout> setLayouts;
            const kor::u32 count = !bindless.empty() ? BindlessSet + 1 : sets.empty() ? 0 : highestSet + 1;
            setLayouts.assign(count, Context::Bindless().EmptyLayout());
            for (const auto& [set, layout] : sets)
                setLayouts[set] = **dynamic_cast<const DescriptorSetLayout*>(layout.Get());
            if (!bindless.empty()) setLayouts[BindlessSet] = Context::Bindless().Layout();

            std::vector<::vk::PushConstantRange> ranges;
            for (const auto& [offset, pushConstant] : pushConstants)
                ranges.push_back(::vk::PushConstantRange()
                    .setStageFlags(getVkShaderStageFlags(pushConstant.stages))
                    .setOffset(offset)
                    .setSize(pushConstant.size));

            _layout = Context::Device()->createPipelineLayout(::vk::PipelineLayoutCreateInfo()
                .setSetLayouts(setLayouts)
                .setPushConstantRanges(ranges));
            return;
        }

        // ---- descriptor heap: a mapping per binding ----------------------------------------------------
        _heap = true;

        // The push constants and the sets' base slots share push data, and must not overlap.
        kor::u32 pushEnd = 0;
        for (const auto& range : pushConstants | std::views::values) pushEnd = std::max(pushEnd, range.offset + range.size);
        if (!sets.empty() && pushEnd > HeapSetPushOffset(highestSet))
            throw std::runtime_error(std::format(
                "Under the descriptor heap, binding a set pushes where its descriptors start: 8 bytes a set, from the "
                "top of the {} bytes of push data down. This pipeline's sets reach set {}, which leaves {} bytes for "
                "push constants, and they take {}. Shrink the push-constant block, or use fewer sets.",
                heaps->PushDataSize(), highestSet, HeapSetPushOffset(highestSet), pushEnd));

        const auto resourceSlot = heaps->SlotSize(HeapRole::eResource);
        const auto samplerSlot = heaps->SlotSize(HeapRole::eSampler);

        for (const auto& [set, layout] : sets) {
            const auto& vkLayout = dynamic_cast<const DescriptorSetLayout&>(*layout);
            const auto push = HeapSetPushOffset(set);
            for (const auto& [binding, slots] : vkLayout.HeapBindings()) {
                const auto& description = layout->Bindings().at(binding);
                VkDescriptorSetAndBindingMappingEXT mapping{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_AND_BINDING_MAPPING_EXT };
                mapping.descriptorSet = set;
                mapping.firstBinding = binding;
                mapping.bindingCount = 1;
                mapping.resourceMask = VK_SPIRV_RESOURCE_TYPE_ALL_EXT;

                const VkSamplerCreateInfo* embedded = nullptr;
                if (description.embeddedSampler.Valid())
                    embedded = &_embedded.emplace_back(static_cast<VkSamplerCreateInfo>(
                        dynamic_cast<const vk::Sampler&>(*description.embeddedSampler).CreateInfo()));

                if (description.type == DescriptorType::eSampler && embedded) {
                    // Nothing in the heap at all: the sampler is the mapping's own.
                    mapping.source = VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_CONSTANT_OFFSET_EXT;
                    mapping.sourceData.constantOffset = {};
                    mapping.sourceData.constantOffset.pEmbeddedSampler = embedded;
                    _mappings.push_back(mapping);
                    continue;
                }

                // A slot index pushed for the set, stepped into bytes by the slot size, plus where in the
                // set's run the binding starts, plus one slot per array element.
                mapping.source = VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_PUSH_INDEX_EXT;
                auto& index = mapping.sourceData.pushIndex;
                index = {};
                if (description.type == DescriptorType::eSampler) {
                    // A sampler on its own reads the sampler heap through the base parameters.
                    index.pushOffset = push + 4;
                    index.heapOffset = slots.sampler * samplerSlot;
                    index.heapIndexStride = samplerSlot;
                    index.heapArrayStride = samplerSlot;
                } else {
                    index.pushOffset = push;
                    index.heapOffset = slots.resource * resourceSlot;
                    index.heapIndexStride = resourceSlot;
                    index.heapArrayStride = resourceSlot;
                    if (description.type == DescriptorType::eCombinedImageSampler) {
                        if (embedded) {
                            index.pEmbeddedSampler = embedded;
                        } else {
                            index.samplerPushOffset = push + 4;
                            index.samplerHeapOffset = slots.sampler * samplerSlot;
                            index.samplerHeapIndexStride = samplerSlot;
                            index.samplerHeapArrayStride = samplerSlot;
                        }
                    }
                }
                _mappings.push_back(mapping);
            }
        }

        // The bindless tables are the heaps themselves, from their first slot: an index is a slot.
        for (const auto& [binding, type] : bindless) {
            VkDescriptorSetAndBindingMappingEXT mapping{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_AND_BINDING_MAPPING_EXT };
            mapping.descriptorSet = BindlessSet;
            mapping.firstBinding = binding;
            mapping.bindingCount = 1;
            mapping.resourceMask = VK_SPIRV_RESOURCE_TYPE_ALL_EXT;
            mapping.source = VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_CONSTANT_OFFSET_EXT;
            mapping.sourceData.constantOffset = {};
            mapping.sourceData.constantOffset.heapArrayStride = type == DescriptorType::eSampler ? samplerSlot : resourceSlot;
            _mappings.push_back(mapping);
        }

        _mappingInfo.mappingCount = static_cast<kor::u32>(_mappings.size());
        _mappingInfo.pMappings = _mappings.data();
    }

    const void* PipelineInterface::CreateNext(const void* next)
    {
        if (!_heap) return next;
        _flags.flags = VK_PIPELINE_CREATE_2_DESCRIPTOR_HEAP_BIT_EXT;
        _flags.pNext = next;
        return &_flags;
    }
}
