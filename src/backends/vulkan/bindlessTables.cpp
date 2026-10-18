//
// The bindless tables. See bindlessTables.h.
//

#include "bindlessTables.h"

#include <algorithm>
#include <format>
#include <ranges>
#include <stdexcept>

#include <descriptor.h>
#include <descriptorHeap.h>
#include <log.h>

#include "accelerationStructure.h"
#include "buffer.h"
#include "device.h"
#include "heaps.h"
#include "imageView.h"
#include "runtime.h"
#include "sampler.h"
#include "vulkanContext.h"

namespace kor::vk
{
    namespace
    {
        // The most indices a table holds under descriptor sets, where each table is an array of its
        // own. Kept modest because the whole array is reserved up front; a device with less room
        // than this gets what it has.
        constexpr kor::u32 WantedImages = 16384;
        constexpr kor::u32 WantedStorageImages = 4096;
        constexpr kor::u32 WantedBuffers = 16384;
        constexpr kor::u32 WantedUniformBuffers = 4096;
        constexpr kor::u32 WantedSamplers = 2048;
        constexpr kor::u32 WantedStructures = 1024;

        constexpr BindlessKind Kinds[] = {
            BindlessKind::eSampledImage, BindlessKind::eStorageImage, BindlessKind::eStorageBuffer,
            BindlessKind::eUniformBuffer, BindlessKind::eSampler, BindlessKind::eAccelerationStructure,
        };

        ::vk::DescriptorType descriptorType(const BindlessKind kind)
        {
            switch (kind) {
            case BindlessKind::eSampledImage:          return ::vk::DescriptorType::eSampledImage;
            case BindlessKind::eStorageImage:          return ::vk::DescriptorType::eStorageImage;
            case BindlessKind::eStorageBuffer:         return ::vk::DescriptorType::eStorageBuffer;
            case BindlessKind::eUniformBuffer:         return ::vk::DescriptorType::eUniformBuffer;
            case BindlessKind::eSampler:               return ::vk::DescriptorType::eSampler;
            case BindlessKind::eAccelerationStructure: return ::vk::DescriptorType::eAccelerationStructureKHR;
            default:                                   return ::vk::DescriptorType::eSampler;
            }
        }

        DescriptorType heapType(const BindlessKind kind)
        {
            switch (kind) {
            case BindlessKind::eSampledImage:          return DescriptorType::eSampledImage;
            case BindlessKind::eStorageImage:          return DescriptorType::eStorageImage;
            case BindlessKind::eStorageBuffer:         return DescriptorType::eStorageBuffer;
            case BindlessKind::eUniformBuffer:         return DescriptorType::eUniformBuffer;
            case BindlessKind::eAccelerationStructure: return DescriptorType::eAccelerationStructure;
            default:                                   return DescriptorType::eSampler;
            }
        }

        HeapRole heapRole(const BindlessKind kind)
        {
            return kind == BindlessKind::eSampler ? HeapRole::eSampler : HeapRole::eResource;
        }
    }

