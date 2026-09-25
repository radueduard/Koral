// Unit tests for the frame graph compiler (src/core/frameGraphCompiler.h): ordering, culling,
// levels, lifetimes, and the declarations it must refuse. Pure logic; no device.

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "src/core/frameGraphCompiler.h"

using namespace kor;
using namespace kor::graph;

namespace {

PassDecl pass(std::string name, std::vector<Use> uses, const bool sideEffect = false) {
    return PassDecl{.name = std::move(name), .uses = std::move(uses), .sideEffect = sideEffect};
}
Use create(std::string r) { return {std::move(r), Access::eCreate}; }
Use read(std::string r)   { return {std::move(r), Access::eRead}; }
Use write(std::string r)  { return {std::move(r), Access::eWrite}; }
Use readPrevious(std::string r) { return {std::move(r), Access::eReadPrevious}; }

std::vector<std::string> names(const std::vector<PassDecl>& passes, const std::vector<std::size_t>& indices) {
    std::vector<std::string> out;
    for (const auto i : indices) out.push_back(passes[i].name);
    return out;
}

// The shape this is for: a G-buffer, two effects that only read it, and a composite to the screen.
std::vector<PassDecl> deferredFrame() {
    return {
        pass("gbuffer",   {create("depth"), create("normal"), create("albedo")}),
        pass("ssao",      {read("depth"), read("normal"), create("ao")}),
        pass("ssr",       {read("depth"), read("normal"), read("albedo"), create("reflections")}),
        pass("composite", {read("albedo"), read("ao"), read("reflections"), write("screen")}),
    };
}

TEST(FrameGraphCompiler, OrdersProducersBeforeConsumers) {
    const auto passes = deferredFrame();
    const auto compiled = compile(passes, {"screen"});
    ASSERT_TRUE(compiled) << compiled.error().message;
    EXPECT_EQ(names(passes, compiled->order), (std::vector<std::string>{"gbuffer", "ssao", "ssr", "composite"}));
    EXPECT_TRUE(compiled->culled.empty());
}

TEST(FrameGraphCompiler, DeclarationOrderDoesNotMatter) {
    auto passes = deferredFrame();
    std::ranges::reverse(passes);  // composite, ssr, ssao, gbuffer
    const auto compiled = compile(passes, {"screen"});
    ASSERT_TRUE(compiled) << compiled.error().message;
    // Dependencies decide; among the free (ssr, ssao) declaration order does — ssr was declared first.
    EXPECT_EQ(names(passes, compiled->order), (std::vector<std::string>{"gbuffer", "ssr", "ssao", "composite"}));
}

TEST(FrameGraphCompiler, IndependentPassesShareALevel) {
    const auto passes = deferredFrame();
    const auto compiled = compile(passes, {"screen"});
    ASSERT_TRUE(compiled);
    EXPECT_EQ(compiled->level, (std::vector<std::uint32_t>{0, 1, 1, 2}));
}

TEST(FrameGraphCompiler, CullsWhatNothingNeeds) {
    auto passes = deferredFrame();
    passes.push_back(pass("debugView", {read("depth"), create("debugImage")}));  // nobody reads debugImage
    passes[1] = pass("ssao", {read("depth"), read("normal"), create("ao")});
    passes[3] = pass("composite", {read("albedo"), read("reflections"), write("screen")});  // no longer reads ao
    const auto compiled = compile(passes, {"screen"});
    ASSERT_TRUE(compiled) << compiled.error().message;
    EXPECT_EQ(names(passes, compiled->culled), (std::vector<std::string>{"ssao", "debugView"}));
    EXPECT_EQ(names(passes, compiled->order), (std::vector<std::string>{"gbuffer", "ssr", "composite"}));
}

TEST(FrameGraphCompiler, ASideEffectKeepsAPassNobodyReads) {
    const std::vector<PassDecl> passes{
        pass("capture", {create("frame")}),
        pass("save", {read("frame")}, /*sideEffect=*/true),
    };
    const auto compiled = compile(passes);
    ASSERT_TRUE(compiled);
    EXPECT_EQ(names(passes, compiled->order), (std::vector<std::string>{"capture", "save"}));
}

TEST(FrameGraphCompiler, NothingVisibleMeansNothingRuns) {
    const std::vector<PassDecl> passes{pass("a", {create("x")}), pass("b", {read("x")})};
    const auto compiled = compile(passes);
    ASSERT_TRUE(compiled);
    EXPECT_TRUE(compiled->order.empty());
    EXPECT_EQ(compiled->culled.size(), 2u);
}

TEST(FrameGraphCompiler, WritersRunInDeclarationOrderAndReadersAfterAll) {
    const std::vector<PassDecl> passes{
        pass("lighting", {create("hdr")}),
        pass("tonemap",  {read("hdr"), write("screen")}),  // declared before the writers below
        pass("sky",      {write("hdr")}),
        pass("particles", {write("hdr")}),
    };
    const auto compiled = compile(passes, {"screen"});
    ASSERT_TRUE(compiled) << compiled.error().message;
    EXPECT_EQ(names(passes, compiled->order), (std::vector<std::string>{"lighting", "sky", "particles", "tonemap"}));
    EXPECT_EQ(compiled->level, (std::vector<std::uint32_t>{0, 1, 2, 3}));
}

TEST(FrameGraphCompiler, ReportsLifetimesOfCreatedResourcesOnly) {
    const auto passes = deferredFrame();
    const auto compiled = compile(passes, {"screen"});
    ASSERT_TRUE(compiled);
    // Positions in the order: gbuffer 0, ssao 1, ssr 2, composite 3.
    std::map<std::string, std::pair<std::size_t, std::size_t>> spans;
    for (const auto& l : compiled->lifetimes) spans[l.resource] = {l.first, l.last};
    EXPECT_EQ(spans.size(), 5u) << "the imported screen is not the graph's to allocate";
    EXPECT_EQ(spans["depth"], (std::pair<std::size_t, std::size_t>{0, 2}));
    EXPECT_EQ(spans["albedo"], (std::pair<std::size_t, std::size_t>{0, 3}));
    EXPECT_EQ(spans["ao"], (std::pair<std::size_t, std::size_t>{1, 3}));
    EXPECT_EQ(spans["reflections"], (std::pair<std::size_t, std::size_t>{2, 3}));
}

TEST(FrameGraphCompiler, AResourceNamedTwiceByOnePassCountsAtItsStrongestUse) {
    const std::vector<PassDecl> passes{pass("blur", {read("img"), create("img")}), pass("show", {read("img"), write("screen")})};
    const auto compiled = compile(passes, {"screen"});
    ASSERT_TRUE(compiled) << compiled.error().message;
    EXPECT_EQ(names(passes, compiled->order), (std::vector<std::string>{"blur", "show"}));
}

TEST(FrameGraphCompiler, RefusesAResourceNothingMakes) {
    const std::vector<PassDecl> passes{pass("composite", {read("ao"), write("screen")})};
    const auto compiled = compile(passes, {"screen"});
    ASSERT_FALSE(compiled);
    EXPECT_EQ(compiled.error().code, ErrorCode::eFrameGraphInvalid);
    EXPECT_NE(compiled.error().message.find("'composite' uses 'ao'"), std::string::npos) << compiled.error().message;
}

TEST(FrameGraphCompiler, RefusesAResourceCreatedTwice) {
    const std::vector<PassDecl> passes{pass("a", {create("x")}), pass("b", {create("x")})};
    const auto compiled = compile(passes);
    ASSERT_FALSE(compiled);
    EXPECT_NE(compiled.error().message.find("created by both 'a' and 'b'"), std::string::npos) << compiled.error().message;
}

TEST(FrameGraphCompiler, RefusesCreatingAnImportedResource) {
    const std::vector<PassDecl> passes{pass("a", {create("screen")})};
    const auto compiled = compile(passes, {"screen"});
    ASSERT_FALSE(compiled);
    EXPECT_NE(compiled.error().message.find("'screen' is imported"), std::string::npos) << compiled.error().message;
}

TEST(FrameGraphCompiler, ReportsEveryProblemAtOnce) {
    const std::vector<PassDecl> passes{pass("a", {create("x"), read("missing")}), pass("b", {create("x")})};
    const auto compiled = compile(passes);
    ASSERT_FALSE(compiled);
    EXPECT_NE(compiled.error().message.find("created by both"), std::string::npos);
    EXPECT_NE(compiled.error().message.find("'missing'"), std::string::npos);
}

TEST(FrameGraphCompiler, NamesTheCycleWhenPassesDependOnEachOther) {
    const std::vector<PassDecl> passes{
        pass("a", {create("x"), read("y"), write("screen")}),
        pass("b", {create("y"), read("x")}),
    };
    const auto compiled = compile(passes, {"screen"});
    ASSERT_FALSE(compiled);
    EXPECT_EQ(compiled.error().code, ErrorCode::eFrameGraphInvalid);
    const auto& message = compiled.error().message;
    EXPECT_NE(message.find("circle"), std::string::npos) << message;
    EXPECT_NE(message.find("-(x)->"), std::string::npos) << message;
    EXPECT_NE(message.find("-(y)->"), std::string::npos) << message;
}

Use consume(std::string r, std::string as) { return {std::move(r), Access::eConsume, std::move(as)}; }

// The GPU-driven cull: shadows and the depth pre-pass draw from the full list, the cull rewrites it
// in place from what the pre-pass saw, and the forward pass draws what survived. One buffer, two
// versions — which "readers after all writers" alone cannot say without a cycle.
std::vector<PassDecl> culledFrame() {
    return {
        pass("forward", {read("draws.culled"), read("shadowMap"), create("hdr"), write("screen")}),
        pass("cull",    {read("visibility"), consume("draws", "draws.culled")}),
        pass("shadows", {read("draws"), create("shadowMap")}),
        pass("prepass", {read("draws"), create("visibility")}),
        pass("reset",   {create("draws")}),
    };
}

TEST(FrameGraphCompiler, AConsumerRunsAfterEveryReaderOfTheOldVersion) {
    const auto passes = culledFrame();
    const auto compiled = compile(passes, {"screen"});
    ASSERT_TRUE(compiled) << compiled.error().message;
    EXPECT_EQ(names(passes, compiled->order),
              (std::vector<std::string>{"reset", "shadows", "prepass", "cull", "forward"}));
    EXPECT_EQ(compiled->aliases.at("draws.culled"), "draws");
}

TEST(FrameGraphCompiler, BothVersionsShareOneLifetime) {
    const auto compiled = compile(culledFrame(), {"screen"});
    ASSERT_TRUE(compiled);
    const auto it = std::ranges::find(compiled->lifetimes, std::string("draws"), &CompiledGraph::Lifetime::resource);
    ASSERT_NE(it, compiled->lifetimes.end());
    EXPECT_EQ(it->first, 0u);
    EXPECT_EQ(it->last, 4u) << "the culled version is the same buffer, still in use by the forward pass";
    EXPECT_EQ(std::ranges::count(compiled->lifetimes, std::string("draws.culled"), &CompiledGraph::Lifetime::resource), 0)
        << "an alias is not a separate allocation";
}

TEST(FrameGraphCompiler, ConsumingAnImportedResourceIsAVisibleEffect) {
    const std::vector<PassDecl> passes{
        pass("cull", {consume("sceneDraws", "sceneDraws.culled")}),
    };
    const auto compiled = compile(passes, {"sceneDraws"});
    ASSERT_TRUE(compiled) << compiled.error().message;
    EXPECT_EQ(compiled->order.size(), 1u) << "it changes something outside the graph, so it is kept";
}

TEST(FrameGraphCompiler, RefusesTwoConsumersOfOneResource) {
    const std::vector<PassDecl> passes{
        pass("make", {create("x")}),
        pass("a", {consume("x", "x.a")}),
        pass("b", {consume("x", "x.b")}),
    };
    const auto compiled = compile(passes);
    ASSERT_FALSE(compiled);
    EXPECT_NE(compiled.error().message.find("consumed by both 'a' and 'b'"), std::string::npos) << compiled.error().message;
}

TEST(FrameGraphCompiler, RefusesAConsumeWithoutANewName) {
    const std::vector<PassDecl> passes{pass("make", {create("x")}), pass("a", {consume("x", "")})};
    const auto compiled = compile(passes);
    ASSERT_FALSE(compiled);
    EXPECT_NE(compiled.error().message.find("without giving the result a new name"), std::string::npos);
}

// ---- disabled passes -------------------------------------------------------------------------

std::vector<PassDecl> disable(std::vector<PassDecl> passes, const std::string& name) {
    for (auto& p : passes) if (p.name == name) p.enabled = false;
    return passes;
}

TEST(FrameGraphCompiler, ADisabledModifierIsLeftOutQuietly) {
    auto passes = deferredFrame();
    passes.insert(passes.begin() + 2, pass("blurAO", {write("ao")}));
    passes = disable(std::move(passes), "blurAO");
    const auto compiled = compile(passes, {"screen"});
    ASSERT_TRUE(compiled) << compiled.error().message;
    EXPECT_EQ(names(passes, compiled->order), (std::vector<std::string>{"gbuffer", "ssao", "ssr", "composite"}));
    EXPECT_EQ(names(passes, compiled->disabled), (std::vector<std::string>{"blurAO"}));
    EXPECT_TRUE(compiled->skipped.empty());
    EXPECT_TRUE(compiled->culled.empty()) << "disabled is not the same as culled";
}

TEST(FrameGraphCompiler, ADisabledConsumerPassesItsResourceThrough) {
    const auto passes = disable(culledFrame(), "cull");
    const auto compiled = compile(passes, {"screen"});
    ASSERT_TRUE(compiled) << compiled.error().message;
    // The forward pass now reads the draw list as it was; the visibility buffer went with the cull.
    EXPECT_EQ(names(passes, compiled->order), (std::vector<std::string>{"reset", "shadows", "forward"}));
    EXPECT_EQ(names(passes, compiled->culled), (std::vector<std::string>{"prepass"}));
    EXPECT_EQ(compiled->aliases.at("draws.culled"), "draws");
    EXPECT_TRUE(compiled->skipped.empty());
}

TEST(FrameGraphCompiler, PassThroughFollowsAChainOfConsumers) {
    std::vector<PassDecl> passes{
        pass("make",  {create("x")}),
        pass("a",     {consume("x", "x.a")}),
        pass("b",     {consume("x.a", "x.b")}),
        pass("use",   {read("x.b"), write("screen")}),
    };
    passes = disable(disable(std::move(passes), "a"), "b");
    const auto compiled = compile(passes, {"screen"});
    ASSERT_TRUE(compiled) << compiled.error().message;
    EXPECT_EQ(names(passes, compiled->order), (std::vector<std::string>{"make", "use"}));
    EXPECT_EQ(compiled->aliases.at("x.a"), "x");
    EXPECT_EQ(compiled->aliases.at("x.b"), "x");
}

TEST(FrameGraphCompiler, DisablingACreatorSkipsWhatNeedsItTransitively) {
    const auto passes = disable(deferredFrame(), "ssao");
    const auto compiled = compile(passes, {"screen"});
    ASSERT_TRUE(compiled) << compiled.error().message;
    // composite needs "ao"; without composite nothing reads "reflections", so ssr is culled.
    EXPECT_EQ(names(passes, compiled->order), (std::vector<std::string>{}));
    ASSERT_EQ(compiled->skipped.size(), 1u);
    EXPECT_EQ(passes[compiled->skipped[0].pass].name, "composite");
    EXPECT_EQ(compiled->skipped[0].resource, "ao");
    EXPECT_EQ(passes[compiled->skipped[0].source].name, "ssao");

    const auto chain = disable(culledFrame(), "reset");
    const auto all = compile(chain, {"screen"});
    ASSERT_TRUE(all) << all.error().message;
    EXPECT_TRUE(all->order.empty());
    EXPECT_EQ(all->skipped.size(), 4u) << "shadows, prepass, cull and forward all stand on the draw list";
    for (const auto& s : all->skipped) {
        if (chain[s.pass].name == "forward") {
            EXPECT_EQ(chain[s.source].name, "cull") << "blamed on the pass it actually needed";
        }
    }
}

// ---- previous-frame reads -----------------------------------------------------------------------

// Occlusion culling against last frame's depth: the cull runs before this frame's depth exists, and
// the draw that makes the depth needs the cull's list. Read as this frame's depth, that is a cycle.
TEST(FrameGraphCompiler, ReadingThePreviousFrameAddsNoOrdering) {
    const std::vector<PassDecl> passes{
        pass("draw", {read("list"), create("depth"), write("screen")}),
        pass("cull", {readPrevious("depth"), create("list")}),
    };
    const auto compiled = compile(passes, {"screen"});
    ASSERT_TRUE(compiled) << compiled.error().message;
    EXPECT_EQ(names(passes, compiled->order), (std::vector<std::string>{"cull", "draw"}));
    EXPECT_EQ(compiled->history, (std::vector<std::string>{"depth"}));

    const auto it = std::ranges::find(compiled->lifetimes, std::string("depth"), &CompiledGraph::Lifetime::resource);
    ASSERT_NE(it, compiled->lifetimes.end());
    EXPECT_EQ(it->first, 0u) << "read at the start of the frame, as last frame left it";
    EXPECT_EQ(it->last, 1u) << "copied for the next frame after the last pass";
}

TEST(FrameGraphCompiler, APreviousFrameReadKeepsItsProducerAlive) {
    const std::vector<PassDecl> passes{
        pass("make", {create("x")}),                            // nothing reads x this frame
        pass("use",  {readPrevious("x"), write("screen")}),
    };
    const auto compiled = compile(passes, {"screen"});
    ASSERT_TRUE(compiled) << compiled.error().message;
    EXPECT_EQ(names(passes, compiled->order), (std::vector<std::string>{"make", "use"}));
    EXPECT_TRUE(compiled->culled.empty()) << "next frame needs what 'make' leaves";
    EXPECT_EQ(compiled->history, (std::vector<std::string>{"x"}));
}

// Temporal accumulation: blend this frame into what the pass itself produced last frame.
TEST(FrameGraphCompiler, APassMayReadThePreviousVersionOfWhatItCreates) {
    const std::vector<PassDecl> passes{
        pass("scene",   {create("hdr")}),
        pass("taa",     {read("hdr"), readPrevious("taa"), create("taa")}),
        pass("present", {read("taa"), write("screen")}),
    };
    const auto compiled = compile(passes, {"screen"});
    ASSERT_TRUE(compiled) << compiled.error().message;
    EXPECT_EQ(names(passes, compiled->order), (std::vector<std::string>{"scene", "taa", "present"}));
    EXPECT_EQ(compiled->history, (std::vector<std::string>{"taa"}));
}

TEST(FrameGraphCompiler, ThePreviousFrameOfAConsumedResourceIsItsFinalState) {
    const std::vector<PassDecl> passes{
        pass("reset",   {create("draws")}),
        pass("cull",    {consume("draws", "draws.culled")}),
        pass("forward", {read("draws.culled"), readPrevious("draws.culled"), write("screen")}),
    };
    const auto compiled = compile(passes, {"screen"});
    ASSERT_TRUE(compiled) << compiled.error().message;
    EXPECT_EQ(compiled->history, (std::vector<std::string>{"draws"})) << "one physical resource, kept once";
}

TEST(FrameGraphCompiler, RefusesThePreviousFrameOfAnImportedResource) {
    const std::vector<PassDecl> passes{pass("feedback", {readPrevious("screen"), write("screen")})};
    const auto compiled = compile(passes, {"screen"});
    ASSERT_FALSE(compiled);
    EXPECT_NE(compiled.error().message.find("imported"), std::string::npos) << compiled.error().message;
}

TEST(FrameGraphCompiler, RefusesThePreviousFrameOfSomethingNothingCreates) {
    const std::vector<PassDecl> passes{pass("use", {readPrevious("ghost"), write("screen")})};
    const auto compiled = compile(passes, {"screen"});
    ASSERT_FALSE(compiled);
    EXPECT_NE(compiled.error().message.find("reads 'ghost' from the previous frame, but no pass creates it"),
              std::string::npos) << compiled.error().message;
}

TEST(FrameGraphCompiler, ACulledReaderKeepsNoHistory) {
    const std::vector<PassDecl> passes{
        pass("make",   {create("x")}),
        pass("unused", {readPrevious("x"), create("nobodyReadsThis")}),
        pass("other",  {write("screen")}),
    };
    const auto compiled = compile(passes, {"screen"});
    ASSERT_TRUE(compiled) << compiled.error().message;
    EXPECT_EQ(names(passes, compiled->order), (std::vector<std::string>{"other"}));
    EXPECT_TRUE(compiled->history.empty());
}

TEST(FrameGraphCompiler, DisablingTheCreatorSkipsThePreviousFrameReader) {
    std::vector<PassDecl> passes{
        pass("make", {create("x")}),
        pass("use",  {readPrevious("x"), write("screen")}),
    };
    passes = disable(std::move(passes), "make");
    const auto compiled = compile(passes, {"screen"});
    ASSERT_TRUE(compiled) << compiled.error().message;
    ASSERT_EQ(compiled->skipped.size(), 1u);
    EXPECT_EQ(passes[compiled->skipped[0].pass].name, "use");
    EXPECT_EQ(compiled->skipped[0].resource, "x");
    EXPECT_TRUE(compiled->history.empty());
}

TEST(FrameGraphCompiler, ListsWhatEachPassWaitsFor) {
    const auto passes = deferredFrame();
    const auto compiled = compile(passes, {"screen"});
    ASSERT_TRUE(compiled) << compiled.error().message;
    ASSERT_EQ(compiled->dependencies.size(), 4u);
    EXPECT_TRUE(compiled->dependencies[0].empty());                                  // gbuffer
    EXPECT_EQ(compiled->dependencies[1], (std::vector<std::size_t>{0}));             // ssao
    EXPECT_EQ(compiled->dependencies[2], (std::vector<std::size_t>{0}));             // ssr
    EXPECT_EQ(compiled->dependencies[3], (std::vector<std::size_t>{0, 1, 2}));       // composite
}

PassDecl cpuPass(std::string name, std::vector<Use> uses, const bool sideEffect = false) {
    auto decl = pass(std::move(name), std::move(uses), sideEffect);
    decl.cpu = true;
    return decl;
}

TEST(FrameGraphCompiler, ACpuPassMayFeedTheGpu) {
    const std::vector<PassDecl> passes{
        cpuPass("sort",  {create("list")}),
        cpuPass("count", {read("list"), create("count")}),
        pass("draw",     {read("list"), read("count"), write("screen")}),
    };
    const auto compiled = compile(passes, {"screen"});
    ASSERT_TRUE(compiled) << compiled.error().message;
    EXPECT_EQ(names(passes, compiled->order), (std::vector<std::string>{"sort", "count", "draw"}));
}

TEST(FrameGraphCompiler, RefusesACpuPassTheGpuFeeds) {
    const std::vector<PassDecl> passes{
        pass("gpu",    {create("x")}),
        cpuPass("cpu", {read("x")}, true),
    };
    const auto compiled = compile(passes, {"screen"});
    ASSERT_FALSE(compiled);
    EXPECT_NE(compiled.error().message.find("'cpu'"), std::string::npos) << compiled.error().message;
    EXPECT_NE(compiled.error().message.find("'gpu'"), std::string::npos) << compiled.error().message;
}

TEST(FrameGraphCompiler, RefusesACpuPassReadingAPreviousFrame) {
    const std::vector<PassDecl> passes{
        pass("gpu",    {create("x"), write("screen")}),
        cpuPass("cpu", {readPrevious("x")}, true),
    };
    const auto compiled = compile(passes, {"screen"});
    ASSERT_FALSE(compiled);
    EXPECT_NE(compiled.error().message.find("previous frame"), std::string::npos) << compiled.error().message;
}

TEST(FrameGraphCompiler, ResourcesNeverAliveTogetherShareASlot) {
    const std::vector<CompiledGraph::Lifetime> lifetimes{
        {"a", 0, 1},
        {"b", 2, 3},   // after a: shares
        {"c", 1, 2},   // overlaps both a (at 1) and b (at 2): its own
        {"d", 4, 4},   // after b and c: shares with the first free one
    };
    const auto slots = packLifetimes(lifetimes, {"k", "k", "k", "k"});
    ASSERT_EQ(slots.size(), 4u);
    EXPECT_EQ(slots[0], slots[1]);
    EXPECT_NE(slots[2], slots[0]);
    EXPECT_TRUE(slots[3] == slots[0] || slots[3] == slots[2]);
}

TEST(FrameGraphCompiler, OnlyEqualKeysShareASlot) {
    const std::vector<CompiledGraph::Lifetime> lifetimes{{"a", 0, 0}, {"b", 1, 1}, {"c", 2, 2}, {"d", 3, 3}};
    const auto slots = packLifetimes(lifetimes, {"rgba8", "r32f", "", ""});
    EXPECT_NE(slots[0], slots[1]) << "different shapes";
    EXPECT_NE(slots[2], slots[3]) << "an empty key never shares";
    EXPECT_NE(slots[2], slots[0]);
}

TEST(FrameGraphCompiler, AResourceReadAndAnotherWrittenByOnePassDoNotShare) {
    // One pass is the last to read a and the first to write b: both are live in that pass.
    const std::vector<CompiledGraph::Lifetime> lifetimes{{"a", 0, 1}, {"b", 1, 2}};
    const auto slots = packLifetimes(lifetimes, {"k", "k"});
    EXPECT_NE(slots[0], slots[1]);
}

TEST(FrameGraphCompiler, AnEmptyGraphIsFine) {
    const auto compiled = compile({});
    ASSERT_TRUE(compiled);
    EXPECT_TRUE(compiled->order.empty());
}

}  // namespace
