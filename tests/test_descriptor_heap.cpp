//
// What of the descriptor model is decided without a device: how a heap is carved into runs of slots,
// the bindless tables' binding numbers (which shaders spell), and the model's name and feature.
//

#include <gtest/gtest.h>

#include <set>

#include "bindless.h"
#include "context.h"
#include "deviceFeatures.h"
#include "src/core/slotAllocator.h"

namespace {

using kor::detail::SlotAllocator;

TEST(SlotAllocator, HandsOutRunsInOrder)
{
    SlotAllocator slots(16);
    EXPECT_EQ(slots.Allocate(4), 0u);
    EXPECT_EQ(slots.Allocate(4), 4u);
    EXPECT_EQ(slots.Allocate(8), 8u);
    EXPECT_EQ(slots.Used(), 16u);
    EXPECT_FALSE(slots.Allocate(1)) << "a full table has nothing to give";
}

TEST(SlotAllocator, AFreedRunIsReusedAndNeighboursMerge)
{
    SlotAllocator slots(12);
    const auto a = *slots.Allocate(4);
    const auto b = *slots.Allocate(4);
    const auto c = *slots.Allocate(4);
    ASSERT_TRUE(slots.Free(a, 4));
    ASSERT_TRUE(slots.Free(c, 4));
    EXPECT_EQ(slots.FreeRuns(), 2u);
    EXPECT_FALSE(slots.Allocate(8)) << "eight are free, but not side by side";
    ASSERT_TRUE(slots.Free(b, 4));
    EXPECT_EQ(slots.FreeRuns(), 1u) << "the three runs merged back into one";
    EXPECT_EQ(slots.Allocate(12), 0u);
}

TEST(SlotAllocator, ADoubleFreeOrAStrayRunIsRefused)
{
    SlotAllocator slots(8);
    const auto a = *slots.Allocate(4);
    ASSERT_TRUE(slots.Free(a, 4));
    EXPECT_FALSE(slots.Free(a, 4)) << "already free";
    EXPECT_FALSE(slots.Free(6, 4)) << "past the end";
    EXPECT_FALSE(slots.Free(0, 0));
    EXPECT_EQ(slots.Used(), 0u);
}

TEST(SlotAllocator, FirstFitLeavesTheRestOfARun)
{
    SlotAllocator slots(10);
    ASSERT_EQ(slots.Allocate(10), 0u);
    ASSERT_TRUE(slots.Free(2, 5));
    EXPECT_EQ(slots.Allocate(3), 2u);
    EXPECT_EQ(slots.Allocate(2), 5u);
    EXPECT_FALSE(slots.Allocate(1));
}

// Shaders hard-code these (koral_bindless.slang and .glsl), so they never move.
TEST(Bindless, EachKindHasItsOwnBindingOfTheBindlessSet)
{
    EXPECT_EQ(kor::BindlessSet, 7u);
    EXPECT_EQ(kor::BindlessBinding(kor::BindlessKind::eSampledImage), 0u);
    EXPECT_EQ(kor::BindlessBinding(kor::BindlessKind::eStorageImage), 1u);
    EXPECT_EQ(kor::BindlessBinding(kor::BindlessKind::eStorageBuffer), 2u);
    EXPECT_EQ(kor::BindlessBinding(kor::BindlessKind::eUniformBuffer), 3u);
    EXPECT_EQ(kor::BindlessBinding(kor::BindlessKind::eSampler), 4u);
    EXPECT_EQ(kor::BindlessBinding(kor::BindlessKind::eAccelerationStructure), 5u);
    EXPECT_FALSE(kor::DescriptorIndex{}.Valid());
}

TEST(DescriptorModel, IsNamedAsKoralJsonSpellsIt)
{
    EXPECT_STREQ(kor::DescriptorModelName(kor::DescriptorModel::eSets), "sets");
    EXPECT_STREQ(kor::DescriptorModelName(kor::DescriptorModel::eHeap), "heap");
    EXPECT_EQ(kor::FeatureNamed("DescriptorHeap"), kor::Feature::eDescriptorHeap);
    EXPECT_EQ(kor::Context::ActiveDescriptorModel(), kor::DescriptorModel::eSets) << "no device, no heap";
}

}
