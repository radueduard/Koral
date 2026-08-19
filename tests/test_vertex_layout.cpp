// Unit tests for kor::VertexLayout::resolve — matching a vertex format against the inputs a
// vertex shader declares. This is the engine half of the mesh split: the layout says what the
// vertices hold, the shader says what it wants, and this decides which bytes reach which location.
// No shader is compiled here; the reflection is handed in directly.

#include <gtest/gtest.h>

#include <vector>

#include <error.h>
#include <vertexLayout.h>

using kor::ChannelType;
using kor::VertexLayout;

namespace {

// position/normal/uv in one binding, the way a mesh module would describe them.
VertexLayout threeAttributes()
{
    VertexLayout layout;
    layout.bindings.push_back({ .binding = 0, .stride = 48 });
    layout.attributes.push_back({ .semantic = "POSITION", .semanticNamespace = "mesh",
                                  .binding = 0, .offset = 0,  .channelType = ChannelType::eFloat, .channelCount = 3 });
    layout.attributes.push_back({ .semantic = "NORMAL",   .semanticNamespace = "mesh",
                                  .binding = 0, .offset = 16, .channelType = ChannelType::eFloat, .channelCount = 3 });
    layout.attributes.push_back({ .semantic = "UV",       .semanticNamespace = "mesh",
                                  .binding = 0, .offset = 32, .channelType = ChannelType::eFloat, .channelCount = 2 });
    return layout;
}

// -----------------------------------------------------------------------------
// A shader that annotates nothing: matched by declaration order, which is what a
// layout meant before semantics existed.
// -----------------------------------------------------------------------------
TEST(VertexLayoutResolve, UnannotatedShaderIsMatchedByOrder) {
    const auto layout = threeAttributes();
    const std::vector<VertexLayout::ShaderInput> inputs{
        { .location = 0, .name = "inPosition" },
        { .location = 1, .name = "inNormal" },
    };

    const auto resolved = layout.resolve(inputs);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;

    // Every attribute is described, whether the shader reads it or not.
    ASSERT_EQ(resolved->size(), 3u);
    EXPECT_EQ((*resolved)[0].location, 0u);
    EXPECT_EQ((*resolved)[1].location, 1u);
    EXPECT_EQ((*resolved)[1].offset, 16u);
    EXPECT_EQ((*resolved)[2].location, 2u);
}

// -----------------------------------------------------------------------------
// A shader that names what it wants: order and completeness are its own business.
// -----------------------------------------------------------------------------
TEST(VertexLayoutResolve, SemanticsBeatDeclarationOrder) {
    const auto layout = threeAttributes();
    const std::vector<VertexLayout::ShaderInput> inputs{
        { .location = 0, .name = "uv",  .semanticNamespace = "mesh", .semantic = "UV" },
        { .location = 1, .name = "pos", .semanticNamespace = "mesh", .semantic = "POSITION" },
    };

    const auto resolved = layout.resolve(inputs);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;

    // Only what the shader asked for, each fed from the right offset.
    ASSERT_EQ(resolved->size(), 2u);
    EXPECT_EQ((*resolved)[0].location, 0u);
    EXPECT_EQ((*resolved)[0].offset, 32u);          // the UV
    EXPECT_EQ((*resolved)[0].channelCount, 2u);
    EXPECT_EQ((*resolved)[1].location, 1u);
    EXPECT_EQ((*resolved)[1].offset, 0u);           // the position
}

TEST(VertexLayoutResolve, SemanticsAreCaseInsensitive) {
    const auto layout = threeAttributes();
    const std::vector<VertexLayout::ShaderInput> inputs{
        { .location = 0, .name = "p", .semantic = "position" },
    };

    const auto resolved = layout.resolve(inputs);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
    ASSERT_EQ(resolved->size(), 1u);
    EXPECT_EQ((*resolved)[0].offset, 0u);
}

TEST(VertexLayoutResolve, ABareSemanticMatchesAnyVocabulary) {
    // Slang's `: POSITION` names no module. It still matches a layout that does.
    const auto layout = threeAttributes();
    const std::vector<VertexLayout::ShaderInput> inputs{
        { .location = 0, .name = "p", .semantic = "POSITION" },
    };

    EXPECT_TRUE(layout.resolve(inputs).has_value());
}

TEST(VertexLayoutResolve, AnotherModulesSemanticIsNotAnswered) {
    const auto layout = threeAttributes();
    const std::vector<VertexLayout::ShaderInput> inputs{
        { .location = 0, .name = "p", .semanticNamespace = "sprite", .semantic = "POSITION" },
    };

    const auto resolved = layout.resolve(inputs);
    ASSERT_FALSE(resolved.has_value());
    EXPECT_EQ(resolved.error().code, kor::ErrorCode::eVertexLayoutMismatch);
    EXPECT_NE(resolved.error().message.find("sprite(POSITION)"), std::string::npos);
}

TEST(VertexLayoutResolve, AnUnknownSemanticNamesWhatIsAvailable) {
    const auto layout = threeAttributes();
    const std::vector<VertexLayout::ShaderInput> inputs{
        { .location = 0, .name = "t", .semanticNamespace = "mesh", .semantic = "TANGENT" },
    };

    const auto resolved = layout.resolve(inputs);
    ASSERT_FALSE(resolved.has_value());
    EXPECT_EQ(resolved.error().code, kor::ErrorCode::eVertexLayoutMismatch);
    // The message has to be enough to fix the shader with: what was asked for, and what there is.
    EXPECT_NE(resolved.error().message.find("TANGENT"), std::string::npos);
    EXPECT_NE(resolved.error().message.find("mesh(UV)"), std::string::npos);
    EXPECT_NE(resolved.error().message.find("'t'"), std::string::npos);
}

TEST(VertexLayoutResolve, AnUnannotatedInputAmongAnnotatedOnesFallsBackToItsLocation) {
    const auto layout = threeAttributes();
    const std::vector<VertexLayout::ShaderInput> inputs{
        { .location = 0, .name = "p", .semantic = "POSITION" },
        { .location = 1, .name = "whatever" },   // no semantic: takes attribute 1
    };

    const auto resolved = layout.resolve(inputs);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
    ASSERT_EQ(resolved->size(), 2u);
    EXPECT_EQ((*resolved)[1].offset, 16u);
}

TEST(VertexLayoutResolve, AnUnannotatedInputPastTheLayoutIsAnError) {
    const auto layout = threeAttributes();
    const std::vector<VertexLayout::ShaderInput> inputs{
        { .location = 0, .name = "p", .semantic = "POSITION" },
        { .location = 7, .name = "stray" },
    };

    const auto resolved = layout.resolve(inputs);
    ASSERT_FALSE(resolved.has_value());
    EXPECT_EQ(resolved.error().code, kor::ErrorCode::eVertexLayoutMismatch);
}

// -----------------------------------------------------------------------------
// A layout that names no semantics at all, only the locations its attributes are
// read at. Nothing is annotated on either side.
// -----------------------------------------------------------------------------

// position at location 3 and colour at location 1 — deliberately neither in order nor contiguous,
// so nothing about the result can be the old positional fallback in disguise.
VertexLayout locationsOnly()
{
    VertexLayout layout;
    layout.bindings.push_back({ .binding = 0, .stride = 24 });
    layout.attributes.push_back(VertexLayout::Attribute::AtLocation(3, 0, 0,  ChannelType::eFloat, 3));
    layout.attributes.push_back(VertexLayout::Attribute::AtLocation(1, 0, 12, ChannelType::eFloat, 3));
    return layout;
}

TEST(VertexLayoutResolve, ExplicitLocationsBeatDeclarationOrder) {
    const auto layout = locationsOnly();
    const std::vector<VertexLayout::ShaderInput> inputs{
        { .location = 3, .name = "inPosition" },
        { .location = 1, .name = "inColor" },
    };

    const auto resolved = layout.resolve(inputs);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;

    ASSERT_EQ(resolved->size(), 2u);
    EXPECT_EQ((*resolved)[0].location, 3u);
    EXPECT_EQ((*resolved)[0].offset, 0u);
    EXPECT_EQ((*resolved)[1].location, 1u);
    EXPECT_EQ((*resolved)[1].offset, 12u);
}

TEST(VertexLayoutResolve, AnAttributeWithNoLocationStillFallsBackToItsPlace) {
    // Mixed on purpose: the second attribute names nothing, so it is location 1 by position.
    VertexLayout layout;
    layout.bindings.push_back({ .binding = 0, .stride = 24 });
    layout.attributes.push_back(VertexLayout::Attribute::AtLocation(5, 0, 0, ChannelType::eFloat, 3));
    layout.attributes.push_back({ .binding = 0, .offset = 12, .channelType = ChannelType::eFloat, .channelCount = 3 });

    const auto resolved = layout.resolve(std::vector<VertexLayout::ShaderInput>{
        { .location = 5, .name = "a" }, { .location = 1, .name = "b" },
    });
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
    EXPECT_EQ((*resolved)[0].location, 5u);
    EXPECT_EQ((*resolved)[1].location, 1u);
}

TEST(VertexLayoutResolve, TwoAttributesAtOneLocationIsAnError) {
    // The trap the mixed form sets: attribute 1 falls back to location 1, which attribute 0 took.
    VertexLayout layout;
    layout.bindings.push_back({ .binding = 0, .stride = 24 });
    layout.attributes.push_back(VertexLayout::Attribute::AtLocation(1, 0, 0, ChannelType::eFloat, 3));
    layout.attributes.push_back({ .binding = 0, .offset = 12, .channelType = ChannelType::eFloat, .channelCount = 3 });

    const auto resolved = layout.resolve(std::vector<VertexLayout::ShaderInput>{
        { .location = 0, .name = "a" }, { .location = 1, .name = "b" },
    });
    ASSERT_FALSE(resolved.has_value());
    EXPECT_EQ(resolved.error().code, kor::ErrorCode::eVertexLayoutMismatch);
    EXPECT_NE(resolved.error().message.find("location 1"), std::string::npos);
}

TEST(VertexLayoutResolve, AnnotatedInputsFindTheAttributeThatNamesTheirLocation) {
    // A shader where something else is annotated, so the semantic path is taken: an input that
    // carries no semantic is still answered by the attribute claiming its location.
    VertexLayout layout;
    layout.bindings.push_back({ .binding = 0, .stride = 24 });
    layout.attributes.push_back({ .semantic = "POSITION", .semanticNamespace = "mesh", .binding = 0, .offset = 0,
                                  .channelType = ChannelType::eFloat, .channelCount = 3 });
    layout.attributes.push_back(VertexLayout::Attribute::AtLocation(4, 0, 12, ChannelType::eFloat, 3));

    const auto resolved = layout.resolve(std::vector<VertexLayout::ShaderInput>{
        { .location = 0, .name = "p", .semanticNamespace = "mesh", .semantic = "POSITION" },
        { .location = 4, .name = "extra" },
    });
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
    ASSERT_EQ(resolved->size(), 2u);
    EXPECT_EQ((*resolved)[1].location, 4u);
    EXPECT_EQ((*resolved)[1].offset, 12u);   // not attributes[4], which does not exist
}

TEST(VertexLayoutResolve, ASemanticAskedOfALocationOnlyLayoutSaysSo) {
    const auto layout = locationsOnly();
    const std::vector<VertexLayout::ShaderInput> inputs{
        { .location = 0, .name = "p", .semanticNamespace = "mesh", .semantic = "POSITION" },
    };

    const auto resolved = layout.resolve(inputs);
    ASSERT_FALSE(resolved.has_value());
    EXPECT_EQ(resolved.error().code, kor::ErrorCode::eVertexLayoutMismatch);
    // Naming no semantics is a different problem from missing one, and reads differently.
    EXPECT_NE(resolved.error().message.find("names no semantics"), std::string::npos);
}

TEST(VertexLayoutResolve, AnEmptyLayoutResolvesToNothing) {
    const VertexLayout layout;
    const std::vector<VertexLayout::ShaderInput> inputs;

    const auto resolved = layout.resolve(inputs);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_TRUE(resolved->empty());
}

// -----------------------------------------------------------------------------
// The default layout: the first format described wins, and later ones do not
// silently replace it.
// -----------------------------------------------------------------------------
TEST(VertexLayoutDefault, FirstOneWins) {
    VertexLayout first;
    first.bindings.push_back({ .binding = 0, .stride = 12 });
    first.attributes.push_back({ .semantic = "POSITION", .binding = 0, .offset = 0,
                                 .channelType = ChannelType::eFloat, .channelCount = 3 });
    VertexLayout::SetDefault(first);

    VertexLayout second;
    second.bindings.push_back({ .binding = 0, .stride = 999 });
    VertexLayout::SetDefault(second);

    ASSERT_EQ(VertexLayout::Default().bindings.size(), 1u);
    EXPECT_EQ(VertexLayout::Default().bindings[0].stride, 12u);
}

} // namespace
