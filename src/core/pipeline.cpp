//
// Created by radue on 6/23/2026.
//

#include <pipeline.h>

#include <map>
#include <unordered_map>

#include <descriptorSetLayout.h>
#include <log.h>

#include "shader.h"

namespace gfx
{
    Pipeline::~Pipeline() = default;

    const DescriptorSetLayout& Pipeline::getSetLayout(const glm::u32 index) const
    {
        if (!_setLayouts.contains(index))
            throw std::runtime_error("This pipeline does not contain a set with that index!");
        return *_setLayouts.at(index);
    }

    ResourceRef<const DescriptorSetLayout> Pipeline::getSetLayoutRef(const glm::u32 index) const
    {
        if (!_setLayouts.contains(index))
            throw std::runtime_error("This pipeline does not contain a set with that index!");
        return ResourceRef<const DescriptorSetLayout>(_setLayouts.at(index));
    }

    const Shader::PushConstant& Pipeline::getPushConstantRange(const glm::u32 offset) const
    {
        if (!_pushConstantRanges.contains(offset))
            throw std::runtime_error("This pipeline does not contain a push constant with that offset!");
        return _pushConstantRanges.at(offset);
    }

    bool Pipeline::buildLayouts(const std::span<const ResourceRef<const Shader>> shaders)
    {
        bool valid = true;

        // Merge per-shader memory layouts: descriptors sharing a (set, binding) are
        // unioned across stages, push constants are unioned by offset.
        std::unordered_map<glm::u32, std::map<glm::u32, Shader::Descriptor>> mergedSetLayouts;
        std::unordered_map<glm::u32, Shader::PushConstant> mergedPushConstants;
        for (const auto& shader : shaders)
        {
            const auto& memoryLayout = shader->getMemoryLayout();
            for (const auto& [setIndex, setDescription] : memoryLayout.descriptorSets)
            {
                for (const auto& [binding, descriptor] : setDescription.descriptors)
                {
                    if (mergedSetLayouts[setIndex].contains(binding)) {
                        auto& existingDescriptor = mergedSetLayouts[setIndex][binding];
                        if (existingDescriptor != descriptor)
                        {
                            gfx::log::error("Descriptor set layout conflict between shaders in pipeline! Set: {}, Binding: {}, Existing: {{ Type: {}, Name: {}, Count: {} }}, New: {{ Type: {}, Name: {}, Count: {} }}",
                                setIndex, binding,
                                static_cast<glm::u32>(existingDescriptor.type), existingDescriptor.name, existingDescriptor.count,
                                static_cast<glm::u32>(descriptor.type), descriptor.name, descriptor.count);
                            valid = false;
                        }
                        existingDescriptor.stages |= descriptor.stages;
                    } else
                    {
                        mergedSetLayouts[setIndex][binding] = descriptor;
                    }
                }
            }
            for (const auto& [offset, pushConstant] : memoryLayout.pushConstants)
            {
                if (mergedPushConstants.contains(offset)) {
                    mergedPushConstants[offset].stages |= pushConstant.stages;
                } else {
                    mergedPushConstants[offset] = pushConstant;
                }
            }
        }

        _pushConstantRanges.clear();

        // Rebuild only the sets whose *interface* actually changed. A shader edit usually changes
        // code, not its bindings, and an unchanged layout must keep its identity: descriptor sets
        // hold a reference to it, and destroying it on every reload is what used to dangle them.
        std::map<glm::u32, Resource<DescriptorSetLayout>> rebuilt;

        for (const auto& [setIndex, setDescription] : mergedSetLayouts)
        {
            auto builder = DescriptorSetLayout::Builder();
            for (const auto& [binding, descriptor] : setDescription)
            {
                const auto& [type, name, count, _] = descriptor;
                builder.addBinding(binding, type, count);
            }

            if (const auto existing = _setLayouts.find(setIndex);
                existing != _setLayouts.end() && existing->second.valid() &&
                existing->second->matches(builder))
            {
                rebuilt[setIndex] = std::move(existing->second);  // unchanged: keep it alive
                continue;
            }

            auto layout = builder.build();
            if (!layout.valid()) {
                // materialize() already logged the full history; just fail the pipeline.
                valid = false;
                continue;
            }
            rebuilt[setIndex] = std::move(layout);
        }

        // Sets that survived unchanged were moved across; the rest (removed, or genuinely
        // reshaped) are dropped here, and any descriptor set still referencing one of those
        // now sees an expired ref rather than freed memory.
        _setLayouts = std::move(rebuilt);

        for (const auto& [offset, pushConstant] : mergedPushConstants)
        {
            _pushConstantRanges[offset] = pushConstant;
        }

        return valid;
    }

    void Pipeline::subscribeReload(const ResourceRef<const Shader>& shader)
    {
        const glm::u64 id = const_cast<Shader&>(*shader).RegisterReloadCallback([this] { _shouldReload = true; });
        _shaderReloadCallbackIds[&*shader] = id;
    }

    void Pipeline::unsubscribeReload(const ResourceRef<const Shader>& shader)
    {
        if (!shader) return; // shader already destroyed; its callbacks are gone with it
        if (const auto it = _shaderReloadCallbackIds.find(&*shader); it != _shaderReloadCallbackIds.end()) {
            const_cast<Shader&>(*shader).UnregisterReloadCallback(it->second);
        }
    }

    void Pipeline::Reload()
    {
        if (!_shouldReload) return;
        _shouldReload = false;
        if (auto v = Validate(); !v) {
            gfx::log::error("Pipeline validation failed during reload: {}", v.error().toString());
            return;
        }
        Teardown();
        Setup();
    }

    void Pipeline::automaticUpdate()
    {
        Reload();
    }
}