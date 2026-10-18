//
// The bindless tables: what kor::Bindless hands indices into.
//
// Under descriptor sets they are one descriptor set, made once with the device, with one unbounded
// update-after-bind array per kind at the bindings of kor::BindlessSet. It is bound for every draw and
// dispatch of a pipeline that declares the set; an index is a slot of the kind's array.
//
// Under the descriptor heap there is nothing of their own to make: a pipeline that declares the set
// has those arrays mapped onto the whole heap (the sampler array onto the sampler heap), so an index
// is a heap slot, taken from the same allocator as everything else in the heap.
//

#pragma once

#include <map>
#include <mutex>
#include <optional>
#include <variant>
#include <vector>

#include <vulkan/vulkan.hpp>

#include <bindless.h>
#include <imageView.h>
#include <sampler.h>
#include <accelerationStructure.h>
#include <buffer.h>
#include <image.h>

#include "../../core/slotAllocator.h"

namespace kor::vk
{
    class BindlessTables
    {
    public:
        BindlessTables();
        ~BindlessTables();

        BindlessTables(const BindlessTables&) = delete;
        BindlessTables& operator=(const BindlessTables&) = delete;

        /** @brief The set's layout, for a pipeline layout that declares it. Null under the heap. */
        [[nodiscard]] ::vk::DescriptorSetLayout Layout() const { return _layout; }
        /** @brief An empty set layout, for a pipeline layout's sets between its own and the bindless one. */
        [[nodiscard]] ::vk::DescriptorSetLayout EmptyLayout() const { return _emptyLayout; }
        /** @brief The set itself, to bind. Null under the heap. */
        [[nodiscard]] ::vk::DescriptorSet Set() const { return _set; }

        /** @brief Whether this device has a table of @p kind at all (under sets, some need update-after-bind). */
        [[nodiscard]] bool Has(BindlessKind kind) const;
        [[nodiscard]] kor::u32 Capacity(BindlessKind kind) const;
        [[nodiscard]] std::size_t Count() const;

        // kor::Bindless, one entry point per kind of resource. The refs are checked by the caller.
        DescriptorIndex Acquire(BindlessKind kind, const ResourceRef<const kor::ImageView>& view,
                                const ResourceRef<const kor::Image>& image, ImageShape shape);
        DescriptorIndex Acquire(BindlessKind kind, const ResourceRef<const kor::Buffer>& buffer);
        DescriptorIndex Acquire(const ResourceRef<const kor::Sampler>& sampler);
        DescriptorIndex Acquire(const ResourceRef<const kor::AccelerationStructure>& structure);

        void Release(DescriptorIndex handle);
        [[nodiscard]] std::optional<detail::BindlessTarget> Target(DescriptorIndex handle) const;
        void Collect();

    private:
        struct Entry
        {
            // What the index points at, held weakly: the index goes when its resource does.
            std::variant<std::monostate, ResourceRef<const kor::ImageView>, ResourceRef<const kor::Buffer>,
                         ResourceRef<const kor::Sampler>, ResourceRef<const kor::AccelerationStructure>> resource;
            // For an index asked of an Image rather than a view: the image, and the shape to view it as
            // again after a resize, which drops the image's own views.
            ResourceRef<const kor::Image> image;
            ImageShape shape = ImageShape::e2D;
            kor::u64 generation = 0;
            const void* key = nullptr;
        };

        [[nodiscard]] static bool alive(const Entry& entry);
        [[nodiscard]] std::optional<kor::u32> allocate(BindlessKind kind);
        void free(BindlessKind kind, kor::u32 index);
        void write(BindlessKind kind, kor::u32 index, const Entry& entry);
        DescriptorIndex acquire(BindlessKind kind, const void* key, Entry entry);

        ::vk::DescriptorSetLayout _layout;
        ::vk::DescriptorSetLayout _emptyLayout;
        ::vk::DescriptorPool _pool;
        ::vk::DescriptorSet _set;
        std::map<BindlessKind, kor::u32> _capacity;
        std::map<BindlessKind, detail::SlotAllocator> _slots;   ///< Under sets: one index space per kind.

        std::map<std::pair<BindlessKind, kor::u32>, Entry> _entries;
        std::map<std::pair<const void*, BindlessKind>, kor::u32> _byResource;
        mutable std::recursive_mutex _mutex;
    };
}
