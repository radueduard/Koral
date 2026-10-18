//
// kor::DescriptorHeap — the checks and the CPU-side record of what is in each slot. The slots
// themselves are taken from the context's heap by the backend (src/backends/vulkan/descriptorHeap.cpp).
//

#include <descriptorHeap.h>

#include <format>

#include <context.h>
#include <log.h>

#include "../backends/vulkan/descriptorHeap.h"
#include "../backends/vulkan/heaps.h"
#include "../backends/vulkan/vulkanContext.h"

namespace kor
{
    const char* HeapRoleName(const HeapRole role)
    {
        return role == HeapRole::eSampler ? "sampler" : "resource";
    }

    DescriptorHeap::DescriptorHeap(const Builder& builder, const kor::u32 first)
        : _role(builder.role), _first(first), _slots(builder.slotCount)
    {
    }

    VoidResult DescriptorHeap::Write(const kor::u32 slot, const Descriptor& descriptor, const DescriptorType type)
    {
        const auto fail = [](std::string message) {
            return std::unexpected(Error{ .code = ErrorCode::eInvalidArgument, .message = std::move(message) });
        };
        if (slot >= _slots.size())
            return fail(std::format("Slot {} is past this descriptor heap's {} slots.", slot, _slots.size()));
        if (!descriptor.IsValid())
            return std::unexpected(descriptor.Failure().value_or(Error{
                .code = ErrorCode::eInvalidArgument, .message = std::format("The descriptor written to slot {} is invalid.", slot) }));
        const bool samplerType = type == DescriptorType::eSampler;
        if (samplerType != (_role == HeapRole::eSampler))
            return fail(std::format("A {} heap holds {}; slot {} was written as something else. A combined image sampler "
                                    "is two descriptors here: its image in a resource heap, its sampler in a sampler heap.",
                                    HeapRoleName(_role), _role == HeapRole::eSampler ? "samplers only" : "everything but samplers", slot));

        // The accessors throw on a kind the descriptor does not hold: a buffer written as an image is
        // said here, not encoded into a slot the shader then reads garbage from.
        try {
            switch (type) {
            case DescriptorType::eUniformBuffer:
            case DescriptorType::eStorageBuffer:        (void)descriptor.BoundBuffer(); break;
            case DescriptorType::eSampledImage:
            case DescriptorType::eStorageImage:
            case DescriptorType::eCombinedImageSampler: (void)descriptor.BoundImageView(); break;
            case DescriptorType::eSampler:              (void)descriptor.BoundSampler(); break;
            case DescriptorType::eAccelerationStructure:(void)descriptor.BoundAccelerationStructure(); break;
            case DescriptorType::eUniformTexelBuffer:
            case DescriptorType::eStorageTexelBuffer:   (void)descriptor.BoundBufferView(); break;
            default: return fail(std::format("Slot {} was written as a kind of descriptor a heap cannot hold.", slot));
            }
        } catch (const std::exception& e) {
            return fail(std::format("The descriptor written to slot {} is not the kind it was written as: {}", slot, e.what()));
        }

        if (auto written = DoWrite(slot, descriptor, type); !written) return written;
        _slots[slot] = descriptor;
        return {};
    }

    const Descriptor& DescriptorHeap::At(const kor::u32 slot) const
    {
        static const Descriptor empty;
        return slot < _slots.size() ? _slots[slot] : empty;
    }

    DescriptorIndex DescriptorHeap::Index(const kor::u32 slot, const BindlessKind kind) const
    {
        if (slot >= _slots.size()) {
            log::Error("[descriptors] Slot {} is past this descriptor heap's {} slots.", slot, _slots.size());
            return {};
        }
        if ((kind == BindlessKind::eSampler) != (_role == HeapRole::eSampler)) {
            log::Error("[descriptors] A {} heap's slot cannot be reached through the {} table.", HeapRoleName(_role), BindlessKindName(kind));
            return {};
        }
        return { _first + slot, kind };
    }

    kor::u32 DescriptorHeap::SlotSize(const HeapRole role)
    {
        const auto* heaps = Context::HasDevice() ? vk::Context::Heaps() : nullptr;
        return heaps ? heaps->SlotSize(role) : 0;
    }

    Result<std::vector<std::byte>> DescriptorHeap::Encode(const Descriptor& descriptor, const DescriptorType type)
    {
        auto* heaps = Context::HasDevice() ? vk::Context::Heaps() : nullptr;
        if (!heaps)
            return std::unexpected(Error{ .code = ErrorCode::eDescriptorHeapUnsupported,
                .message = "Descriptors have bytes only under the descriptor heap; this device was made for descriptor sets." });
        if (!descriptor.IsValid())
            return std::unexpected(descriptor.Failure().value_or(Error{ .code = ErrorCode::eInvalidArgument,
                .message = "Cannot encode an invalid descriptor." }));
        const auto role = type == DescriptorType::eSampler ? HeapRole::eSampler : HeapRole::eResource;
        std::vector<std::byte> bytes(heaps->SlotSize(role));
        vk::DescriptorHeaps::Writer writer(*heaps);
        writer.ResourceTo(bytes.data(), type, descriptor, 0);
        writer.Flush();
        return bytes;
    }

    DescriptorHeap::Builder& DescriptorHeap::Builder::SetSlotCount(const kor::u32 count)
    {
        if (count == 0) AddError(ErrorCode::eInvalidArgument, "A descriptor heap needs at least one slot.");
        else slotCount = count;
        return *this;
    }

    Result<std::unique_ptr<DescriptorHeap>> DescriptorHeap::Builder::Create() const
    {
        BeginAttempt();
        if (slotCount == 0) AddError(ErrorCode::eInvalidArgument, "A descriptor heap needs SetSlotCount(n) with n at least one.");
        if (auto v = Validate(); !v) return std::unexpected(v.error());
        if (!Context::HasDevice() || !vk::Context::Heaps())
            return Fail(ErrorCode::eDescriptorHeapUnsupported,
                "A DescriptorHeap takes slots of the descriptor heap, and this device was made for descriptor sets. "
                "Set rendering.descriptors to \"heap\" in koral.json (or AppSettings::descriptors) on a GPU with "
                "VK_EXT_descriptor_heap; Context::ActiveDescriptorModel() says which one a run got.");
        return Guard(ErrorCode::eBackend, [&]() -> std::unique_ptr<DescriptorHeap> {
            return kor::MakeBackendPtr<DescriptorHeap, vk::DescriptorHeap>(*this);
        });
    }

    Resource<DescriptorHeap> DescriptorHeap::Builder::Build(const std::source_location where) const
    {
        return Materialize<DescriptorHeap>(*this, "DescriptorHeap", where);
    }
}
