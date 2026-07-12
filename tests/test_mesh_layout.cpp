// Unit tests for the compile-time vertex layout machinery in meshLayout.h:
// VertexValueTraits, Std430AlignTraits, ParamVertex storage/stride/offset math,
// and FindPositionAttribute. This is pure CPU reflection math — no GPU needed.
// (ParamMesh::Create and friends touch the GPU and are intentionally not used.)

#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include "meshLayout.h"

using namespace kor;

namespace {

// -----------------------------------------------------------------------------
// VertexValueTraits: channel count + channel type for scalars and glm vectors.
// -----------------------------------------------------------------------------
TEST(MeshLayout, ValueTraitsScalar) {
    EXPECT_EQ(VertexValueTraits<float>::channelCount, 1u);
    EXPECT_EQ(VertexValueTraits<float>::channelType, ChannelType::eFloat);
    EXPECT_EQ(VertexValueTraits<int>::channelType, ChannelType::eInt);
    EXPECT_EQ(VertexValueTraits<unsigned int>::channelType, ChannelType::eUInt);
}

TEST(MeshLayout, ValueTraitsVectors) {
    EXPECT_EQ(VertexValueTraits<glm::vec2>::channelCount, 2u);
    EXPECT_EQ(VertexValueTraits<glm::vec3>::channelCount, 3u);
    EXPECT_EQ(VertexValueTraits<glm::vec4>::channelCount, 4u);
    EXPECT_EQ(VertexValueTraits<glm::vec3>::channelType, ChannelType::eFloat);
    EXPECT_EQ(VertexValueTraits<glm::ivec4>::channelCount, 4u);
    EXPECT_EQ(VertexValueTraits<glm::ivec4>::channelType, ChannelType::eInt);
}

// -----------------------------------------------------------------------------
// Std430AlignTraits: std430 base alignment rules.
//   scalar -> N, vec2 -> 2N, vec3/vec4 -> 4N
// -----------------------------------------------------------------------------
TEST(MeshLayout, Std430Alignment) {
    EXPECT_EQ(Std430AlignTraits<float>::alignment, sizeof(float));
    EXPECT_EQ(Std430AlignTraits<glm::vec2>::alignment, 2 * sizeof(float));
    EXPECT_EQ(Std430AlignTraits<glm::vec3>::alignment, 4 * sizeof(float));
    EXPECT_EQ(Std430AlignTraits<glm::vec4>::alignment, 4 * sizeof(float));
}

// -----------------------------------------------------------------------------
// ParamVertex storage: stride and per-attribute offsets follow std430 alignment.
// For <Position(vec3), Normal(vec3), UV(vec2)>:
//   Position @ 0, Normal @ 16 (vec3 aligns to 16), UV @ 32, stride padded to 48.
// -----------------------------------------------------------------------------
using PNU = ParamVertex<Position, Normal, UV>;

TEST(MeshLayout, ParamVertexAttributeCount) {
    EXPECT_EQ(PNU::kAttributeCount, 3u);
}

TEST(MeshLayout, ParamVertexOffsets) {
    EXPECT_EQ(PNU::OffsetOf<0>(), 0u);
    EXPECT_EQ(PNU::OffsetOf<1>(), 16u);
    EXPECT_EQ(PNU::OffsetOf<2>(), 32u);
}

TEST(MeshLayout, ParamVertexStrideMatchesStorageSize) {
    EXPECT_EQ(PNU::kStride, sizeof(PNU::Storage));
    EXPECT_EQ(PNU::kStride, 48u); // 32 (UV offset) + 8, padded up to 16-alignment
}

TEST(MeshLayout, ParamVertexConstructAndGet) {
    PNU v(glm::vec3(1, 2, 3), glm::vec3(0, 1, 0), glm::vec2(0.5f, 0.25f));
    EXPECT_EQ(v.get<0>(), glm::vec3(1, 2, 3));
    EXPECT_EQ(v.get<1>(), glm::vec3(0, 1, 0));
    EXPECT_EQ(v.get<2>(), glm::vec2(0.5f, 0.25f));
}

// -----------------------------------------------------------------------------
// FindPositionAttribute: locates the PositionAttribute-marked attribute across
// one or more vertex streams and reports its binding/offset/format.
// -----------------------------------------------------------------------------
TEST(MeshLayout, FindPositionInSingleStream) {
    auto pos = FindPositionAttribute<PNU>();
    ASSERT_TRUE(pos.has_value());
    EXPECT_EQ(pos->binding, 0u);
    EXPECT_EQ(pos->offset, 0u); // Position is first
    EXPECT_EQ(pos->channelCount, 3u);
    EXPECT_EQ(pos->channelType, ChannelType::eFloat);
}

TEST(MeshLayout, FindPositionWhenNotFirst) {
    using NPU = ParamVertex<Normal, Position, UV>;
    auto pos = FindPositionAttribute<NPU>();
    ASSERT_TRUE(pos.has_value());
    EXPECT_EQ(pos->binding, 0u);
    EXPECT_EQ(pos->offset, NPU::OffsetOf<1>()); // Position is the 2nd attribute
}

TEST(MeshLayout, FindPositionAcrossMultipleStreams) {
    using PosStream = ParamVertex<Position>;
    using UvStream  = ParamVertex<UV>;
    // Position lives in the first stream -> binding 0.
    auto a = FindPositionAttribute<PosStream, UvStream>();
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ(a->binding, 0u);
    // Position lives in the second stream -> binding 1.
    auto b = FindPositionAttribute<UvStream, PosStream>();
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(b->binding, 1u);
}

TEST(MeshLayout, FindPositionReturnsNulloptWhenAbsent) {
    using NoPos = ParamVertex<Normal, UV, Color>;
    EXPECT_FALSE(FindPositionAttribute<NoPos>().has_value());
}

} // namespace
