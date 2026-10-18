//
// SlotAllocator — hands out runs of consecutive slots from a fixed-size table, and takes them back.
//
// What a descriptor heap is carved up with: a descriptor set takes one run (as many slots as its
// bindings hold), a bindless index takes one slot, a project's own DescriptorHeap takes a run. First
// fit over a free list kept sorted by position, with neighbours merged when a run comes back, so a
// table that fills and empties over a session does not fragment into runs nothing fits in.
//
// Not thread-safe: the owner locks around it. Header-only so the unit tests reach it without a device.
//

#pragma once

#include <cstdint>
#include <iterator>
#include <map>
#include <optional>

namespace kor::detail
{
    class SlotAllocator
    {
    public:
        explicit SlotAllocator(const std::uint32_t capacity = 0) : _capacity(capacity)
        {
            if (capacity > 0) _free.emplace(0u, capacity);
        }

        /** @brief A run of @p count consecutive slots, or nullopt when no free run is that long. */
        [[nodiscard]] std::optional<std::uint32_t> Allocate(const std::uint32_t count)
        {
            if (count == 0) return std::nullopt;
            for (auto it = _free.begin(); it != _free.end(); ++it) {
                const auto [first, length] = *it;
                if (length < count) continue;
                _free.erase(it);
                if (length > count) _free.emplace(first + count, length - count);
                _used += count;
                return first;
            }
            return std::nullopt;
        }

        /**
         * @brief Gives a run back, merged with the free runs either side of it.
         * @return False, and nothing changed, when the run is not wholly in use (outside the table,
         *         or overlapping a free run): a double free is refused rather than corrupting the list.
         */
        bool Free(const std::uint32_t first, const std::uint32_t count)
        {
            if (count == 0 || first >= _capacity || count > _capacity - first) return false;

            auto next = _free.lower_bound(first);
            if (next != _free.end() && next->first < first + count) return false;
            if (next != _free.begin()) {
                const auto previous = std::prev(next);
                if (previous->first + previous->second > first) return false;
            }

            std::uint32_t start = first;
            std::uint32_t length = count;
            if (next != _free.begin()) {
                const auto previous = std::prev(next);
                if (previous->first + previous->second == first) {
                    start = previous->first;
                    length += previous->second;
                    _free.erase(previous);
                }
            }
            if (next != _free.end() && next->first == first + count) {
                length += next->second;
                _free.erase(next);
            }
            _free.emplace(start, length);
            _used -= count;
            return true;
        }

        [[nodiscard]] std::uint32_t Capacity() const { return _capacity; }
        [[nodiscard]] std::uint32_t Used() const { return _used; }
        /** @brief How many separate free runs there are: 1 for an empty table, more as it fragments. */
        [[nodiscard]] std::size_t FreeRuns() const { return _free.size(); }

    private:
        std::map<std::uint32_t, std::uint32_t> _free;   ///< First slot of each free run -> its length.
        std::uint32_t _capacity = 0;
        std::uint32_t _used = 0;
    };
}
