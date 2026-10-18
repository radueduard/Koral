//
// The context's descriptor heaps. See heaps.h.
//

// Every entry point here is an extension's, resolved at run time: the loader exports none of them.
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1

#include "heaps.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <stdexcept>

#include <log.h>

#include "accelerationStructure.h"
#include "allocator.h"
#include "buffer.h"
#include "bufferView.h"
#include "device.h"
#include "imageView.h"
#include "runtime.h"
#include "sampler.h"
#include "vk_enum_conversions.h"
#include "vulkanContext.h"

namespace kor::vk
{
    namespace
    {
        constexpr ::vk::DeviceSize alignUp(const ::vk::DeviceSize value, const ::vk::DeviceSize alignment)
        {
            return alignment <= 1 ? value : (value + alignment - 1) / alignment * alignment;
        }

        // How many slots a heap is made with, when the device allows it. Each is cheap — a few dozen
        // bytes — so the resource heap is made generous rather than grown: growing a heap means
        // rebinding it, which is the one thing it is expensive to do.
        constexpr ::vk::DeviceSize WantedResourceSlots = 1u << 18;
        constexpr ::vk::DeviceSize WantedSamplerSlots = 4096;

        VkDescriptorType vkType(const DescriptorType type)
        {
            // A combined image sampler is its image here; the sampler is written into the other heap.
            if (type == DescriptorType::eCombinedImageSampler) return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            return static_cast<VkDescriptorType>(getVkDescriptorType(type));
        }
    }

    DescriptorHeaps::DescriptorHeaps()
    {
        const auto physicalDevice = *Context::Runtime().getPhysicalDevice();

        auto heapProperties = ::vk::PhysicalDeviceDescriptorHeapPropertiesEXT();
        auto properties = ::vk::PhysicalDeviceProperties2();
        properties.pNext = &heapProperties;
        physicalDevice.getProperties2(&properties);
        _pushDataSize = static_cast<kor::u32>(heapProperties.maxPushDataSize);

        // A slot holds any one descriptor of its heap, so it is as large as the largest — which is what
        // lets one index space serve every kind of resource, and a bindless index be a slot number.
        std::vector<DescriptorType> types = {
            DescriptorType::eUniformBuffer, DescriptorType::eStorageBuffer,
            DescriptorType::eSampledImage, DescriptorType::eStorageImage,
            DescriptorType::eUniformTexelBuffer, DescriptorType::eStorageTexelBuffer,
        };
        if (Context::Device().supportsRayTracing()) types.push_back(DescriptorType::eAccelerationStructure);
        ::vk::DeviceSize resourceSlot = 0;
        for (const auto type : types) {
            const auto size = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetPhysicalDeviceDescriptorSizeEXT(
                static_cast<VkPhysicalDevice>(physicalDevice), static_cast<VkDescriptorType>(getVkDescriptorType(type)));
            _descriptorSizes.emplace_back(type, static_cast<kor::u32>(size));
            resourceSlot = std::max(resourceSlot, size);
        }
        resourceSlot = alignUp(resourceSlot, std::max(heapProperties.imageDescriptorAlignment, heapProperties.bufferDescriptorAlignment));
        const auto samplerSlot = alignUp(heapProperties.samplerDescriptorSize, heapProperties.samplerDescriptorAlignment);
        _descriptorSizes.emplace_back(DescriptorType::eSampler, static_cast<kor::u32>(heapProperties.samplerDescriptorSize));

        create(_resource, resourceSlot, WantedResourceSlots, heapProperties.maxResourceHeapSize,
               heapProperties.minResourceHeapReservedRange, heapProperties.resourceHeapAlignment, "resource");
        // Reserved for embedded samplers too: a pipeline may embed one (PipelineSettings::SetSampler),
        // and the larger reservation is what keeps that legal without a second, different heap.
        create(_sampler, samplerSlot, WantedSamplerSlots, heapProperties.maxSamplerHeapSize,
               heapProperties.minSamplerHeapReservedRangeWithEmbedded, heapProperties.samplerHeapAlignment, "sampler");

        log::Info("[descriptors] descriptor heap: {} resource slots of {} bytes, {} sampler slots of {} bytes, {} bytes of push data",
                  _resource.capacity, _resource.slotSize, _sampler.capacity, _sampler.slotSize, _pushDataSize);
    }

    DescriptorHeaps::~DescriptorHeaps()
    {
        destroy(_resource);
        destroy(_sampler);
    }

