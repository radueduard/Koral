//
// Created by radue on 3/6/2026.
//

#include "descriptorSet.h"

#include <deque>
#include <format>
#include <iostream>
#include <ranges>

#include "accelerationStructure.h"
#include "buffer.h"
#include "context.h"
#include "descriptor.h"
#include "descriptorPool.h"
#include "descriptorSetLayout.h"
#include "device.h"
#include "bufferView.h"
#include "heaps.h"
#include "imageView.h"
#include "sampler.h"
#include "vulkanContext.h"
#include <descriptorSetLayout.h>
#include <scheduler.h>

namespace kor::vk
{
    namespace
    {
        // Descriptor writes for one VkDescriptorSet, collected and made in one vkUpdateDescriptorSets.
        // Deques, because each write points at its info and the infos must not move as more are added.
        struct SetWrites
        {
            std::vector<::vk::WriteDescriptorSet> writes;
            std::deque<::vk::DescriptorBufferInfo> buffers;
            std::deque<::vk::DescriptorImageInfo> images;
            std::deque<::vk::WriteDescriptorSetAccelerationStructureKHR> structures;
            std::deque<::vk::AccelerationStructureKHR> structureHandles;
            std::deque<::vk::BufferView> texelViews;

            void Add(const ::vk::DescriptorSet set, const kor::u32 binding, const kor::u32 index, const DescriptorType type,
                     const DescriptorSetLayout::Binding& description, const Descriptor& descriptor, const std::size_t frame)
            {
                auto write = ::vk::WriteDescriptorSet()
                    .setDstSet(set)
                    .setDstBinding(binding)
                    .setDstArrayElement(index)
                    .setDescriptorCount(1);
                const bool embedded = description.embeddedSampler.Valid();
                switch (type) {
                case DescriptorType::eUniformBuffer:
                case DescriptorType::eStorageBuffer: {
                    const auto& buffer = dynamic_cast<const vk::Buffer&>(descriptor.BoundBuffer());
                    write.setDescriptorType(type == DescriptorType::eUniformBuffer ? ::vk::DescriptorType::eUniformBuffer
                                                                                   : ::vk::DescriptorType::eStorageBuffer)
                         .setBufferInfo(buffers.emplace_back()
                             .setBuffer(buffer.IsPerFrame() ? buffer[frame] : buffer[0])
                             .setOffset(descriptor.Offset())
                             .setRange(descriptor.Range()));
                    break;
                }
                case DescriptorType::eSampler:
                    // An embedded sampler is the layout's own; there is nothing to write.
                    if (embedded) return;
                    write.setDescriptorType(::vk::DescriptorType::eSampler)
                         .setImageInfo(images.emplace_back().setSampler(*dynamic_cast<const vk::Sampler&>(descriptor.BoundSampler())));
                    break;
                case DescriptorType::eCombinedImageSampler: {
                    const auto& view = dynamic_cast<const vk::ImageView&>(descriptor.BoundImageView());
                    auto& info = images.emplace_back()
                        .setImageView(view.IsPerFrame() ? view[frame] : view[0])
                        .setImageLayout(::vk::ImageLayout::eShaderReadOnlyOptimal);
                    // With an embedded sampler only the image is written; the sampler field is ignored.
                    if (!embedded) info.setSampler(*dynamic_cast<const vk::Sampler&>(descriptor.BoundSampler()));
                    write.setDescriptorType(::vk::DescriptorType::eCombinedImageSampler).setImageInfo(info);
                    break;
                }
                case DescriptorType::eStorageImage:
                case DescriptorType::eSampledImage: {
                    const auto& view = dynamic_cast<const vk::ImageView&>(descriptor.BoundImageView());
                    const bool storage = type == DescriptorType::eStorageImage;
                    write.setDescriptorType(storage ? ::vk::DescriptorType::eStorageImage : ::vk::DescriptorType::eSampledImage)
                         .setImageInfo(images.emplace_back()
                             .setImageView(view.IsPerFrame() ? view[frame] : view[0])
                             .setImageLayout(storage ? ::vk::ImageLayout::eGeneral : ::vk::ImageLayout::eShaderReadOnlyOptimal));
                    break;
                }
                case DescriptorType::eAccelerationStructure: {
                    const auto& structure = dynamic_cast<const vk::AccelerationStructure&>(descriptor.BoundAccelerationStructure());
                    const auto& handle = structureHandles.emplace_back(*structure);
                    write.setDescriptorType(::vk::DescriptorType::eAccelerationStructureKHR)
                         .setPNext(&structures.emplace_back().setAccelerationStructures(handle));
                    break;
                }
                case DescriptorType::eUniformTexelBuffer:
                case DescriptorType::eStorageTexelBuffer: {
                    // A texel buffer is written through neither a buffer info nor an image info but a
                    // VkBufferView handle of its own — the one case where the handle goes straight in.
                    const auto& view = dynamic_cast<const vk::BufferView&>(descriptor.BoundBufferView());
                    write.setDescriptorType(type == DescriptorType::eUniformTexelBuffer
                                                ? ::vk::DescriptorType::eUniformTexelBuffer
                                                : ::vk::DescriptorType::eStorageTexelBuffer)
                         .setTexelBufferView(texelViews.emplace_back(view.IsPerFrame() ? view[frame] : view[0]));
                    break;
                }
                default:
                    throw std::runtime_error(std::format("Unknown descriptor type for binding {} index {}!", binding, index));
                }
                writes.push_back(write);
            }

