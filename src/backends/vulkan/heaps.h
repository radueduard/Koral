//
// The context's two descriptor heaps (VK_EXT_descriptor_heap): one for resources, one for samplers.
//
// Made once, with the device, when it is made for DescriptorModel::eHeap; bound once at the start of
// every command buffer; never rebound, because rebinding a heap can cost a full pipeline drain on
// some hardware. Everything that holds descriptors in heap mode is a run of slots in one of these:
//
//   - a DescriptorSet: as many consecutive slots as its layout holds (per frame in flight, for a
//     per-frame set), whose first slot is pushed to the shader when the set is bound;
//   - a bindless index: one slot, which *is* the index the shader reads with, because the bindless
//     tables are mapped onto the whole heap;
//   - a project's own kor::DescriptorHeap: the run it asked for.
//
// Slots are freed only once the frames submitted before the free have finished (DestroyWhenUnused),
// so a slot a recorded draw still reads is never handed out again underneath it.
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <span>
#include <vector>

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>

#include <descriptor.h>
#include <descriptorHeap.h>
#include <structs.h>

#include "../../core/slotAllocator.h"

namespace kor::vk
{
    class DescriptorHeaps
    {
    public:
        DescriptorHeaps();
        ~DescriptorHeaps();

        DescriptorHeaps(const DescriptorHeaps&) = delete;
        DescriptorHeaps& operator=(const DescriptorHeaps&) = delete;

        /** @brief Bytes between two slots of a heap: every slot fits the largest descriptor that heap holds. */
        [[nodiscard]] kor::u32 SlotSize(HeapRole role) const { return heap(role).slotSize; }
        /** @brief How many slots a heap has for descriptors (its reserved range excluded). */
        [[nodiscard]] kor::u32 Capacity(HeapRole role) const { return heap(role).capacity; }
        /** @brief How many slots are handed out. */
        [[nodiscard]] kor::u32 Used(HeapRole role) const;

        /** @brief A run of @p count slots, or nullopt when the heap has no run that long free. */
        [[nodiscard]] std::optional<kor::u32> Allocate(HeapRole role, kor::u32 count);
        /** @brief Gives a run back, once every frame submitted until now has finished. */
        void Release(HeapRole role, kor::u32 first, kor::u32 count);

        /** @brief Binds both heaps. The first thing every command buffer records. */
        void Bind(::vk::CommandBuffer commandBuffer) const;

        /** @brief The heap's first slot as the device sees it. */
        [[nodiscard]] ::vk::DeviceAddress Address(HeapRole role) const { return heap(role).address; }
        /** @brief The buffer the heap lives in, and where in it slot 0 is: for a copy into the heap. */
        [[nodiscard]] ::vk::Buffer Buffer(HeapRole role) const { return heap(role).buffer; }
        [[nodiscard]] ::vk::DeviceSize BufferOffset(HeapRole role) const { return heap(role).bufferOffset; }

        /** @brief How many bytes of push data the device has: the sets' base slots are pushed at its top. */
        [[nodiscard]] kor::u32 PushDataSize() const { return _pushDataSize; }
        /** @brief How large the bytes of one descriptor of @p type are, before rounding to a slot. */
        [[nodiscard]] kor::u32 DescriptorSize(DescriptorType type) const;

        /**
         * @brief Collects descriptor writes and makes them in as few driver calls as there are heaps.
         *
         * vkWrite*DescriptorsEXT is a CPU encode the driver cannot hide behind anything, so a set's
         * dozen descriptors go through one call, not twelve. Flushed on destruction.
         */
        class Writer
        {
        public:
            explicit Writer(DescriptorHeaps& heaps) : _heaps(heaps) {}
            ~Writer() { Flush(); }
            Writer(const Writer&) = delete;
            Writer& operator=(const Writer&) = delete;

            /**
             * @brief Writes @p descriptor into resource slot @p slot as a @p type descriptor.
             * @param copy Which copy of a per-frame resource; 0 for one that is not per-frame.
             *
             * A combined image sampler writes only its image here, as a sampled image: its sampler
             * goes in the sampler heap, through Sampler().
             */
            void Resource(kor::u32 slot, DescriptorType type, const Descriptor& descriptor, std::size_t copy);
            /** @brief The same, into memory of the caller's rather than a slot of the heap. */
            void ResourceTo(void* destination, DescriptorType type, const Descriptor& descriptor, std::size_t copy);
            /** @brief Writes a sampler into sampler slot @p slot. */
            void Sampler(kor::u32 slot, const ::vk::SamplerCreateInfo& info);
            /** @brief The same, into memory of the caller's. */
            void SamplerTo(void* destination, const ::vk::SamplerCreateInfo& info);

            /** @brief Makes the writes collected so far. */
            void Flush();

        private:
            DescriptorHeaps& _heaps;
            // Deques: what the write infos point at must not move as more are added.
            std::deque<::vk::ImageViewCreateInfo> _views;
            std::deque<VkImageDescriptorInfoEXT> _images;
            std::deque<VkDeviceAddressRangeEXT> _ranges;
            std::deque<VkTexelBufferDescriptorInfoEXT> _texels;
            std::vector<VkResourceDescriptorInfoEXT> _resources;
            std::vector<VkHostAddressRangeEXT> _resourceTargets;
            std::vector<VkSamplerCreateInfo> _samplers;
            std::vector<VkHostAddressRangeEXT> _samplerTargets;
        };

    private:
        struct Heap
        {
            ::vk::Buffer buffer;
            VmaAllocation allocation = nullptr;
            std::byte* host = nullptr;          ///< Slot 0, as the CPU writes it.
            ::vk::DeviceAddress address = 0;    ///< Slot 0, as the device reads it. Aligned as the heap must be.
            ::vk::DeviceSize bufferOffset = 0;  ///< Slot 0's offset into the buffer.
            ::vk::DeviceSize size = 0;          ///< The bound range: the slots, then the reserved range.
            ::vk::DeviceSize reservedOffset = 0;
            ::vk::DeviceSize reservedSize = 0;
            kor::u32 slotSize = 0;
            kor::u32 capacity = 0;
            detail::SlotAllocator slots;
        };

        [[nodiscard]] const Heap& heap(const HeapRole role) const { return role == HeapRole::eSampler ? _sampler : _resource; }
        [[nodiscard]] Heap& heap(const HeapRole role) { return role == HeapRole::eSampler ? _sampler : _resource; }

        void create(Heap& heap, ::vk::DeviceSize slotSize, ::vk::DeviceSize wanted, ::vk::DeviceSize maxSize,
                    ::vk::DeviceSize reserved, ::vk::DeviceSize alignment, const char* name);
        void destroy(Heap& heap);

        Heap _resource;
        Heap _sampler;
        kor::u32 _pushDataSize = 0;
        std::vector<std::pair<DescriptorType, kor::u32>> _descriptorSizes;
        mutable std::mutex _mutex;  ///< Over both allocators; descriptor sets are built on any thread.
    };
}
