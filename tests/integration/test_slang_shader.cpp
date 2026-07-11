// Integration coverage for the Slang compilation path (slangCompiler.cpp and the
// Slang branch of shader.cpp). Compiles the bundled sample.slang module, which
// `import helpers;` — exercising cross-module resolution across the shader search
// roots. Building a backend shader needs the device, so this runs on the headless
// fixture and skips when none is available.

#include "gpu_fixture.h"

#include "shader.h"

using gfx::Shader;

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

} // namespace
