//
// How a pipeline's shaders reach their descriptors, under whichever descriptor model the device has.
//
// Under descriptor sets: a VkPipelineLayout of the pipeline's set layouts — plus, when a shader
// declares the bindless tables, empty layouts up to kor::BindlessSet and the tables' own layout there —
// and its push-constant ranges.
//
// Under the descriptor heap: no layout at all. Each stage gets a set-and-binding mapping instead, which
// sends every binding the shader declares into the heap: a set's bindings to its run of slots, whose
// first slot is read from push data where binding the set put it; the bindless tables to the whole heap.
// The shader is the same SPIR-V either way.
//
// Push data under the heap: the shader's own push constants keep their offsets, and the sets' base
// slots are pushed from the top down — 8 bytes a set, the resource heap's slot then the sampler heap's —
// so a pipeline using sets 0 and 1 gives up the last 16 bytes of push data and nothing else.
//

#pragma once

#include <deque>
#include <map>
#include <vector>

#include <vulkan/vulkan.hpp>

#include <descriptorSetLayout.h>
#include <pipeline.h>
#include <shader.h>

namespace kor::vk
{
    /** @brief Where binding a set at @p set pushes its two base slots, under the heap. */
    [[nodiscard]] kor::u32 HeapSetPushOffset(kor::u32 set);

    class PipelineInterface
    {
    public:
        /**
         * @param sets The pipeline's own set layouts.
         * @param pushConstants Its push-constant ranges, by offset.
         * @param bindless The bindless tables its shaders declare (Pipeline::BindlessBindings).
         * @throws std::runtime_error when the heap's push data has no room for both the push constants
         *         and the sets' base slots: said with the numbers, so the fix (a smaller block, or fewer
         *         sets) is plain.
         */
        PipelineInterface(const std::map<kor::u32, Resource<kor::DescriptorSetLayout>>& sets,
                          const std::map<kor::u32, kor::Shader::PushConstant>& pushConstants,
                          const std::map<kor::u32, DescriptorType>& bindless);

        PipelineInterface(const PipelineInterface&) = delete;
        PipelineInterface& operator=(const PipelineInterface&) = delete;

        /** @brief The pipeline layout to create the pipeline with, and to destroy with it. Null under the heap. */
        [[nodiscard]] ::vk::PipelineLayout TakeLayout() { return std::exchange(_layout, nullptr); }

        /** @brief What each stage's create info chains in: the binding mapping under the heap, null under sets. */
        [[nodiscard]] const void* StageNext() const { return _mappings.empty() && !_heap ? nullptr : &_mappingInfo; }

        /**
         * @brief Chains what the pipeline's create info needs in front of @p next: the heap pipeline flag
         *        under the heap; @p next unchanged under sets.
         */
        [[nodiscard]] const void* CreateNext(const void* next);

    private:
        bool _heap = false;
        ::vk::PipelineLayout _layout;
        std::vector<VkDescriptorSetAndBindingMappingEXT> _mappings;
        std::deque<VkSamplerCreateInfo> _embedded;   ///< What the mappings' embedded-sampler pointers point at.
        VkShaderDescriptorSetAndBindingMappingInfoEXT _mappingInfo{ VK_STRUCTURE_TYPE_SHADER_DESCRIPTOR_SET_AND_BINDING_MAPPING_INFO_EXT };
        VkPipelineCreateFlags2CreateInfo _flags{ VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO };
    };
}