            void Flush()
            {
                if (!writes.empty()) Context::Device()->updateDescriptorSets(writes, {});
                writes.clear();
                buffers.clear();
                images.clear();
                structures.clear();
                structureHandles.clear();
                texelViews.clear();
            }
        };

        // Whether a slot needs a descriptor at all: an unbounded array is partly bound, and an embedded
        // sampler is the pipeline's. Every other slot left unwritten is the builder's mistake, said the
        // way the accessors say it, under either model.
        bool needsWrite(const DescriptorSetLayout::Binding& description, const Descriptor& descriptor)
        {
            if (descriptor.IsValid()) return true;
            if (description.count == 0) return false;
            if (description.type == DescriptorType::eSampler && description.embeddedSampler.Valid()) return false;
            return true;
        }
    }

    std::size_t DescriptorSet::frameCount() const
    {
        return _isPerFrame ? kor::Context::Scheduler().ImageCount() : 1;
    }

    DescriptorSet::DescriptorSet(const Builder& builder) : kor::DescriptorSet(builder)
    {
        const auto& layout = dynamic_cast<const DescriptorSetLayout&>(*_layout);
        const auto frames = frameCount();

        if (auto* heaps = Context::Heaps()) {
            _resourceSlots = layout.ResourceSlots();
            _samplerSlots = layout.SamplerSlots();
            for (std::size_t frame = 0; frame < frames; ++frame) {
                HeapBase base;
                const auto resources = _resourceSlots ? heaps->Allocate(HeapRole::eResource, _resourceSlots) : std::optional<kor::u32>(0);
                const auto samplers = _samplerSlots ? heaps->Allocate(HeapRole::eSampler, _samplerSlots) : std::optional<kor::u32>(0);
                if (!resources || !samplers) {
                    if (resources && _resourceSlots) heaps->Release(HeapRole::eResource, *resources, _resourceSlots);
                    if (samplers && _samplerSlots) heaps->Release(HeapRole::eSampler, *samplers, _samplerSlots);
                    for (const auto& made : _heapBases) {
                        if (_resourceSlots) heaps->Release(HeapRole::eResource, made.resource, _resourceSlots);
                        if (_samplerSlots) heaps->Release(HeapRole::eSampler, made.sampler, _samplerSlots);
                    }
                    _heapBases.clear();
                    throw std::runtime_error(std::format(
                        "The descriptor heap has no room for this set ({} resource and {} sampler slots wanted; {} of {} "
                        "and {} of {} are in use). Sets that are no longer used give theirs back once destroyed.",
                        _resourceSlots, _samplerSlots, heaps->Used(HeapRole::eResource), heaps->Capacity(HeapRole::eResource),
                        heaps->Used(HeapRole::eSampler), heaps->Capacity(HeapRole::eSampler)));
                }
                base.resource = *resources;
                base.sampler = *samplers;
                _heapBases.push_back(base);
            }
        } else {
            for (std::size_t i = 0; i < frames; ++i)
                _descriptorSets.emplace_back(Context::DescriptorPool().Allocate(layout));
        }

        for (std::size_t frame = 0; frame < frames; ++frame) {
            if (auto* heaps = Context::Heaps()) {
                DescriptorHeaps::Writer writer(*heaps);
                const auto& base = _heapBases[frame];
                for (const auto& [binding, descriptors] : _writes) {
                    const auto& description = _layout->Bindings().at(binding);
                    const auto& slots = layout.HeapBindings().at(binding);
                    for (kor::u32 i = 0; i < descriptors.size(); ++i) {
                        const auto& descriptor = descriptors[i];
                        if (!needsWrite(description, descriptor)) continue;
                        if (i >= slots.count)
                            throw std::runtime_error(std::format("Binding {} index {} is past the binding's {} slots.", binding, i, slots.count));
                        if (slots.resource != ~0u)
                            writer.Resource(base.resource + slots.resource + i, description.type, descriptor, frame);
                        if (slots.sampler != ~0u)
                            writer.Sampler(base.sampler + slots.sampler + i,
                                           dynamic_cast<const vk::Sampler&>(descriptor.BoundSampler()).CreateInfo());
                    }
                }
                continue;
            }

            SetWrites batch;
            for (const auto& [binding, descriptors] : _writes) {
                const auto& description = _layout->Bindings().at(binding);
                for (kor::u32 i = 0; i < descriptors.size(); ++i) {
                    if (!needsWrite(description, descriptors[i])) continue;
                    batch.Add(_descriptorSets[frame], binding, i, description.type, description, descriptors[i], frame);
                }
            }
            batch.Flush();
        }
    }

