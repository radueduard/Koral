//
// Created by radue on 5/11/2026.
//
// A generic CPU-side Two-Level Segregated Fit (TLSF) allocator.
// Operates in abstract "element" units. No GPU, no mesh concepts.
//
// It lives in the mesh module because MeshHeap is the only thing that needs it, and the engine
// should not carry a suballocator in its public surface for one consumer's sake. Nothing about
// the algorithm is mesh-specific — a project that wants it for something else links koral-mesh.
//

#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

namespace kmesh
{
    /**
     * @brief A reserved range within a TLSF-managed heap. Offsets and sizes are in elements.
     *
     * Returned by TLSFAllocator::Allocate() and handed back to TLSFAllocator::Free() unchanged.
     */
    struct TLSFAllocation
    {
        std::uint64_t offset; ///< Element offset into the backing heap.
        std::uint64_t size;   ///< Number of elements actually reserved (may exceed requested due to block granularity).
    };

    /**
     * @brief A general-purpose suballocator: hands out ranges of one large block in O(1).
     *
     * Two-Level Segregated Fit keeps free blocks in buckets indexed by size, so a fitting block is
     * found by two bit scans rather than a search — allocation and free both take constant time,
     * whatever the fragmentation. Adjacent free blocks are merged as they are released.
     *
     * It manages no memory of its own: it works in abstract element units over a heap somebody else
     * owns, which is how MeshHeap packs many meshes into one pair of GPU buffers. Not thread-safe.
     *
     * @code
     * kmesh::TLSFAllocator allocator(vertexCapacity);
     * if (const auto range = allocator.Allocate(vertexCount)) {
     *     upload(range->offset, range->size);
     *     allocator.Free(*range);
     * }
     * @endcode
     */
    class TLSFAllocator
    {
    public:
        // TLSF parameters
        static constexpr int SL_INDEX_COUNT_LOG2 = 5;                      // log2(sub-lists per FL bucket) = 5 → 32
        static constexpr int SL_INDEX_COUNT      = 1 << SL_INDEX_COUNT_LOG2; // 32
        static constexpr int FL_INDEX_SHIFT      = SL_INDEX_COUNT_LOG2;
        static constexpr int FL_INDEX_MAX        = 32;                      // supports allocations up to 2^32 elements
        static constexpr std::uint64_t MIN_BLOCK_SIZE = SL_INDEX_COUNT;          // 32 elements minimum

        /**
         * @brief Creates an allocator over a heap of @p capacity elements.
         * @param capacity How many elements the backing heap holds.
         * @throws std::invalid_argument if @p capacity is below MIN_BLOCK_SIZE.
         */
        explicit TLSFAllocator(std::uint64_t capacity)
            : _capacity(capacity)
        {
            if (capacity < MIN_BLOCK_SIZE)
                throw std::invalid_argument("TLSFAllocator: capacity must be >= MIN_BLOCK_SIZE");

            // Reserve a sensible initial pool; it grows as needed.
            _pool.reserve(256);

            // Seed the pool with a sentinel null node at index 0 and
            // one free block covering the whole capacity.
            _pool.push_back(Block{});   // index 0 = null sentinel

            const std::uint32_t rootIdx = newBlock();
            Block& root = _pool[rootIdx];
            root.offset   = 0;
            root.size     = capacity;
            root.isFree   = true;
            root.prevPhys = 0;
            root.nextPhys = 0;

            insertFreeBlock(rootIdx);
        }

        ~TLSFAllocator() = default;

        // Non-copyable, movable
        TLSFAllocator(const TLSFAllocator&)            = delete;
        TLSFAllocator& operator=(const TLSFAllocator&) = delete;
        TLSFAllocator(TLSFAllocator&&)                 = default;
        TLSFAllocator& operator=(TLSFAllocator&&)      = default;