    BindlessTables::BindlessTables()
    {
        const auto& device = Context::Device();
        _emptyLayout = device->createDescriptorSetLayout(::vk::DescriptorSetLayoutCreateInfo());

        if (Context::Heaps()) {
            // Under the heap the tables are the heap itself: nothing to make, and one capacity per heap.
            for (const auto kind : Kinds) {
                if (kind == BindlessKind::eAccelerationStructure && !device.supportsRayTracing()) continue;
                _capacity[kind] = Context::Heaps()->Capacity(heapRole(kind));
            }
            return;
        }

        const auto& support = device.bindlessSupport();
        auto indexing = ::vk::PhysicalDeviceDescriptorIndexingProperties();
        auto properties = ::vk::PhysicalDeviceProperties2();
        properties.pNext = &indexing;
        (*Context::Runtime().getPhysicalDevice()).getProperties2(&properties);

        // Every array counts against the per-stage total of update-after-bind resources, so the wanted
        // sizes are scaled down together if the device's total is smaller than their sum.
        const auto fit = [](const kor::u32 wanted, const kor::u32 setLimit, const kor::u32 stageLimit) {
            return std::min({ wanted, setLimit, stageLimit });
        };
        _capacity[BindlessKind::eSampledImage] = fit(WantedImages, indexing.maxDescriptorSetUpdateAfterBindSampledImages,
                                                     indexing.maxPerStageDescriptorUpdateAfterBindSampledImages);
        _capacity[BindlessKind::eSampler] = fit(WantedSamplers, indexing.maxDescriptorSetUpdateAfterBindSamplers,
                                                indexing.maxPerStageDescriptorUpdateAfterBindSamplers);
        if (support.storageImages)
            _capacity[BindlessKind::eStorageImage] = fit(WantedStorageImages, indexing.maxDescriptorSetUpdateAfterBindStorageImages,
                                                         indexing.maxPerStageDescriptorUpdateAfterBindStorageImages);
        if (support.storageBuffers)
            _capacity[BindlessKind::eStorageBuffer] = fit(WantedBuffers, indexing.maxDescriptorSetUpdateAfterBindStorageBuffers,
                                                          indexing.maxPerStageDescriptorUpdateAfterBindStorageBuffers);
        if (support.uniformBuffers)
            _capacity[BindlessKind::eUniformBuffer] = fit(WantedUniformBuffers, indexing.maxDescriptorSetUpdateAfterBindUniformBuffers,
                                                          indexing.maxPerStageDescriptorUpdateAfterBindUniformBuffers);
        if (support.accelerationStructures)
            _capacity[BindlessKind::eAccelerationStructure] = WantedStructures;

        kor::u64 total = 0;
        for (const auto& count : _capacity | std::views::values) total += count;
        if (total > indexing.maxPerStageUpdateAfterBindResources && total > 0) {
            const double scale = static_cast<double>(indexing.maxPerStageUpdateAfterBindResources) / static_cast<double>(total);
            for (auto& count : _capacity | std::views::values)
                count = std::max<kor::u32>(1, static_cast<kor::u32>(count * scale));
        }

        // One array per kind, each partly bound (most slots are empty), written while bound and while
        // frames reading other slots of it are in flight.
        std::vector<::vk::DescriptorSetLayoutBinding> bindings;
        std::vector<::vk::DescriptorBindingFlags> flags;
        std::vector<::vk::DescriptorPoolSize> sizes;
        auto bindingFlags = ::vk::DescriptorBindingFlagBits::ePartiallyBound | ::vk::DescriptorBindingFlagBits::eUpdateAfterBind;
        if (support.updateUnusedWhilePending) bindingFlags |= ::vk::DescriptorBindingFlagBits::eUpdateUnusedWhilePending;
        for (const auto& [kind, count] : _capacity) {
            bindings.push_back(::vk::DescriptorSetLayoutBinding()
                .setBinding(BindlessBinding(kind))
                .setDescriptorType(descriptorType(kind))
                .setDescriptorCount(count)
                .setStageFlags(::vk::ShaderStageFlagBits::eAll));
            flags.push_back(bindingFlags);
            sizes.push_back(::vk::DescriptorPoolSize().setType(descriptorType(kind)).setDescriptorCount(count));
            _slots[kind] = detail::SlotAllocator(count);
        }
        const auto flagsInfo = ::vk::DescriptorSetLayoutBindingFlagsCreateInfo().setBindingFlags(flags);
        _layout = device->createDescriptorSetLayout(::vk::DescriptorSetLayoutCreateInfo()
            .setFlags(::vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool)
            .setBindings(bindings)
            .setPNext(&flagsInfo));
        _pool = device->createDescriptorPool(::vk::DescriptorPoolCreateInfo()
            .setFlags(::vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind)
            .setMaxSets(1)
            .setPoolSizes(sizes));
        _set = device->allocateDescriptorSets(::vk::DescriptorSetAllocateInfo()
            .setDescriptorPool(_pool)
            .setSetLayouts(_layout)).front();
    }

    BindlessTables::~BindlessTables()
    {
        const auto& device = Context::Device();
        if (_pool) device->destroyDescriptorPool(_pool);
        if (_layout) device->destroyDescriptorSetLayout(_layout);
        if (_emptyLayout) device->destroyDescriptorSetLayout(_emptyLayout);
    }

    bool BindlessTables::Has(const BindlessKind kind) const
    {
        return _capacity.contains(kind);
    }

    kor::u32 BindlessTables::Capacity(const BindlessKind kind) const
    {
        const auto it = _capacity.find(kind);
        return it == _capacity.end() ? 0 : it->second;
    }

    std::size_t BindlessTables::Count() const
    {
        std::lock_guard lock(_mutex);
        return _entries.size();
    }

    std::optional<kor::u32> BindlessTables::allocate(const BindlessKind kind)
    {
        if (auto* heaps = Context::Heaps()) return heaps->Allocate(heapRole(kind), 1);
        const auto it = _slots.find(kind);
        if (it == _slots.end()) return std::nullopt;
        return it->second.Allocate(1);
    }

    void BindlessTables::free(const BindlessKind kind, const kor::u32 index)
    {
        if (auto* heaps = Context::Heaps()) {
            heaps->Release(heapRole(kind), index, 1);
            return;
        }
        // Only once the frames that may read it are done: until then a recorded draw can still pick it.
        Context::DestroyWhenUnused([this, kind, index] {
            std::lock_guard lock(_mutex);
            if (const auto it = _slots.find(kind); it != _slots.end()) it->second.Free(index, 1);
        });
    }