    void DescriptorHeaps::create(Heap& heap, const ::vk::DeviceSize slotSize, const ::vk::DeviceSize wanted,
                                 const ::vk::DeviceSize maxSize, const ::vk::DeviceSize reserved,
                                 const ::vk::DeviceSize alignment, const char* name)
    {
        // The reserved range sits after the slots, at an offset kept aligned so whatever the driver
        // keeps there starts where it would in a heap of its own.
        const auto reservedAlignment = std::max<::vk::DeviceSize>(alignment, 256);
        ::vk::DeviceSize slots = wanted;
        if (maxSize > 0) {
            const auto room = maxSize > reserved + reservedAlignment ? maxSize - reserved - reservedAlignment : 0;
            slots = std::min(slots, room / slotSize);
        }
        if (slots == 0)
            throw std::runtime_error(std::format("The {} descriptor heap has no room for a single descriptor.", name));

        heap.slotSize = static_cast<kor::u32>(slotSize);
        heap.capacity = static_cast<kor::u32>(slots);
        heap.slots = detail::SlotAllocator(heap.capacity);
        heap.reservedOffset = alignUp(slots * slotSize, reservedAlignment);
        heap.reservedSize = reserved;
        heap.size = heap.reservedOffset + heap.reservedSize;

        // Host-written (the CPU encodes every descriptor straight into place) and read by every shader
        // invocation that reaches a resource, so it goes where the CPU can write the device's memory if
        // there is such a place (resizable BAR), and to cached-on-the-bus host memory otherwise. Over-
        // allocated by one alignment, so the heap can start at an aligned address whatever VMA returns.
        const auto info = ::vk::BufferCreateInfo()
            .setSize(heap.size + alignment)
            .setUsage(::vk::BufferUsageFlagBits::eShaderDeviceAddress | ::vk::BufferUsageFlagBits::eTransferDst
                      | static_cast<::vk::BufferUsageFlagBits>(VK_BUFFER_USAGE_DESCRIPTOR_HEAP_BIT_EXT))
            .setSharingMode(::vk::SharingMode::eExclusive);
        const auto [buffer, allocation] = Context::Allocator().AllocateBuffer(
            info, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT);
        heap.buffer = buffer;
        heap.allocation = allocation;

        const auto base = Context::Device()->getBufferAddress(::vk::BufferDeviceAddressInfo().setBuffer(buffer));
        heap.address = alignUp(base, alignment);
        heap.bufferOffset = heap.address - base;
        heap.host = static_cast<std::byte*>(Context::Allocator().MapMemory(allocation)) + heap.bufferOffset;
    }

    void DescriptorHeaps::destroy(Heap& heap)
    {
        if (!heap.allocation) return;
        Context::Allocator().UnmapMemory(heap.allocation);
        Context::Allocator().FreeBuffer(heap.buffer, heap.allocation);
        heap.allocation = nullptr;
        heap.buffer = nullptr;
        heap.host = nullptr;
    }

    kor::u32 DescriptorHeaps::Used(const HeapRole role) const
    {
        std::lock_guard lock(_mutex);
        return heap(role).slots.Used();
    }

    std::optional<kor::u32> DescriptorHeaps::Allocate(const HeapRole role, const kor::u32 count)
    {
        std::lock_guard lock(_mutex);
        return heap(role).slots.Allocate(count);
    }

    void DescriptorHeaps::Release(const HeapRole role, const kor::u32 first, const kor::u32 count)
    {
        if (count == 0) return;
        Context::DestroyWhenUnused([this, role, first, count] {
            std::lock_guard lock(_mutex);
            if (!heap(role).slots.Free(first, count))
                log::Error("[descriptors] {} heap slots {}..{} were given back twice.", HeapRoleName(role), first, first + count);
        });
    }

    void DescriptorHeaps::Bind(const ::vk::CommandBuffer commandBuffer) const
    {
        const auto bind = [](const Heap& heap) {
            return ::vk::BindHeapInfoEXT()
                .setHeapRange(::vk::DeviceAddressRangeEXT().setAddress(heap.address).setSize(heap.size))
                .setReservedRangeOffset(heap.reservedOffset)
                .setReservedRangeSize(heap.reservedSize);
        };
        commandBuffer.bindResourceHeapEXT(bind(_resource));
        commandBuffer.bindSamplerHeapEXT(bind(_sampler));
    }

    kor::u32 DescriptorHeaps::DescriptorSize(const DescriptorType type) const
    {
        const auto wanted = type == DescriptorType::eCombinedImageSampler ? DescriptorType::eSampledImage : type;
        for (const auto& [t, size] : _descriptorSizes)
            if (t == wanted) return size;
        return _resource.slotSize;
    }

    // ---- writer -----------------------------------------------------------------------------------

    void DescriptorHeaps::Writer::Resource(const kor::u32 slot, const DescriptorType type, const Descriptor& descriptor,
                                           const std::size_t copy)
    {
        if (slot >= _heaps._resource.capacity) {
            log::Error("[descriptors] resource heap slot {} is past the heap's {}.", slot, _heaps._resource.capacity);
            return;
        }
        ResourceTo(_heaps._resource.host + static_cast<std::size_t>(slot) * _heaps._resource.slotSize, type, descriptor, copy);
    }

