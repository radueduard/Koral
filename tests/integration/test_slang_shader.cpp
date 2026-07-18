// Integration coverage for the Slang compilation path (slangCompiler.cpp and the
// Slang branch of shader.cpp). Compiles the bundled sample.slang module, which
// `import helpers;` — exercising cross-module resolution across the shader search
// roots. Building a backend shader needs the device, so this runs on the headless
// fixture and skips when none is available.

#include "gpu_fixture.h"

#include "shader.h"
#include "context.h"

using kor::Shader;

namespace {

// The stage is auto-detected from each entry point's [shader("...")] attribute,
// so we don't set it and assert the compiler recovered it.
TEST_F(GpuTest, SlangCompilesEntryPointsWithAutoStage) {
    auto vs = Shader::Builder{}
                  .setLang<Shader::Lang::eSlang>()
                  .setEntryPoint("sample", "vertexMain")
                  .build();
    ASSERT_TRUE(vs.valid()) << vs.error()->history();
    EXPECT_EQ(vs->getStage(), Shader::Stage::eVertex);
    EXPECT_EQ(vs->getLang(), Shader::Lang::eSlang);
    // sample.slang imports helpers.slang; both should be tracked for hot-reload.
    EXPECT_FALSE(vs->getDependencies().empty());

    auto fs = Shader::Builder{}
                  .setLang<Shader::Lang::eSlang>()
                  .setEntryPoint("sample", "fragmentMain")
                  .build();
    ASSERT_TRUE(fs.valid()) << fs.error()->history();
    EXPECT_EQ(fs->getStage(), Shader::Stage::eFragment);
}

// getOrBuild caches by "module:entry"; a second call returns the same shader.
TEST_F(GpuTest, SlangGetOrBuildCaches) {
    auto first = Shader::Builder{}
                     .setLang<Shader::Lang::eSlang>()
                     .setEntryPoint("sample", "vertexMain")
                     .getOrBuild();
    ASSERT_TRUE(first.valid()) << first.error()->history();

    auto second = Shader::Builder{}
                      .setLang<Shader::Lang::eSlang>()
                      .setEntryPoint("sample", "vertexMain")
                      .getOrBuild();
    ASSERT_TRUE(second.valid()) << second.error()->history();
    EXPECT_EQ(first.get(), second.get());   // same cached shader object
}

// The point of the unified builder: the same call shape builds either language. No
// setLang, no kor::shaderPath, no setStage — path (+ entry point for Slang) is enough.
TEST_F(GpuTest, ShaderBuilderIsIdenticalAcrossLanguages) {
    auto glsl = Shader::Builder{}.setPath("flatTriangle.vert.glsl").build();
    ASSERT_TRUE(glsl.valid()) << glsl.error()->history();
    EXPECT_EQ(glsl->getLang(), Shader::Lang::eGLSL);   // inferred from ".glsl"
    EXPECT_EQ(glsl->getStage(), Shader::Stage::eVertex); // inferred from ".vert."

    auto slang = Shader::Builder{}.setPath("sample.slang").setEntryPoint("vertexMain").build();
    ASSERT_TRUE(slang.valid()) << slang.error()->history();
    EXPECT_EQ(slang->getLang(), Shader::Lang::eSlang);  // inferred from ".slang"
    EXPECT_EQ(slang->getStage(), Shader::Stage::eVertex); // from [shader("vertex")]

    // Explicit setters still override every inference.
    auto forced = Shader::Builder{}
                      .setLang<Shader::Lang::eGLSL>()
                      .setStage(Shader::Stage::eFragment)
                      .setPath(kor::shaderPath("flatTriangle.frag.glsl"))
                      .build();
    ASSERT_TRUE(forced.valid()) << forced.error()->history();
    EXPECT_EQ(forced->getStage(), Shader::Stage::eFragment);
}

// getOrBuild derives its cache key from the source, so the no-argument form caches
// correctly for both languages.
TEST_F(GpuTest, ShaderGetOrBuildDefaultsItsIdentifier) {
    auto a = Shader::Builder{}.setPath("flatTriangle.vert.glsl").getOrBuild();
    auto b = Shader::Builder{}.setPath("flatTriangle.vert.glsl").getOrBuild();
    ASSERT_TRUE(a.valid()) << a.error()->history();
    EXPECT_EQ(a.get(), b.get());

    // Two entry points of one Slang module must not collide.
    auto vs = Shader::Builder{}.setPath("sample.slang").setEntryPoint("vertexMain").getOrBuild();
    auto fs = Shader::Builder{}.setPath("sample.slang").setEntryPoint("fragmentMain").getOrBuild();
    ASSERT_TRUE(vs.valid()) << vs.error()->history();
    ASSERT_TRUE(fs.valid()) << fs.error()->history();
    EXPECT_NE(vs.get(), fs.get());
    EXPECT_EQ(vs->getStage(), Shader::Stage::eVertex);
    EXPECT_EQ(fs->getStage(), Shader::Stage::eFragment);
}

// A GLSL name with no stage tag is a clear error, not a silent compile as eCompute.
TEST_F(GpuTest, ShaderWithoutAStageTagFailsLoudly) {
    const auto shader = Shader::Builder{}.setPath("helpers.slang.glsl").build();
    ASSERT_FALSE(shader.valid());
    EXPECT_EQ(shader.error()->code, kor::ErrorCode::eInvalidArgument);
    EXPECT_NE(shader.error()->history().find("infer the shader stage"), std::string::npos);
}

} // namespace
