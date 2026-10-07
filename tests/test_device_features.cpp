// kor::Feature: the names koral.json and messages use, and the check that refuses a GPU lacking a required one,
// naming who required it. No device here: what the device is made with is in the GPU tests.

#include <gtest/gtest.h>

#include <deviceFeatures.h>
#include <projectConfig.h>

using kor::Feature;
using kor::Flags;

TEST(DeviceFeatures, NamesGoBothWays) {
    EXPECT_STREQ(kor::FeatureName(Feature::eAtomicFloat32), "AtomicFloat32");
    EXPECT_EQ(kor::FeatureNamed("AtomicFloat32"), Feature::eAtomicFloat32);
    EXPECT_EQ(kor::FeatureNamed("eatomicfloat32"), Feature::eAtomicFloat32) << "the enumerator's spelling, in any case";
    EXPECT_EQ(kor::FeatureNamed("Nope"), Feature::eNone);
    EXPECT_EQ(kor::FeatureNames(Feature::eShaderInt64 | Feature::eAtomicFloat32), "ShaderInt64, AtomicFloat32");
}

TEST(DeviceFeatures, AGpuLackingARequiredFeatureIsRefusedNamingWhoRequiredIt) {
    const std::vector<kor::FeatureRequest> requests {
        { .by = "physics.cpp", .required = Feature::eAtomicFloat32 | Feature::eShaderInt64 },
        { .by = "koral.json", .required = Feature::eShaderInt64, .optional = Feature::eCooperativeMatrix },
    };
    EXPECT_TRUE(kor::detail::MissingRequired(Feature::eAtomicFloat32 | Feature::eShaderInt64, requests).empty());
    EXPECT_TRUE(kor::detail::MissingRequired(Feature::eAtomicFloat32 | Feature::eShaderInt64 | Feature::eWideLines, requests).empty())
        << "an optional one it lacks is no reason to refuse it";

    const std::string missing = kor::detail::MissingRequired(Flags<Feature>(Feature::eShaderInt64), requests);
    EXPECT_NE(missing.find("AtomicFloat32 (required by physics.cpp)"), std::string::npos) << missing;
    EXPECT_EQ(missing.find("koral.json"), std::string::npos) << "what it has is nobody's problem: " << missing;
}

TEST(DeviceFeatures, KoralJsonAsksForThemByName) {
    kor::ProjectConfig config;
    ASSERT_TRUE(config.Merge(R"({ "features": { "required": ["AtomicFloat32"], "optional": ["CooperativeMatrix", "eShaderInt64"] } })", ".").has_value());
    EXPECT_EQ(config.requiredFeatures, Flags<Feature>(Feature::eAtomicFloat32));
    EXPECT_EQ(config.optionalFeatures, Feature::eCooperativeMatrix | Feature::eShaderInt64);

    const auto refused = config.Merge(R"({ "features": { "required": ["Teleportation"] } })", ".");
    ASSERT_FALSE(refused.has_value());
    EXPECT_NE(refused.error().message.find("Teleportation"), std::string::npos);
}