    void DescriptorHeaps::Writer::ResourceTo(void* destination, const DescriptorType type, const Descriptor& descriptor,
                                             const std::size_t copy)
    {
        if (!descriptor.IsValid()) return;
        VkResourceDescriptorInfoEXT info{ VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT };
        info.type = vkType(type);

        switch (type) {
        case DescriptorType::eUniformBuffer:
        case DescriptorType::eStorageBuffer: {
            const auto& buffer = dynamic_cast<const vk::Buffer&>(descriptor.BoundBuffer());
            const auto handle = buffer.IsPerFrame() ? buffer[copy] : buffer[0];
            const auto address = Context::Device()->getBufferAddress(::vk::BufferDeviceAddressInfo().setBuffer(handle));
            auto& range = _ranges.emplace_back();
            range.address = address + static_cast<VkDeviceSize>(descriptor.Offset());
            range.size = static_cast<VkDeviceSize>(descriptor.Range());
            info.data.pAddressRange = &range;
            break;
        }
        case DescriptorType::eUniformTexelBuffer:
        case DescriptorType::eStorageTexelBuffer: {
            const auto& view = dynamic_cast<const vk::BufferView&>(descriptor.BoundBufferView());
            const auto& buffer = dynamic_cast<const vk::Buffer&>(*view.SourceBuffer());
            const auto handle = buffer.IsPerFrame() ? buffer[copy] : buffer[0];
            const auto address = Context::Device()->getBufferAddress(::vk::BufferDeviceAddressInfo().setBuffer(handle));
            auto& texel = _texels.emplace_back();
            texel.sType = VK_STRUCTURE_TYPE_TEXEL_BUFFER_DESCRIPTOR_INFO_EXT;
            texel.format = static_cast<VkFormat>(getVkFormat(view.PixelFormat()));
            texel.addressRange.address = address + static_cast<VkDeviceSize>(view.Offset());
            texel.addressRange.size = static_cast<VkDeviceSize>(view.Range());
            info.data.pTexelBuffer = &texel;
            break;
        }
        case DescriptorType::eSampledImage:
        case DescriptorType::eCombinedImageSampler:
        case DescriptorType::eStorageImage: {
            const auto& view = dynamic_cast<const vk::ImageView&>(descriptor.BoundImageView());
            auto& create = _views.emplace_back(view.CreateInfo(view.IsPerFrame() ? copy : 0));
            auto& image = _images.emplace_back();
            image.sType = VK_STRUCTURE_TYPE_IMAGE_DESCRIPTOR_INFO_EXT;
            image.pView = reinterpret_cast<const VkImageViewCreateInfo*>(&create);
            image.layout = type == DescriptorType::eStorageImage ? VK_IMAGE_LAYOUT_GENERAL
                                                                 : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            info.data.pImage = &image;
            break;
        }
        case DescriptorType::eAccelerationStructure: {
            const auto& structure = dynamic_cast<const vk::AccelerationStructure&>(descriptor.BoundAccelerationStructure());
            auto& range = _ranges.emplace_back();
            range.address = structure.deviceAddress();
            range.size = 0;
            info.data.pAddressRange = &range;
            break;
        }
        case DescriptorType::eSampler:
            SamplerTo(destination, dynamic_cast<const vk::Sampler&>(descriptor.BoundSampler()).CreateInfo());
            return;
        default:
            log::Error("[descriptors] a descriptor of this kind cannot be written into a descriptor heap.");
            return;
        }
        _resources.push_back(info);
        _resourceTargets.push_back(VkHostAddressRangeEXT{ destination, _heaps._resource.slotSize });
    }

    void DescriptorHeaps::Writer::Sampler(const kor::u32 slot, const ::vk::SamplerCreateInfo& info)
    {
        if (slot >= _heaps._sampler.capacity) {
            log::Error("[descriptors] sampler heap slot {} is past the heap's {}.", slot, _heaps._sampler.capacity);
            return;
        }
        SamplerTo(_heaps._sampler.host + static_cast<std::size_t>(slot) * _heaps._sampler.slotSize, info);
    }

    void DescriptorHeaps::Writer::SamplerTo(void* destination, const ::vk::SamplerCreateInfo& info)
    {
        _samplers.push_back(static_cast<VkSamplerCreateInfo>(info));
        _samplerTargets.push_back(VkHostAddressRangeEXT{ destination, _heaps._sampler.slotSize });
    }

    void DescriptorHeaps::Writer::Flush()
    {
        const auto device = static_cast<VkDevice>(*Context::Device());
        if (!_resources.empty()) {
            if (const auto result = VULKAN_HPP_DEFAULT_DISPATCHER.vkWriteResourceDescriptorsEXT(
                    device, static_cast<kor::u32>(_resources.size()), _resources.data(), _resourceTargets.data());
                result != VK_SUCCESS)
                log::Error("[descriptors] writing {} descriptors into the resource heap failed: {}",
                           _resources.size(), ::vk::to_string(static_cast<::vk::Result>(result)));
        }
        if (!_samplers.empty()) {
            if (const auto result = VULKAN_HPP_DEFAULT_DISPATCHER.vkWriteSamplerDescriptorsEXT(
                    device, static_cast<kor::u32>(_samplers.size()), _samplers.data(), _samplerTargets.data());
                result != VK_SUCCESS)
                log::Error("[descriptors] writing {} samplers into the sampler heap failed: {}",
                           _samplers.size(), ::vk::to_string(static_cast<::vk::Result>(result)));
        }
        _views.clear();
        _images.clear();
        _ranges.clear();
        _texels.clear();
        _resources.clear();
        _resourceTargets.clear();
        _samplers.clear();
        _samplerTargets.clear();
    }
}
