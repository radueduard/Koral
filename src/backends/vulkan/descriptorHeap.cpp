//
// Vulkan backend of kor::DescriptorHeap. See descriptorHeap.h.
//

#include "descriptorHeap.h"

#include <format>
#include <stdexcept>

#include "heaps.h"
#include "sampler.h"
#include "vulkanContext.h"

namespace kor::vk
{
    namespace
    {
        // The run is taken before the base class is made, because the base records where it starts.
        kor::u32 take(const kor::DescriptorHeap::Builder& builder)
        {
            const auto first = Context::Heaps()->Allocate(builder.role, builder.slotCount);
            if (!first)
                throw std::runtime_error(std::format(
                    "The {} heap has no run of {} free slots left (it holds {}, and {} are in use).",
                    HeapRoleName(builder.role), builder.slotCount, Context::Heaps()->Capacity(builder.role),
                    Context::Heaps()->Used(builder.role)));
            return *first;
        }
    }

    DescriptorHeap::DescriptorHeap(const Builder& builder) : kor::DescriptorHeap(builder, take(builder))
    {
    }

    DescriptorHeap::~DescriptorHeap()
    {
        if (auto* heaps = Context::Heaps()) heaps->Release(_role, _first, SlotCount());
    }

    kor::u64 DescriptorHeap::DeviceAddress(const kor::u32 slot) const
    {
        const auto* heaps = Context::Heaps();
        return heaps->Address(_role) + static_cast<kor::u64>(_first + slot) * heaps->SlotSize(_role);
    }

    VoidResult DescriptorHeap::DoWrite(const kor::u32 slot, const Descriptor& descriptor, const DescriptorType type)
    {
        DescriptorHeaps::Writer writer(*Context::Heaps());
        if (_role == HeapRole::eSampler)
            writer.Sampler(_first + slot, dynamic_cast<const vk::Sampler&>(descriptor.BoundSampler()).CreateInfo());
        else
            writer.Resource(_first + slot, type, descriptor, 0);
        return {};
    }
}