        /**
         * @brief Reserves a contiguous range of elements.
         * @param numElements How many are needed.
         * @return The reserved range, or nullopt when no free block is large enough. The range may
         *         be larger than requested: allocations are rounded up to the block granularity.
         */
        [[nodiscard]] std::optional<TLSFAllocation> Allocate(std::uint64_t numElements)
        {
            if (numElements == 0) return std::nullopt;

            const std::uint64_t size = std::max(numElements, MIN_BLOCK_SIZE);

            int fl, sl;
            mappingSearch(size, fl, sl);

            const std::uint32_t blockIdx = findSuitableBlock(fl, sl);
            if (blockIdx == 0) return std::nullopt;

            removeFreeBlock(blockIdx);

            // Split if the remainder is large enough
            const std::uint64_t remaining = _pool[blockIdx].size - size;
            if (remaining >= MIN_BLOCK_SIZE)
            {
                const std::uint32_t splitIdx = newBlock();
                Block& blk   = _pool[blockIdx]; // re-fetch after potential reallocation
                Block& split = _pool[splitIdx];

                split.offset   = blk.offset + size;
                split.size     = remaining;
                split.isFree   = true;
                split.prevPhys = blockIdx;
                split.nextPhys = blk.nextPhys;

                if (blk.nextPhys != 0)
                    _pool[blk.nextPhys].prevPhys = splitIdx;

                blk.size     = size;
                blk.nextPhys = splitIdx;

                insertFreeBlock(splitIdx);
            }

            _pool[blockIdx].isFree = false;
            _allocated += _pool[blockIdx].size;

            return TLSFAllocation{ _pool[blockIdx].offset, _pool[blockIdx].size };
        }

        /**
         * @brief Releases a range, merging it with any free neighbours.
         * @param alloc The range exactly as Allocate() returned it.
         *
         * Freeing a range that was never allocated, or freeing one twice, asserts in a debug build.
         */
        void Free(TLSFAllocation alloc)
        {
            const std::uint32_t blockIdx = findBlockByOffset(alloc.offset);
            assert(blockIdx != 0 && "TLSFAllocator::Free: invalid offset");
            assert(!_pool[blockIdx].isFree && "TLSFAllocator::Free: double free");

            _allocated -= _pool[blockIdx].size;
            _pool[blockIdx].isFree = true;

            const std::uint32_t merged = mergeBlock(blockIdx);
            insertFreeBlock(merged);
        }

        /** @brief How many elements the heap holds in total. */
        [[nodiscard]] std::uint64_t Capacity()  const { return _capacity;  }

        /** @brief How many elements are currently reserved, including block-granularity rounding. */
        [[nodiscard]] std::uint64_t Allocated() const { return _allocated; }

        /**
         * @brief How many elements are unreserved.
         * @return Capacity minus Allocated. Not a promise that a single allocation of that size will
         *         succeed: the free space may be spread over several blocks.
         */
        [[nodiscard]] std::uint64_t Available() const { return _capacity - _allocated; }

    private:
        // =====================================================================
        // Internal block node (CPU-only metadata)
        // =====================================================================
        struct Block
        {
            std::uint64_t offset   = 0;
            std::uint64_t size     = 0;
            bool     isFree   = false;

            std::uint32_t prevFree = 0; ///< Previous block in the same segregated free list (0 = none)
            std::uint32_t nextFree = 0; ///< Next block in the same segregated free list    (0 = none)

            std::uint32_t prevPhys = 0; ///< Physical predecessor in address order (0 = none)
            std::uint32_t nextPhys = 0; ///< Physical successor  in address order  (0 = none)
        };

        std::uint64_t _capacity  = 0;
        std::uint64_t _allocated = 0;

        // Two-level bitmaps
        std::uint32_t _flBitmap = 0;
        std::array<std::uint32_t, FL_INDEX_MAX> _slBitmap{};

        // Segregated free lists: _freeLists[fl][sl] = index of head block (0 = empty)
        std::array<std::array<std::uint32_t, SL_INDEX_COUNT>, FL_INDEX_MAX> _freeLists{};

        // Block pool (index 0 is null sentinel, never used for real blocks)
        std::vector<Block> _pool;

        // Intrusive free-node list for recycling pool slots
        std::uint32_t _freeNodeHead = 0;

        // =====================================================================
        // Pool management
        // =====================================================================
        std::uint32_t newBlock()
        {
            if (_freeNodeHead != 0)
            {
                const std::uint32_t idx = _freeNodeHead;
                _freeNodeHead = _pool[idx].nextFree;
                _pool[idx] = Block{};
                return idx;
            }
            _pool.push_back(Block{});
            return static_cast<std::uint32_t>(_pool.size() - 1);
        }

        void recycleBlock(std::uint32_t idx)
        {
            _pool[idx] = Block{};
            _pool[idx].nextFree = _freeNodeHead;
            _freeNodeHead = idx;
        }

        // =====================================================================
        // TLSF index mapping
        // =====================================================================
        static void mappingInsert(std::uint64_t size, int& fl, int& sl)
        {
            if (size < static_cast<std::uint64_t>(MIN_BLOCK_SIZE))
            {
                fl = 0;
                sl = static_cast<int>(size * SL_INDEX_COUNT / MIN_BLOCK_SIZE);
            }
            else
            {
                fl = 63 - std::countl_zero(size);
                sl = static_cast<int>((size >> (fl - SL_INDEX_COUNT_LOG2)) ^ SL_INDEX_COUNT);
                fl -= FL_INDEX_SHIFT - 1;
            }
            assert(fl >= 0 && fl < FL_INDEX_MAX);
            assert(sl >= 0 && sl < SL_INDEX_COUNT);
        }