    bool BindlessTables::alive(const Entry& entry)
    {
        if (entry.image.Alive() || entry.image.Poisoned()) return entry.image.Valid();
        return std::visit([]<typename T>(const T& ref) {
            if constexpr (std::is_same_v<T, std::monostate>) return false;
            else return ref.Valid();
        }, entry.resource);
    }

    void BindlessTables::write(const BindlessKind kind, const kor::u32 index, const Entry& entry)
    {
        if (auto* heaps = Context::Heaps()) {
            DescriptorHeaps::Writer writer(*heaps);
            if (const auto* sampler = std::get_if<ResourceRef<const kor::Sampler>>(&entry.resource)) {
                writer.Sampler(index, dynamic_cast<const vk::Sampler&>(**sampler).CreateInfo());
                return;
            }
            Descriptor descriptor;
            if (const auto* view = std::get_if<ResourceRef<const kor::ImageView>>(&entry.resource)) descriptor = Descriptor(*view);
            else if (const auto* buffer = std::get_if<ResourceRef<const kor::Buffer>>(&entry.resource)) descriptor = Descriptor(*buffer);
            else if (const auto* structure = std::get_if<ResourceRef<const kor::AccelerationStructure>>(&entry.resource))
                descriptor = Descriptor(*structure);
            writer.Resource(index, heapType(kind), descriptor, 0);
            return;
        }

        auto write = ::vk::WriteDescriptorSet()
            .setDstSet(_set)
            .setDstBinding(BindlessBinding(kind))
            .setDstArrayElement(index)
            .setDescriptorCount(1)
            .setDescriptorType(descriptorType(kind));
        ::vk::DescriptorImageInfo imageInfo;
        ::vk::DescriptorBufferInfo bufferInfo;
        ::vk::AccelerationStructureKHR structureHandle;
        auto structureInfo = ::vk::WriteDescriptorSetAccelerationStructureKHR();
        switch (kind) {
        case BindlessKind::eSampledImage:
        case BindlessKind::eStorageImage: {
            const auto& view = dynamic_cast<const vk::ImageView&>(*std::get<ResourceRef<const kor::ImageView>>(entry.resource));
            imageInfo.setImageView(*view).setImageLayout(kind == BindlessKind::eStorageImage
                ? ::vk::ImageLayout::eGeneral : ::vk::ImageLayout::eShaderReadOnlyOptimal);
            write.setImageInfo(imageInfo);
            break;
        }
        case BindlessKind::eStorageBuffer:
        case BindlessKind::eUniformBuffer: {
            const auto& buffer = dynamic_cast<const vk::Buffer&>(*std::get<ResourceRef<const kor::Buffer>>(entry.resource));
            bufferInfo.setBuffer(buffer[0]).setOffset(0).setRange(::vk::WholeSize);
            write.setBufferInfo(bufferInfo);
            break;
        }
        case BindlessKind::eSampler:
            imageInfo.setSampler(*dynamic_cast<const vk::Sampler&>(*std::get<ResourceRef<const kor::Sampler>>(entry.resource)));
            write.setImageInfo(imageInfo);
            break;
        case BindlessKind::eAccelerationStructure:
            structureHandle = *dynamic_cast<const vk::AccelerationStructure&>(
                *std::get<ResourceRef<const kor::AccelerationStructure>>(entry.resource));
            structureInfo.setAccelerationStructures(structureHandle);
            write.setPNext(&structureInfo);
            break;
        default:
            return;
        }
        Context::Device()->updateDescriptorSets(write, {});
    }

    DescriptorIndex BindlessTables::acquire(const BindlessKind kind, const void* key, Entry entry)
    {
        std::lock_guard lock(_mutex);
        if (!Has(kind)) {
            log::Error("[bindless] This device has no {} table{}.", BindlessKindName(kind),
                       Context::Heaps() ? "" : " under descriptor sets (it needs update-after-bind for that kind)");
            return {};
        }

        // The same resource asked for again gets the index it already has: a buffer of indices
        // written from two places agrees. Unless that resource is gone and this is a new one at the
        // same address, which the entry's own reference tells apart.
        if (const auto it = _byResource.find({ key, kind }); it != _byResource.end()) {
            const auto& existing = _entries.at({ kind, it->second });
            if (alive(existing)) return { it->second, kind };
            free(kind, it->second);
            _entries.erase({ kind, it->second });
            _byResource.erase(it);
        }

        const auto index = allocate(kind);
        if (!index) {
            log::Error("[bindless] The {} table is full ({} indices): give some back (Bindless::Release, or destroy what "
                       "they were for) before asking for more.", BindlessKindName(kind), Capacity(kind));
            return {};
        }
        entry.key = key;
        if (entry.image.Valid()) entry.generation = entry.image->Generation();
        else if (const auto* view = std::get_if<ResourceRef<const kor::ImageView>>(&entry.resource); view && (*view)->SourceImage().Valid())
            entry.generation = (*view)->SourceImage()->Generation();
        write(kind, *index, entry);
        _entries[{ kind, *index }] = std::move(entry);
        _byResource[{ key, kind }] = *index;
        return { *index, kind };
    }

