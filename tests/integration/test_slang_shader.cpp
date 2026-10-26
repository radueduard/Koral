// Integration coverage for the Slang compilation path (slangCompiler.cpp and the
// Slang branch of shader.cpp). Compiles the bundled sample.slang module, which
// `import helpers;` — exercising cross-module resolution across the shader search
// roots. Building a backend shader needs the device, so this runs on the headless
// fixture and skips when none is available.

#include "gpu_fixture.h"

#include "shader.h"
#include "context.h"

#include <filesystem>
#include <fstream>
#include <string_view>

using kor::Shader;

namespace {

// The stage is auto-detected from each entry point's [shader("...")] attribute,
// so we don't set it and assert the compiler recovered it.
TEST_F(GpuTest, SlangCompilesEntryPointsWithAutoStage) {
    auto vs = Shader::Builder{}
                  .SetLang<Shader::Lang::eSlang>()
                  .SetEntryPoint("sample", "vertexMain")
                  .Build();
    ASSERT_TRUE(vs.Valid()) << vs.Failure()->History();
    EXPECT_EQ(vs->ShaderStage(), Shader::Stage::eVertex);
    EXPECT_EQ(vs->Language(), Shader::Lang::eSlang);
    // sample.slang imports helpers.slang; both should be tracked for hot-reload.
    EXPECT_FALSE(vs->Dependencies().empty());

    auto fs = Shader::Builder{}
                  .SetLang<Shader::Lang::eSlang>()
                  .SetEntryPoint("sample", "fragmentMain")
                  .Build();
    ASSERT_TRUE(fs.Valid()) << fs.Failure()->History();
    EXPECT_EQ(fs->ShaderStage(), Shader::Stage::eFragment);
}

// getOrBuild caches by "module:entry"; a second call returns the same shader.
TEST_F(GpuTest, SlangGetOrBuildCaches) {
    auto first = Shader::Builder{}
                     .SetLang<Shader::Lang::eSlang>()
                     .SetEntryPoint("sample", "vertexMain")
                     .GetOrBuild();
    ASSERT_TRUE(first.Valid()) << first.Failure()->History();

    auto second = Shader::Builder{}
                      .SetLang<Shader::Lang::eSlang>()
                      .SetEntryPoint("sample", "vertexMain")
                      .GetOrBuild();
    ASSERT_TRUE(second.Valid()) << second.Failure()->History();
    EXPECT_EQ(first.Get(), second.Get());   // same cached shader object
}

// The point of the unified builder: the same call shape builds either language. No
// setLang, no kor::ShaderPath, no setStage — path (+ entry point for Slang) is enough.
TEST_F(GpuTest, ShaderBuilderIsIdenticalAcrossLanguages) {
    auto glsl = Shader::Builder{}.SetPath("flatTriangle.vert.glsl").Build();
    ASSERT_TRUE(glsl.Valid()) << glsl.Failure()->History();
    EXPECT_EQ(glsl->Language(), Shader::Lang::eGLSL);   // inferred from ".glsl"
    EXPECT_EQ(glsl->ShaderStage(), Shader::Stage::eVertex); // inferred from ".vert."

    auto slang = Shader::Builder{}.SetPath("sample.slang").SetEntryPoint("vertexMain").Build();
    ASSERT_TRUE(slang.Valid()) << slang.Failure()->History();
    EXPECT_EQ(slang->Language(), Shader::Lang::eSlang);  // inferred from ".slang"
    EXPECT_EQ(slang->ShaderStage(), Shader::Stage::eVertex); // from [shader("vertex")]

    // Explicit setters still override every inference.
    auto forced = Shader::Builder{}
                      .SetLang<Shader::Lang::eGLSL>()
                      .SetStage(Shader::Stage::eFragment)
                      .SetPath(kor::ShaderPath("flatTriangle.frag.glsl"))
                      .Build();
    ASSERT_TRUE(forced.Valid()) << forced.Failure()->History();
    EXPECT_EQ(forced->ShaderStage(), Shader::Stage::eFragment);
}

// getOrBuild derives its cache key from the source, so the no-argument form caches
// correctly for both languages.
TEST_F(GpuTest, ShaderGetOrBuildDefaultsItsIdentifier) {
    auto a = Shader::Builder{}.SetPath("flatTriangle.vert.glsl").GetOrBuild();
    auto b = Shader::Builder{}.SetPath("flatTriangle.vert.glsl").GetOrBuild();
    ASSERT_TRUE(a.Valid()) << a.Failure()->History();
    EXPECT_EQ(a.Get(), b.Get());

    // Two entry points of one Slang module must not collide.
    auto vs = Shader::Builder{}.SetPath("sample.slang").SetEntryPoint("vertexMain").GetOrBuild();
    auto fs = Shader::Builder{}.SetPath("sample.slang").SetEntryPoint("fragmentMain").GetOrBuild();
    ASSERT_TRUE(vs.Valid()) << vs.Failure()->History();
    ASSERT_TRUE(fs.Valid()) << fs.Failure()->History();
    EXPECT_NE(vs.Get(), fs.Get());
    EXPECT_EQ(vs->ShaderStage(), Shader::Stage::eVertex);
    EXPECT_EQ(fs->ShaderStage(), Shader::Stage::eFragment);
}

// Two projects opened one after the other may each have a module of the same name. Each GetOrBuild is of the one its
// search roots find now — not of whichever was built first — and a root asked for first again goes back to the front.
TEST_F(GpuTest, ASlangModuleIsTheOneItsRootsFindNow) {
    const auto base = std::filesystem::temp_directory_path() / "koral-twin-modules";
    const auto write = [&](const char* project, const char* buffer) {
        const auto dir = base / project;
        std::filesystem::create_directories(dir);
        std::ofstream(dir / "twinModule.slang") << "RWStructuredBuffer<float> " << buffer
            << ";\n[shader(\"compute\")]\n[numthreads(1, 1, 1)]\nvoid computeMain() { " << buffer << "[0] = 1.0; }\n";
        return dir;
    };
    const auto first = write("first", "firstBuffer"), second = write("second", "secondBuffer");
    const auto names = [](const Shader& shader) {
        std::string all;
        for (const auto& [set, description] : shader.BlockLayout().descriptorSets)
            for (const auto& [binding, descriptor] : description.descriptors) all += descriptor.name + " ";
        return all;
    };
    const auto build = [] { return Shader::Builder{}.SetEntryPoint("twinModule", "computeMain").GetOrBuild(); };

    Shader::AddSearchPath(first, true);
    const auto a = build();
    ASSERT_TRUE(a.Valid()) << a.Failure()->History();
    EXPECT_NE(names(*a).find("firstBuffer"), std::string::npos) << names(*a);

    Shader::AddSearchPath(second, true);
    const auto b = build();
    ASSERT_TRUE(b.Valid()) << b.Failure()->History();
    EXPECT_NE(names(*b).find("secondBuffer"), std::string::npos) << "the second project's, not the first's: " << names(*b);

    Shader::AddSearchPath(first, true);
    EXPECT_EQ(Shader::SearchPaths().front(), first) << "asked for first again, it is first again";
    const auto again = build();
    ASSERT_TRUE(again.Valid());
    EXPECT_EQ(again.Get(), a.Get()) << "the first's, as it was built";
    std::filesystem::remove_all(base);
}

// A GLSL name with no stage tag is a clear error, not a silent compile as eCompute.
TEST_F(GpuTest, ShaderWithoutAStageTagFailsLoudly) {
    const auto shader = Shader::Builder{}.SetPath("helpers.slang.glsl").Build();
    ASSERT_FALSE(shader.Valid());
    EXPECT_EQ(shader.Failure()->code, kor::ErrorCode::eInvalidArgument);
    EXPECT_NE(shader.Failure()->History().find("infer the shader stage"), std::string::npos);
}

} // namespace
