//
// DescriptorHeap — a run of the context's descriptor heap that a project fills itself.
//

#pragma once

#include <cstdint>
#include <memory>
#include <source_location>
#include <vector>

#include "api.h"
#include "bindless.h"
#include "builder.h"
#include "descriptor.h"
#include "error.h"
#include "structs.h"

namespace kor
{
    /**
     * @brief Which of the two heaps: a sampler is described in one, everything else in the other.
     */
    enum class HeapRole : std::uint8_t {
        eResource,  ///< Buffers, images, texel buffers and acceleration structures.
        eSampler,   ///< Samplers.
    };

    /** @brief A role's name, for a message: "resource". */
    [[nodiscard]] KORAL_API const char* HeapRoleName(HeapRole role);

    /**
     * @brief Slots of the descriptor heap that are the project's to fill — on the CPU, or from the GPU.
     *
     * The descriptor model (DescriptorModel::eHeap) puts every descriptor in one table: descriptor
     * sets, bindless indices, and this. What a descriptor set does for you — decide what goes in
     * which slot, keep it there, rebuild it after a shader edit — is yours here, which is the point:
     * descriptors become data. A compute pass can write them, a buffer of them can be copied into
     * place, and a shader reaches any slot through the bindless tables by @ref Index, without a
     * descriptor set in sight.
     *
     * @code
     * auto materials = kor::DescriptorHeap::Builder().SetSlotCount(1024).Build();
     * materials->Write(0, kor::Descriptor(albedoView), kor::DescriptorType::eSampledImage);
     * cb.PushConstant("first", materials->Index(0, kor::BindlessKind::eSampledImage));
     * @endcode
     *
     * Heap only: under descriptor sets there is no heap to take slots of, and the build fails saying
     * so (Context::ActiveDescriptorModel() tells you which model a run got).
     *
     * **From the GPU.** Descriptors are bytes. Encode() gives a descriptor's; a buffer of them copied
     * into the heap with CommandBuffer::CopyDescriptors puts them in place, synchronised for every
     * shader that reads the heap afterwards. A shader may also write them itself, at DeviceAddress();
     * then CommandBuffer::DescriptorHeapBarrier() is what makes the writes visible to the shaders
     * that follow.
     *
     * **Barriers on what the slots point at.** The resolver cannot see which slots a shader picks:
     * name what a draw reads through them with CommandBuffer::Uses, as for any bindless index.
     */
    class KORAL_API DescriptorHeap
    {
    public:
        struct KORAL_API Builder : kor::Builder
        {
            /// Nothing a file edit fixes: a heap that cannot be made is a model or capacity question.
            static constexpr bool Recoverable = false;

            HeapRole role = HeapRole::eResource;
            kor::u32 slotCount = 0;

            /** @brief Which heap the slots are of. Resources unless told. */
            Builder& SetRole(const HeapRole value) { role = value; return *this; }
            /** @brief How many slots. At least one. */
            Builder& SetSlotCount(kor::u32 count);

            /** @brief One build attempt. Internal: prefer Build(). */
            [[nodiscard]] Result<std::unique_ptr<DescriptorHeap>> Create() const;

            /** @brief Takes the slots, poisoned rather than thrown when they cannot be had. */
            [[nodiscard]] kor::Resource<DescriptorHeap> Build(std::source_location where = std::source_location::current()) const;
        };

        virtual ~DescriptorHeap() = default;
        DescriptorHeap(const DescriptorHeap&) = delete;
        DescriptorHeap& operator=(const DescriptorHeap&) = delete;

        /** @brief Which heap the slots are of. */
        [[nodiscard]] HeapRole Role() const { return _role; }
        /** @brief How many slots. */
        [[nodiscard]] kor::u32 SlotCount() const { return static_cast<kor::u32>(_slots.size()); }
        /** @brief Where slot 0 is in the whole heap: the bindless index of slot 0. */
        [[nodiscard]] kor::u32 FirstSlot() const { return _first; }

        /**
         * @brief Writes a descriptor into a slot, from the CPU.
         * @param type What the shader that reads the slot declares there — a buffer is described
         *        differently as a uniform and as a storage buffer, an image as sampled or storage.
         *        A combined image sampler is two descriptors here: its image in a resource heap, its
         *        sampler in a sampler heap.
         *
         * Takes effect at once, including for work already recorded: rewriting a slot a submitted
         * frame still reads is a race, as Rebind() is on a descriptor set.
         */
        VoidResult Write(kor::u32 slot, const Descriptor& descriptor, DescriptorType type);

        /** @brief The descriptor last written into @p slot from the CPU; the empty one if none was. */
        [[nodiscard]] const Descriptor& At(kor::u32 slot) const;

        /**
         * @brief The bindless index a shader reaches @p slot by, through the table of @p kind.
         * @param kind What the slot holds, which picks the table the shader indexes; it has to match
         *        what was written there.
         */
        [[nodiscard]] DescriptorIndex Index(kor::u32 slot, BindlessKind kind) const;

        /** @brief Where the GPU finds @p slot, for a shader that writes descriptors itself. */
        [[nodiscard]] virtual kor::u64 DeviceAddress(kor::u32 slot = 0) const = 0;

        /** @brief Bytes between two slots of a heap of @p role: what Encode() gives is padded to it. */
        [[nodiscard]] static kor::u32 SlotSize(HeapRole role);

        /**
         * @brief A descriptor's bytes, as they are written into a slot: for a buffer of descriptors a
         *        compute pass assembles, or one copied in with CommandBuffer::CopyDescriptors.
         *
         * The same descriptor gives the same bytes for as long as the device lives, and only that
         * long: they are meaningless to another device, and to another run.
         */
        [[nodiscard]] static Result<std::vector<std::byte>> Encode(const Descriptor& descriptor, DescriptorType type);

    protected:
        DescriptorHeap(const Builder& builder, kor::u32 first);
        virtual VoidResult DoWrite(kor::u32 slot, const Descriptor& descriptor, DescriptorType type) = 0;

        HeapRole _role = HeapRole::eResource;
        kor::u32 _first = 0;
        std::vector<Descriptor> _slots;
    };
}