    DescriptorIndex BindlessTables::Acquire(const BindlessKind kind, const ResourceRef<const kor::ImageView>& view,
                                            const ResourceRef<const kor::Image>& image, const ImageShape shape)
    {
        // Keyed by the image when one was given, so asking twice for the same texture is one index even
        // though the image hands out its view anew after a resize.
        const void* key = image.Valid() ? static_cast<const void*>(image.Get()) : static_cast<const void*>(view.Get());
        return acquire(kind, key, Entry{ .resource = view, .image = image, .shape = shape });
    }

    DescriptorIndex BindlessTables::Acquire(const BindlessKind kind, const ResourceRef<const kor::Buffer>& buffer)
    {
        return acquire(kind, buffer.Get(), Entry{ .resource = buffer });
    }

    DescriptorIndex BindlessTables::Acquire(const ResourceRef<const kor::Sampler>& sampler)
    {
        return acquire(BindlessKind::eSampler, sampler.Get(), Entry{ .resource = sampler });
    }

    DescriptorIndex BindlessTables::Acquire(const ResourceRef<const kor::AccelerationStructure>& structure)
    {
        return acquire(BindlessKind::eAccelerationStructure, structure.Get(), Entry{ .resource = structure });
    }

    void BindlessTables::Release(const DescriptorIndex handle)
    {
        std::lock_guard lock(_mutex);
        const auto it = _entries.find({ handle.kind, handle.index });
        if (it == _entries.end()) return;
        _byResource.erase({ it->second.key, handle.kind });
        _entries.erase(it);
        free(handle.kind, handle.index);
    }

    std::optional<detail::BindlessTarget> BindlessTables::Target(const DescriptorIndex handle) const
    {
        std::lock_guard lock(_mutex);
        const auto it = _entries.find({ handle.kind, handle.index });
        if (it == _entries.end() || !alive(it->second)) return std::nullopt;
        const auto& entry = it->second;
        if (const auto* view = std::get_if<ResourceRef<const kor::ImageView>>(&entry.resource); view && view->Valid())
            return detail::BindlessTarget{ .view = *view };
        if (const auto* buffer = std::get_if<ResourceRef<const kor::Buffer>>(&entry.resource); buffer && buffer->Valid())
            return detail::BindlessTarget{ .buffer = *buffer, .size = (*buffer)->size() };
        return std::nullopt;
    }

    void BindlessTables::Collect()
    {
        std::lock_guard lock(_mutex);
        std::vector<std::pair<BindlessKind, kor::u32>> stale;
        for (auto it = _entries.begin(); it != _entries.end();) {
            auto& [slot, entry] = *it;
            if (!alive(entry)) {
                _byResource.erase({ entry.key, slot.first });
                free(slot.first, slot.second);
                it = _entries.erase(it);
                continue;
            }
            const auto image = entry.image.Valid() ? entry.image
                : std::holds_alternative<ResourceRef<const kor::ImageView>>(entry.resource)
                    ? std::get<ResourceRef<const kor::ImageView>>(entry.resource)->SourceImage() : ResourceRef<const kor::Image>{};
            if (image.Valid() && image->Generation() != entry.generation) stale.push_back(slot);
            ++it;
        }
        if (stale.empty()) return;

        // A resized image is a different image to the device: every index to it has to be rewritten.
        // Frames in flight may be reading the old descriptor this very moment, and the index cannot
        // move (the project may have stored it anywhere), so the rewrite waits for them. Resizes are
        // rare, and a resize has already stalled for its own reasons.
        Context::Device().waitIdle();
        for (const auto& slot : stale) {
            auto& entry = _entries.at(slot);
            if (entry.image.Valid()) {
                const auto view = entry.image->View(entry.shape);
                if (!view.Valid()) {
                    log::Error("[bindless] A resized image can no longer be viewed as its index asked; the index is given back.");
                    continue;
                }
                entry.resource = view;
                entry.generation = entry.image->Generation();
            } else {
                entry.generation = std::get<ResourceRef<const kor::ImageView>>(entry.resource)->SourceImage()->Generation();
            }
            write(slot.first, slot.second, entry);
        }
    }
}
