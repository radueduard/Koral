//
// Vulkan backend of kor::DescriptorHeap: a run of slots of the context's heap.
//

#pragma once

#include <descriptorHeap.h>

namespace kor::vk
{
    class DescriptorHeap final : public kor::DescriptorHeap
    {
    public:
        explicit DescriptorHeap(const Builder& builder);
        ~DescriptorHeap() override;

        [[nodiscard]] kor::u64 DeviceAddress(kor::u32 slot) const override;

    protected:
        VoidResult DoWrite(kor::u32 slot, const Descriptor& descriptor, DescriptorType type) override;
    };
}
