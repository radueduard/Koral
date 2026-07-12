// Unit tests for value helpers in structs.h — currently sizeofChannelType(),
// plus guards on the enum <-> byte-size contract that vertex layout math relies on.

#include <gtest/gtest.h>

#include "structs.h"

using namespace kor;

namespace {

TEST(Structs, SizeofChannelTypeMatchesScalarSizes) {
    EXPECT_EQ(sizeofChannelType(ChannelType::eFloat),  sizeof(float));
    EXPECT_EQ(sizeofChannelType(ChannelType::eInt),    sizeof(int));
    EXPECT_EQ(sizeofChannelType(ChannelType::eUInt),   sizeof(unsigned int));
    EXPECT_EQ(sizeofChannelType(ChannelType::eShort),  sizeof(short));
    EXPECT_EQ(sizeofChannelType(ChannelType::eUShort), sizeof(unsigned short));
    EXPECT_EQ(sizeofChannelType(ChannelType::eByte),   sizeof(char));
    EXPECT_EQ(sizeofChannelType(ChannelType::eUByte),  sizeof(unsigned char));
    EXPECT_EQ(sizeofChannelType(ChannelType::eDouble), sizeof(double));
}

TEST(Structs, SizeofChannelTypeThrowsOnInvalid) {
    const auto bogus = static_cast<ChannelType>(0xFF);
    EXPECT_THROW(sizeofChannelType(bogus), std::runtime_error);
}

// Default-constructed pipeline state structs should carry the documented
// defaults; these back-stop accidental changes to the "sensible defaults"
// contract the public API promises.
TEST(Structs, RasterizationStateDefaults) {
    RasterizationState r{};
    EXPECT_EQ(r.polygonMode, PolygonMode::eFill);
    EXPECT_EQ(r.frontFace, FrontFace::eCounterClockwise);
    EXPECT_FALSE(r.depthClampEnable);
    EXPECT_EQ(r.cullMode.value(), 0u); // no faces culled by default
    EXPECT_FLOAT_EQ(r.lineWidth, 1.0f);
}

TEST(Structs, DepthStencilStateDefaults) {
    DepthStencilState d{};
    EXPECT_TRUE(d.depthTestEnable);
    EXPECT_TRUE(d.depthWriteEnable);
    EXPECT_EQ(d.depthCompareOp, CompareOp::eLess);
    EXPECT_FALSE(d.stencilEnable);
    EXPECT_FLOAT_EQ(d.minDepth, 0.0f);
    EXPECT_FLOAT_EQ(d.maxDepth, 1.0f);
}

TEST(Structs, InputAssemblyDefaults) {
    InputAssemblyState ia{};
    EXPECT_EQ(ia.topology, Topology::eTriangleList);
    EXPECT_FALSE(ia.primitiveRestartEnable);
}

TEST(Structs, MultisampleDefaults) {
    MultisampleState ms{};
    EXPECT_EQ(ms.sampleCount, SampleCount::e1);
    EXPECT_FALSE(ms.sampleShadingEnable);
}

} // namespace
