//
// Created by radue on 6/23/2026.
//

#include <pipeline.h>

#include <format>
#include <map>
#include <ranges>
#include <unordered_map>

#include <descriptorSetLayout.h>
#include <log.h>

#include "shader.h"

namespace kor
{
    Pipeline::~Pipeline() = default;

    const DescriptorSetLayout& Pipeline::descriptorSetLayout(const glm::u32 index) const
    {
        if (!_setLayouts.contains(index))
            throw std::runtime_error("This pipeline does not contain a set with that index!");
        return *_setLayouts.at(index);
    }

    ResourceRef<const DescriptorSetLayout> Pipeline::descriptorSetLayoutRef(const glm::u32 index) const
    {
        if (!_setLayouts.contains(index))
            throw std::runtime_error("This pipeline does not contain a set with that index!");
        return ResourceRef<const DescriptorSetLayout>(_setLayouts.at(index));
    }

    const Pipeline::PushConstantMember* Pipeline::findPushConstant(const std::string_view name) const
    {
        const auto it = _pushConstants.find(name);
        return it == _pushConstants.end() ? nullptr : &it->second;
    }

    const Shader::PushConstant& Pipeline::pushConstantRange(const glm::u32 offset) const
    {
        // The range *containing* the offset, not the one that starts at it. A push writes some
        // part of a block — one named constant out of several — so it is only the first field of
        // a block that ever lands exactly on a range's first byte.
        for (const auto& range : _pushConstantRanges | std::views::values) {
            if (offset >= range.offset && offset < range.offset + range.size) return range;
        }
        throw std::runtime_error("This pipeline declares no push-constant range covering that offset!");
    }

