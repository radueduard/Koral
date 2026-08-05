// Unit tests for the mesh module's compile-time vertex layout machinery (koralMesh.h):
// VertexValueTraits, Std430AlignTraits, ParamVertex storage/stride/offset math, and the runtime
// kor::VertexLayout that MakeVertexLayout builds out of them — including which attribute a ray
// tracer reads the position from. This is pure CPU reflection math — no GPU needed.
// (ParamMesh::Create and friends touch the GPU and are intentionally not used.)

#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include <koralMesh.h>

using namespace kmesh;
using kor::ChannelType;

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
// MakeVertexLayout: the runtime description a pipeline is matched against —
// bindings and strides, offsets, channel formats, and the semantic each
// attribute answers to.
// -----------------------------------------------------------------------------
TEST(MeshLayout, LayoutDescribesEveryAttribute) {
    const auto layout = MakeVertexLayout<PNU>();

    ASSERT_EQ(layout.bindings.size(), 1u);
    EXPECT_EQ(layout.bindings[0].binding, 0u);
    EXPECT_EQ(layout.bindings[0].stride, PNU::kStride);

    ASSERT_EQ(layout.attributes.size(), 3u);
    EXPECT_EQ(layout.attributes[0].semantic, "POSITION");
    EXPECT_EQ(layout.attributes[1].semantic, "NORMAL");
    EXPECT_EQ(layout.attributes[2].semantic, "UV");

    // The vocabulary is the module's, and every attribute says so.
    for (const auto& attribute : layout.attributes)
        EXPECT_EQ(attribute.semanticNamespace, semantics::kNamespace);

    EXPECT_EQ(layout.attributes[1].offset, PNU::OffsetOf<1>());
    EXPECT_EQ(layout.attributes[2].channelCount, 2u);
    EXPECT_EQ(layout.attributes[2].channelType, ChannelType::eFloat);
}

TEST(MeshLayout, IndexedAttributeSemanticCarriesItsChannel) {
    using TwoUVs = ParamVertex<Position, IndexedAttribute<UV, 0>, IndexedAttribute<UV, 1>>;
    const auto layout = MakeVertexLayout<TwoUVs>();

    ASSERT_EQ(layout.attributes.size(), 3u);
    EXPECT_EQ(layout.attributes[1].semantic, "UV0");
    EXPECT_EQ(layout.attributes[2].semantic, "UV1");
}

TEST(MeshLayout, StreamsBecomeSeparateBindings) {
    using PosStream = ParamVertex<Position>;
    using UvStream  = ParamVertex<UV>;
    const auto layout = MakeVertexLayout<PosStream, UvStream>();

    ASSERT_EQ(layout.bindings.size(), 2u);
    ASSERT_EQ(layout.attributes.size(), 2u);
    EXPECT_EQ(layout.attributes[0].binding, 0u);
    EXPECT_EQ(layout.attributes[1].binding, 1u);
}

TEST(MeshLayout, BareValueStreamHasOneUnnamedAttribute) {
    // A heap of plain glm::vec3 positions: one attribute, no semantic, so it can only be
    // matched by declaration order.
    const auto layout = MakeVertexLayout<glm::vec3, glm::vec2>();

    ASSERT_EQ(layout.bindings.size(), 2u);
    EXPECT_EQ(layout.bindings[0].stride, sizeof(glm::vec3));
    ASSERT_EQ(layout.attributes.size(), 2u);
    EXPECT_TRUE(layout.attributes[0].semantic.empty());
    EXPECT_EQ(layout.attributes[0].channelCount, 3u);
    EXPECT_EQ(layout.attributes[1].channelCount, 2u);
}

// -----------------------------------------------------------------------------
// The position a ray tracer reads: the first attribute unless the format marks
// another one with PositionAttribute.
// -----------------------------------------------------------------------------
TEST(MeshLayout, PositionDefaultsToTheFirstAttribute) {
    const auto layout = MakeVertexLayout<ParamVertex<Normal, UV>>();

    // Nothing carries PositionAttribute, so nothing is recorded — and the first attribute is
    // what gets read.
    EXPECT_FALSE(layout.positionAttribute.has_value());
    const auto position = layout.position();
    ASSERT_TRUE(position.has_value());
    EXPECT_EQ(position->binding, 0u);
    EXPECT_EQ(position->offset, 0u);
}

TEST(MeshLayout, PositionIsFoundWhenNotFirst) {
    using NPU = ParamVertex<Normal, Position, UV>;
    const auto layout = MakeVertexLayout<NPU>();

    ASSERT_TRUE(layout.positionAttribute.has_value());
    EXPECT_EQ(*layout.positionAttribute, 1u);

    const auto position = layout.position();
    ASSERT_TRUE(position.has_value());
    EXPECT_EQ(position->offset, NPU::OffsetOf<1>());
    EXPECT_EQ(position->channelCount, 3u);
    EXPECT_EQ(position->channelType, ChannelType::eFloat);
}

TEST(MeshLayout, PositionIsFoundInALaterStream) {
    using PosStream = ParamVertex<Position>;
    using UvStream  = ParamVertex<UV>;
    const auto layout = MakeVertexLayout<UvStream, PosStream>();

    const auto position = layout.position();
    ASSERT_TRUE(position.has_value());
    EXPECT_EQ(position->binding, 1u);
}

TEST(MeshLayout, PositionOfAnEmptyLayoutIsNothing) {
    const kor::VertexLayout layout;
    EXPECT_TRUE(layout.empty());
    EXPECT_FALSE(layout.position().has_value());
}

} // namespace
