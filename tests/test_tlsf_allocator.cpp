// Unit tests for gfx::TLSFAllocator — a pure CPU-side two-level segregated-fit
// allocator working in abstract element units. No GPU involved.

#include <gtest/gtest.h>

#include <map>
#include <random>
#include <vector>

#include "tlsfAllocator.h"

using gfx::TLSFAllocator;
using gfx::TLSFAllocation;

namespace {

constexpr uint64_t kMin = TLSFAllocator::MIN_BLOCK_SIZE; // 32

// -----------------------------------------------------------------------------
// Construction
// -----------------------------------------------------------------------------
TEST(TlsfAllocator, RejectsCapacityBelowMinimum) {
    EXPECT_THROW(TLSFAllocator(kMin - 1), std::invalid_argument);
    EXPECT_THROW(TLSFAllocator(0), std::invalid_argument);
}

TEST(TlsfAllocator, AcceptsExactMinimumCapacity) {
    EXPECT_NO_THROW(TLSFAllocator alloc(kMin));
}

TEST(TlsfAllocator, FreshAllocatorIsEmpty) {
    TLSFAllocator alloc(1024);
    EXPECT_EQ(alloc.Capacity(), 1024u);
    EXPECT_EQ(alloc.Allocated(), 0u);
    EXPECT_EQ(alloc.Available(), 1024u);
}

// -----------------------------------------------------------------------------
// Basic allocation semantics
// -----------------------------------------------------------------------------
TEST(TlsfAllocator, ZeroSizeAllocationFails) {
    TLSFAllocator alloc(1024);
    EXPECT_FALSE(alloc.Allocate(0).has_value());
    EXPECT_EQ(alloc.Allocated(), 0u);
}

TEST(TlsfAllocator, SingleAllocationStartsAtZero) {
    TLSFAllocator alloc(1024);
    auto a = alloc.Allocate(64);
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ(a->offset, 0u);
    EXPECT_GE(a->size, 64u);
    EXPECT_EQ(alloc.Allocated(), a->size);
    EXPECT_EQ(alloc.Available(), alloc.Capacity() - a->size);
}

TEST(TlsfAllocator, SubMinimumRequestReservesAtLeastMinimum) {
    TLSFAllocator alloc(1024);
    auto a = alloc.Allocate(1);
    ASSERT_TRUE(a.has_value());
    EXPECT_GE(a->size, kMin);
}

TEST(TlsfAllocator, OverCapacityRequestFails) {
    TLSFAllocator alloc(1024);
    EXPECT_FALSE(alloc.Allocate(2048).has_value());
    EXPECT_EQ(alloc.Allocated(), 0u);
}

TEST(TlsfAllocator, CanAllocateEntireCapacityInOneBlock) {
    TLSFAllocator alloc(1024);
    auto a = alloc.Allocate(1024);
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ(a->offset, 0u);
    EXPECT_EQ(a->size, 1024u);
    EXPECT_EQ(alloc.Available(), 0u);
    // Nothing left.
    EXPECT_FALSE(alloc.Allocate(kMin).has_value());
}

// -----------------------------------------------------------------------------
// Non-overlap
// -----------------------------------------------------------------------------
TEST(TlsfAllocator, ConsecutiveAllocationsDoNotOverlap) {
    TLSFAllocator alloc(4096);
    auto a = alloc.Allocate(100);
    auto b = alloc.Allocate(100);
    auto c = alloc.Allocate(100);
    ASSERT_TRUE(a && b && c);

    // Ordered, tightly packed, non-overlapping.
    EXPECT_LE(a->offset + a->size, b->offset);
    EXPECT_LE(b->offset + b->size, c->offset);
}

// -----------------------------------------------------------------------------
// Free / accounting / coalescing
// -----------------------------------------------------------------------------
TEST(TlsfAllocator, FreeRestoresAvailability) {
    TLSFAllocator alloc(1024);
    auto a = alloc.Allocate(256);
    ASSERT_TRUE(a.has_value());
    const uint64_t used = alloc.Allocated();
    EXPECT_GT(used, 0u);
    alloc.Free(*a);
    EXPECT_EQ(alloc.Allocated(), 0u);
    EXPECT_EQ(alloc.Available(), alloc.Capacity());
}

TEST(TlsfAllocator, FreedSpaceCanBeFullyReallocated) {
    // Allocate the whole heap, free it, and allocate the whole heap again.
    // This only works if Free coalesces the block back to the root.
    TLSFAllocator alloc(1024);
    auto a = alloc.Allocate(1024);
    ASSERT_TRUE(a.has_value());
    alloc.Free(*a);
    auto b = alloc.Allocate(1024);
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(b->offset, 0u);
    EXPECT_EQ(b->size, 1024u);
}

TEST(TlsfAllocator, CoalescesAdjacentFreedBlocks) {
    TLSFAllocator alloc(1024);
    auto a = alloc.Allocate(256);
    auto b = alloc.Allocate(256);
    auto c = alloc.Allocate(256);
    ASSERT_TRUE(a && b && c);

    // Free all three; they are physically adjacent so should merge back into a
    // single free region large enough to satisfy a request spanning all of them.
    alloc.Free(*b);
    alloc.Free(*a);
    alloc.Free(*c);

    EXPECT_EQ(alloc.Allocated(), 0u);
    auto big = alloc.Allocate(768);
    ASSERT_TRUE(big.has_value());
    EXPECT_EQ(big->offset, 0u);
}

TEST(TlsfAllocator, ReusesHoleLeftByFreedMiddleBlock) {
    TLSFAllocator alloc(4096);
    auto a = alloc.Allocate(512);
    auto b = alloc.Allocate(512);
    auto c = alloc.Allocate(512);
    ASSERT_TRUE(a && b && c);

    const uint64_t holeOffset = b->offset;
    alloc.Free(*b);

    // A same-size request should be satisfiable again; first-fit reuses the hole.
    auto d = alloc.Allocate(512);
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(d->offset, holeOffset);
}

// -----------------------------------------------------------------------------
// Move semantics (allocator is movable, not copyable)
// -----------------------------------------------------------------------------
TEST(TlsfAllocator, IsMovable) {
    TLSFAllocator alloc(1024);
    auto a = alloc.Allocate(128);
    ASSERT_TRUE(a.has_value());

    TLSFAllocator moved(std::move(alloc));
    EXPECT_EQ(moved.Capacity(), 1024u);
    EXPECT_EQ(moved.Allocated(), a->size);
    // The moved-to allocator still tracks the live block correctly.
    EXPECT_NO_THROW(moved.Free(*a));
    EXPECT_EQ(moved.Allocated(), 0u);
}

// -----------------------------------------------------------------------------
// Randomized stress test: the strongest invariant guard.
//   * live allocations never overlap
//   * Allocated() equals the summed size of live allocations
//   * Available() == Capacity() - Allocated()
// -----------------------------------------------------------------------------
TEST(TlsfAllocator, RandomizedStressMaintainsInvariants) {
    constexpr uint64_t kCapacity = 1u << 16; // 65536 elements
    TLSFAllocator alloc(kCapacity);

    std::mt19937 rng(12345);
    std::uniform_int_distribution<uint64_t> sizeDist(1, 2048);
    std::uniform_int_distribution<int> coin(0, 1);

    // offset -> size of live allocations
    std::map<uint64_t, uint64_t> live;

    auto totalLive = [&] {
        uint64_t sum = 0;
        for (auto& [off, sz] : live) sum += sz;
        return sum;
    };
    auto assertNoOverlap = [&](uint64_t off, uint64_t sz) {
        // Check against existing intervals.
        for (auto& [o, s] : live) {
            const bool disjoint = (off + sz <= o) || (o + s <= off);
            ASSERT_TRUE(disjoint)
                << "overlap: new [" << off << "," << off + sz << ") vs ["
                << o << "," << o + s << ")";
        }
    };

    std::vector<uint64_t> keys; // live offsets, for random freeing

    for (int i = 0; i < 5000; ++i) {
        const bool doAlloc = live.empty() || coin(rng) == 0;
        if (doAlloc) {
            const uint64_t req = sizeDist(rng);
            auto a = alloc.Allocate(req);
            if (a.has_value()) {
                EXPECT_GE(a->size, req);
                EXPECT_LE(a->offset + a->size, kCapacity);
                assertNoOverlap(a->offset, a->size);
                ASSERT_FALSE(live.contains(a->offset));
                live[a->offset] = a->size;
                keys.push_back(a->offset);
            }
        } else {
            // Free a random live allocation.
            std::uniform_int_distribution<size_t> pick(0, keys.size() - 1);
            const size_t idx = pick(rng);
            const uint64_t off = keys[idx];
            alloc.Free(TLSFAllocation{off, live[off]});
            live.erase(off);
            keys[idx] = keys.back();
            keys.pop_back();
        }

        // Invariants after every operation.
        ASSERT_EQ(alloc.Allocated(), totalLive());
        ASSERT_EQ(alloc.Available(), kCapacity - alloc.Allocated());
    }

    // Free everything and confirm we return to a pristine, fully-usable heap.
    for (auto& [off, sz] : live) alloc.Free(TLSFAllocation{off, sz});
    EXPECT_EQ(alloc.Allocated(), 0u);
    auto whole = alloc.Allocate(kCapacity);
    EXPECT_TRUE(whole.has_value());
}

} // namespace