    VoidResult Pipeline::buildLayouts(const std::span<const ResourceRef<const Shader>> shaders)
    {
        // The first conflict found, kept so the pipeline's failure names what actually went wrong
        // rather than reporting every kind of merge problem as a descriptor conflict. The scan
        // continues past it, so one build logs every conflict there is.
        std::optional<Error> failure;
        const auto conflict = [&failure](const ErrorCode code, std::string message) {
            kor::log::error("{}", message);
            if (!failure) failure = Error{ .code = code, .message = std::move(message) };
        };

        // Merge per-shader memory layouts: descriptors sharing a (set, binding) are
        // unioned across stages, push constants are unioned by offset.
        std::unordered_map<glm::u32, std::map<glm::u32, Shader::Descriptor>> mergedSetLayouts;
        std::unordered_map<glm::u32, Shader::PushConstant> mergedPushConstants;
        // Every block as its own shader declared it. The merge above is keyed by offset and keeps
        // the first declaration it sees, which is exactly the case the name merge below has to
        // examine: two stages declaring different blocks at one offset.
        std::vector<const Shader::PushConstant*> declaredPushConstants;
        for (const auto& shader : shaders)
        {
            const auto& memoryLayout = shader->memoryLayout();
            for (const auto& [setIndex, setDescription] : memoryLayout.descriptorSets)
            {
                for (const auto& [binding, descriptor] : setDescription.descriptors)
                {
                    if (mergedSetLayouts[setIndex].contains(binding)) {
                        auto& existingDescriptor = mergedSetLayouts[setIndex][binding];
                        if (existingDescriptor != descriptor)
                        {
                            conflict(ErrorCode::eDescriptorConflict, std::format(
                                "Descriptor set layout conflict between shaders in pipeline! Set: {}, Binding: {}, Existing: {{ Type: {}, Name: {}, Count: {} }}, New: {{ Type: {}, Name: {}, Count: {} }}",
                                setIndex, binding,
                                static_cast<glm::u32>(existingDescriptor.type), existingDescriptor.name, existingDescriptor.count,
                                static_cast<glm::u32>(descriptor.type), descriptor.name, descriptor.count));
                        }
                        existingDescriptor.stages |= descriptor.stages;
                        // Union the accesses too: a buffer a vertex shader only reads but a
                        // fragment shader writes has to be synchronised as read-write for the
                        // pipeline as a whole. Same for activity — reached by any stage counts.
                        if (existingDescriptor.access != descriptor.access)
                            existingDescriptor.access = Shader::AccessKind::eReadWrite;
                        existingDescriptor.active = existingDescriptor.active || descriptor.active;
                    } else
                    {
                        mergedSetLayouts[setIndex][binding] = descriptor;
                    }
                }
            }
            for (const auto& [offset, pushConstant] : memoryLayout.pushConstants)
            {
                declaredPushConstants.push_back(&pushConstant);
                if (mergedPushConstants.contains(offset)) {
                    mergedPushConstants[offset].stages |= pushConstant.stages;
                } else {
                    mergedPushConstants[offset] = pushConstant;
                }
            }
        }

        _usesDeviceAddresses = false;
        for (const auto& shader : shaders) {
            if (shader.alive() && !shader.poisoned() && shader->usesDeviceAddresses())
                _usesDeviceAddresses = true;
        }

        _pushConstantRanges.clear();
        _pushConstants.clear();

        // Rebuild only the sets whose *interface* actually changed. A shader edit usually changes
        // code, not its bindings, and an unchanged layout must keep its identity: descriptor sets
        // hold a reference to it, and destroying it on every reload is what used to dangle them.
        std::map<glm::u32, Resource<DescriptorSetLayout>> rebuilt;

        for (const auto& [setIndex, setDescription] : mergedSetLayouts)
        {
            auto builder = DescriptorSetLayout::Builder();
            for (const auto& [binding, descriptor] : setDescription)
            {
                builder.addBinding(binding, DescriptorSetLayout::Binding{
                    .type = descriptor.type,
                    .count = descriptor.count,
                    .access = descriptor.access,
                    .stages = descriptor.stages,
                    .active = descriptor.active,
                    .members = descriptor.members,
                    .blockSize = descriptor.blockSize,
                    // Carried through so a set can be written by the name the shader uses rather
                    // than by a number restated in C++. @see DescriptorSet::Builder::write
                    .name = descriptor.name,
                    .blockName = descriptor.blockName,
                    // What an Image bound directly at this binding is turned into a view by.
                    .shape = descriptor.shape,
                });
            }

            if (const auto existing = _setLayouts.find(setIndex);
                existing != _setLayouts.end() && existing->second.valid() &&
                existing->second->matches(builder))
            {
                // The interface is unchanged, so the layout object — and every descriptor set
                // holding it — stays valid. Its *blocks* may still have been reshaped by the edit
                // (a field added to a uniform block changes no binding), and those are what a
                // semantic-filled binding was built from: adopt them, and say so, so the sets
                // built against this layout rebuild themselves against the new shape.
                if (existing->second->refreshBlocks(builder)) existing->second.markChanged();
                rebuilt[setIndex] = std::move(existing->second);
                continue;
            }

            auto layout = builder.build();
            if (!layout.valid()) {
                // materialize() already logged the full history; just fail the pipeline.
                if (!failure) failure = layout.error() ? *layout.error()
                                                       : Error{ .code = ErrorCode::eDescriptorConflict,
                                                                .message = "A descriptor set layout could not be built." };
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

        // The same declarations flattened by name, which is how a command buffer addresses them.
        //
        // Two stages naming one constant is the normal case — a block declared in a shared header
        // and included by both — and they must agree on where it sits, because there is one
        // push-constant range per pipeline and both stages read the same bytes. Disagreeing is a
        // mistake no error message from the driver would explain, so it is caught here: either two
        // blocks were given different layouts, or two different constants were left to collide at
        // the same offset because neither block said where it starts.
        _pushConstants.clear();
        std::map<glm::u32, std::string> claimedBytes;   // first byte of each claim -> owner
        for (const auto* block : declaredPushConstants)
        {
            const auto& pushConstant = *block;
            for (const auto& member : pushConstant.members)
            {
                if (const auto existing = _pushConstants.find(member.name); existing != _pushConstants.end())
                {
                    if (existing->second.offset != member.offset || existing->second.size != member.size)
                    {
                        conflict(ErrorCode::ePushConstantMismatch, std::format(
                            "Push constant '{}' is declared differently by two stages of this pipeline: "
                            "{} bytes at offset {}, and {} bytes at offset {}. There is one push-constant "
                            "range per pipeline, so every stage that names a constant has to place it "
                            "identically — declare the block once in a shared header and include it in both.",
                            member.name, existing->second.size, existing->second.offset,
                            member.size, member.offset));
                        continue;
                    }
                    existing->second.stages |= pushConstant.stages;
                    continue;
                }

                // Only the atomic leaves stake a claim on bytes. A struct, or an array taken as a
                // whole, covers the very bytes its own fields and elements do — letting those claim
                // them would report every nested thing as overlapping itself. Two blocks that
                // really do collide are caught by their leaves, which is where the names differ.
                if (const bool atomic = !member.aggregate && member.count == 1; atomic)
                {
                    bool overlaps = false;
                    for (glm::u32 byte = member.offset; byte < member.offset + member.size; ++byte)
                    {
                        const auto owner = claimedBytes.find(byte);
                        if (owner == claimedBytes.end() || owner->second == member.name) continue;

                        conflict(ErrorCode::ePushConstantMismatch, std::format(
                            "Push constants '{}' and '{}' overlap at byte {}: two stages declare different "
                            "blocks in the same push-constant range. Give one of them an explicit offset "
                            "(layout(push_constant) with offsets past the other), or declare a single "
                            "shared block both stages use.",
                            owner->second, member.name, byte));
                        overlaps = true;
                        break;
                    }
                    if (overlaps) continue;

                    for (glm::u32 byte = member.offset; byte < member.offset + member.size; ++byte)
                        claimedBytes[byte] = member.name;
                }

                _pushConstants.emplace(member.name, PushConstantMember{
                    .offset = member.offset, .size = member.size, .stages = pushConstant.stages,
                    .scalar = member.scalar, .rows = member.rows, .columns = member.columns,
                    .count = member.count, .arrayStride = member.arrayStride,
                    .matrixStride = member.matrixStride, .aggregate = member.aggregate });
            }
        }

        if (failure) return std::unexpected(*failure);
        return {};
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
            kor::log::error("Pipeline validation failed during reload: {}", v.error().toString());
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