        // Round size UP to guarantee a suitable free block is found
        static void mappingSearch(std::uint64_t size, int& fl, int& sl)
        {
            if (size >= static_cast<std::uint64_t>(MIN_BLOCK_SIZE))
            {
                const std::uint64_t round = (1ULL << (63 - std::countl_zero(size) - SL_INDEX_COUNT_LOG2)) - 1;
                size += round;
            }
            mappingInsert(size, fl, sl);
        }

        // =====================================================================
        // Free list management
        // =====================================================================
        void insertFreeBlock(std::uint32_t idx)
        {
            int fl, sl;
            mappingInsert(_pool[idx].size, fl, sl);

            _pool[idx].prevFree = 0;
            _pool[idx].nextFree = _freeLists[fl][sl];

            if (_freeLists[fl][sl] != 0)
                _pool[_freeLists[fl][sl]].prevFree = idx;

            _freeLists[fl][sl] = idx;
            _flBitmap  |= (1u << fl);
            _slBitmap[fl] |= (1u << sl);
        }

        void removeFreeBlock(std::uint32_t idx)
        {
            int fl, sl;
            mappingInsert(_pool[idx].size, fl, sl);

            if (_pool[idx].prevFree != 0)
                _pool[_pool[idx].prevFree].nextFree = _pool[idx].nextFree;
            else
                _freeLists[fl][sl] = _pool[idx].nextFree;

            if (_pool[idx].nextFree != 0)
                _pool[_pool[idx].nextFree].prevFree = _pool[idx].prevFree;

            if (_freeLists[fl][sl] == 0)
            {
                _slBitmap[fl] &= ~(1u << sl);
                if (_slBitmap[fl] == 0)
                    _flBitmap &= ~(1u << fl);
            }

            _pool[idx].prevFree = 0;
            _pool[idx].nextFree = 0;
        }

        // =====================================================================
        // Block search
        // =====================================================================
        std::uint32_t findSuitableBlock(int fl, int sl) const
        {
            // Look in the same fl bucket first, at sl or higher
            std::uint32_t slMap = _slBitmap[fl] & (~0u << sl);
            if (slMap != 0)
            {
                const int foundSl = std::countr_zero(slMap);
                return _freeLists[fl][foundSl];
            }

            // Search higher fl buckets
            const std::uint32_t flMap = _flBitmap & (~0u << (fl + 1));
            if (flMap == 0) return 0; // out of memory

            const int foundFl = std::countr_zero(flMap);
            const int foundSl = std::countr_zero(_slBitmap[foundFl]);
            return _freeLists[foundFl][foundSl];
        }

        // Linear scan by offset — only called on Free(); acceptable cost.
        std::uint32_t findBlockByOffset(std::uint64_t offset) const
        {
            for (std::uint32_t i = 1; i < static_cast<std::uint32_t>(_pool.size()); ++i)
            {
                if (_pool[i].offset == offset && !_pool[i].isFree)
                    return i;
            }
            return 0;
        }

        // =====================================================================
        // Coalescing
        // =====================================================================
        std::uint32_t mergeBlock(std::uint32_t idx)
        {
            // Merge with next physical block if free
            const std::uint32_t nextIdx = _pool[idx].nextPhys;
            if (nextIdx != 0 && _pool[nextIdx].isFree)
            {
                removeFreeBlock(nextIdx);
                _pool[idx].size     += _pool[nextIdx].size;
                _pool[idx].nextPhys  = _pool[nextIdx].nextPhys;
                if (_pool[nextIdx].nextPhys != 0)
                    _pool[_pool[nextIdx].nextPhys].prevPhys = idx;
                recycleBlock(nextIdx);
            }

            // Merge with previous physical block if free
            const std::uint32_t prevIdx = _pool[idx].prevPhys;
            if (prevIdx != 0 && _pool[prevIdx].isFree)
            {
                removeFreeBlock(prevIdx);
                _pool[prevIdx].size     += _pool[idx].size;
                _pool[prevIdx].nextPhys  = _pool[idx].nextPhys;
                if (_pool[idx].nextPhys != 0)
                    _pool[_pool[idx].nextPhys].prevPhys = prevIdx;
                recycleBlock(idx);
                return prevIdx;
            }

            return idx;
        }
    };

} // namespace kmesh