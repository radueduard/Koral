//
// Created by radue on 3/6/2026.
//

#pragma once

#include <descriptorSetLayout.h>

#include "vk_wrapper.h"
#include <vulkan/vulkan.hpp>

namespace kor::vk
{
    /**
     * Under descriptor sets: a VkDescriptorSetLayout. Under the descriptor heap there is no such object,
     * only the arithmetic a set built against it needs — how many heap slots it takes, and where in
     * them each binding starts — which a pipeline's shader mapping reproduces from the same layout.
     */
    class DescriptorSetLayout final : public kor::DescriptorSetLayout, public Wrapper<::vk::DescriptorSetLayout>
    {
    public:
        explicit DescriptorSetLayout(const Builder& builder);
        ~DescriptorSetLayout() override;

        /** @brief Where one binding sits in its set's run of heap slots. */
        struct HeapBinding {
            kor::u32 resource = ~0u;    ///< First resource-heap slot, from the set's first; ~0 when it has none.
            kor::u32 sampler = ~0u;     ///< First sampler-heap slot, likewise.
            kor::u32 count = 1;         ///< How many slots: an array's length (an unbounded one's cap).
        };

        /** @brief How many slots of each heap a set of this layout takes. Heap only. */
        [[nodiscard]] kor::u32 ResourceSlots() const { return _resourceSlots; }
        [[nodiscard]] kor::u32 SamplerSlots() const { return _samplerSlots; }
        [[nodiscard]] const std::map<kor::u32, HeapBinding>& HeapBindings() const { return _heapBindings; }

        /** @brief How many elements an unbounded (bindless-style) array binding is given. */
        static constexpr kor::u32 UnboundedArrayCap = 256;

    private:
        std::map<kor::u32, HeapBinding> _heapBindings;
        kor::u32 _resourceSlots = 0;
        kor::u32 _samplerSlots = 0;
    };
}
