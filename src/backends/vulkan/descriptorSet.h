//
// Created by radue on 3/6/2026.
//

#pragma once

#include <descriptorSet.h>
#include <vulkan/vulkan.hpp>

namespace kor::vk
{
    /**
     * Under descriptor sets: one VkDescriptorSet per frame in flight (one, for a set nothing per-frame
     * is in). Under the descriptor heap: one run of slots in each heap per frame in flight, whose first
     * slots are what binding the set pushes to the shader.
     */
    class DescriptorSet final : public kor::DescriptorSet
    {
    public:

        explicit DescriptorSet(const Builder& builder);
        ~DescriptorSet() override;

        void Rebind(kor::u32 binding, const Descriptor &descriptor, kor::u32 index) override;

        void DebugPrint() const override;

        ::vk::DescriptorSet operator*() const;

        /** @brief The set's first slot in each heap, for the frame being recorded. Heap only. */
        struct HeapBase { kor::u32 resource = 0; kor::u32 sampler = 0; };
        [[nodiscard]] HeapBase HeapBaseNow() const;

    private:
        [[nodiscard]] std::size_t frameCount() const;
        void write(kor::u32 binding, kor::u32 index, const Descriptor& descriptor, std::size_t frame);

        std::vector<::vk::DescriptorSet> _descriptorSets;
        std::vector<HeapBase> _heapBases;
        kor::u32 _resourceSlots = 0;
        kor::u32 _samplerSlots = 0;
    };
}