    DescriptorSet::~DescriptorSet()
    {
        // The frames still in flight may be about to read through these. A set is not only destroyed
        // at teardown: a shader edit that reshapes a block replays the builder, and the *new* set
        // replacing the old one is what destroys it — mid-frame, with the previous frames' command
        // buffers still holding it. Freeing then is a use-after-free the validation layer catches as
        // "can't be called on VkDescriptorSet ... currently in use by VkCommandBuffer". So they are
        // freed once the GPU is done with them (this used to stall the whole device instead).
        if (auto* heaps = Context::Heaps()) {
            for (const auto& base : _heapBases) {
                if (_resourceSlots) heaps->Release(HeapRole::eResource, base.resource, _resourceSlots);
                if (_samplerSlots) heaps->Release(HeapRole::eSampler, base.sampler, _samplerSlots);
            }
            return;
        }
        if (_descriptorSets.empty()) return;
        Context::DestroyWhenUnused([sets = _descriptorSets] {
            for (const auto& set : sets) Context::DescriptorPool().Free(set);
        });
    }

    void DescriptorSet::write(const kor::u32 binding, const kor::u32 index, const Descriptor& descriptor, const std::size_t frame)
    {
        const auto& description = _layout->Bindings().at(binding);
        if (auto* heaps = Context::Heaps()) {
            const auto& slots = dynamic_cast<const DescriptorSetLayout&>(*_layout).HeapBindings().at(binding);
            if (index >= slots.count)
                throw std::runtime_error(std::format("Binding {} index {} is past the binding's {} slots.", binding, index, slots.count));
            DescriptorHeaps::Writer writer(*heaps);
            const auto& base = _heapBases[frame];
            if (slots.resource != ~0u) writer.Resource(base.resource + slots.resource + index, description.type, descriptor, frame);
            if (slots.sampler != ~0u)
                writer.Sampler(base.sampler + slots.sampler + index, dynamic_cast<const vk::Sampler&>(descriptor.BoundSampler()).CreateInfo());
            return;
        }
        SetWrites batch;
        batch.Add(_descriptorSets[frame], binding, index, description.type, description, descriptor, frame);
        batch.Flush();
    }

    void DescriptorSet::Rebind(const kor::u32 binding, const Descriptor &descriptor, const kor::u32 index)
    {
        if (!_layout.Valid() || !_layout->Bindings().contains(binding)) {
            kor::log::Error("Cannot rebind binding {}: this set's layout has no such binding.", binding);
            return;
        }
        for (std::size_t frame = 0; frame < frameCount(); ++frame) write(binding, index, descriptor, frame);
        // Kept as written, so the barrier resolver follows the rebind as it follows a build.
        auto& slots = _writes[binding];
        if (index >= slots.size()) slots.resize(index + 1);
        slots[index] = descriptor;
    }

    void DescriptorSet::DebugPrint() const {
        kor::DescriptorSet::DebugPrint();
        const auto frame = _isPerFrame ? kor::Context::Scheduler().CurrentImageIndex() : 0;
        if (!_heapBases.empty())
            std::cout << "Descriptor heap slots: resources from " << _heapBases[frame].resource
                      << ", samplers from " << _heapBases[frame].sampler << std::endl;
        else
            std::cout << "Current Vulkan descriptor set handle: " << _descriptorSets[frame] << std::endl;
    }

    ::vk::DescriptorSet DescriptorSet::operator*() const
    {
        const auto frameIndex = _isPerFrame ? kor::Context::Scheduler().CurrentImageIndex() : 0;
        return _descriptorSets[frameIndex];
    }

    DescriptorSet::HeapBase DescriptorSet::HeapBaseNow() const
    {
        const auto frameIndex = _isPerFrame ? kor::Context::Scheduler().CurrentImageIndex() : 0;
        return _heapBases.empty() ? HeapBase{} : _heapBases[frameIndex];
    }
}
