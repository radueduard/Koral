// Integration tests for poison propagation: a resource that fails to build is not an exception
// and not an absence — it is a value carrying the reason, and anything built from it inherits
// that reason as a *cause* rather than reporting an unrelated-looking failure of its own.
//
// These run against a real Vulkan device because the propagation only means anything end to end:
// the shader really fails to compile, the pipeline really refuses to reach the backend, and the
// command buffer really declines to record with it — all without throwing.

#include "gpu_fixture.h"

#include <fstream>
#include <string>

#include "commandBuffer.h"
#include "context.h"
#include "computePipeline.h"
#include "error.h"
#include "graphicsPipeline.h"
#include "file.h"
#include "shader.h"

using gfx::CommandBuffer;
using gfx::ComputePipeline;
using gfx::ErrorCode;
using gfx::GraphicsPipeline;
using gfx::Shader;

namespace {

// Writes a shader source that does not compile, and returns its path.
std::filesystem::path writeBrokenShader(const std::string& name, const std::string& body) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream(path) << body;
    return path;
}

// The same shader, but valid — what the user "fixes" the broken one into.
constexpr const char* kWorkingCompute = R"(#version 450
layout(local_size_x = 1) in;
layout(set = 0, binding = 0) buffer Data { uint values[]; };
void main() {
    values[gl_GlobalInvocationID.x] *= 2u;
}
)";

constexpr const char* kBrokenCompute = R"(#version 450
layout(local_size_x = 1) in;
void main() {
    this is not valid glsl at all;
}
)";

// A shader whose source does not compile must come back poisoned, not thrown and not empty.
TEST_F(GpuTest, BrokenShaderIsPoisonedNotThrown) {
    const auto path = writeBrokenShader("gfx_broken.comp.glsl", kBrokenCompute);

    gfx::Resource<Shader> shader;
    ASSERT_NO_THROW({
        shader = Shader::Builder{}
                     .setLang<Shader::Lang::eGLSL>()
                     .setStage(Shader::Stage::eCompute)
                     .setPath(path)
                     .build();
    });

    EXPECT_FALSE(shader.valid());
    EXPECT_TRUE(shader.poisoned());
    ASSERT_NE(shader.error(), nullptr);
    EXPECT_EQ(shader.error()->code, ErrorCode::eShaderCompileFailed);

    // The resource still has an identity and a name — it is a thing that exists and is broken,
    // not a thing that is missing.
    EXPECT_EQ(shader.name(), path.string());
}

// The motivating case. A pipeline built from a shader that failed to compile is itself unusable,
// and says *why*: the compile error is the root of its cause chain, not a separate error.
TEST_F(GpuTest, PoisonedShaderPoisonsThePipelineWithACauseChain) {
    const auto path = writeBrokenShader("gfx_broken2.comp.glsl", kBrokenCompute);

    const auto shader = Shader::Builder{}
                            .setLang<Shader::Lang::eGLSL>()
                            .setStage(Shader::Stage::eCompute)
                            .setPath(path)
                            .build();
    ASSERT_TRUE(shader.poisoned());

    gfx::Resource<ComputePipeline> pipeline;
    ASSERT_NO_THROW({
        pipeline = ComputePipeline::Builder{}
                       .setComputeShader(gfx::ResourceRef<const Shader>(shader))
                       .build();
    });

    EXPECT_FALSE(pipeline.valid());
    ASSERT_TRUE(pipeline.poisoned());

    // The pipeline's own error explains the symptom...
    ASSERT_NE(pipeline.error(), nullptr);
    EXPECT_NE(pipeline.error()->message.find("compute shader"), std::string::npos);

    // ...and the chain beneath it explains what to actually go and fix.
    ASSERT_NE(pipeline.error()->cause, nullptr);
    EXPECT_EQ(pipeline.error()->root().code, ErrorCode::eShaderCompileFailed);

    // The printed history names both ends of the story.
    const std::string history = pipeline.error()->history();
    EXPECT_NE(history.find("caused by"), std::string::npos);
}

// Recording with a poisoned pipeline fails the command buffer instead of throwing or, worse,
// dereferencing a pipeline that does not exist.
TEST_F(GpuTest, RecordingWithAPoisonedPipelineFailsTheCommandBufferWithoutThrowing) {
    const auto path = writeBrokenShader("gfx_broken3.comp.glsl", kBrokenCompute);

    const auto shader = Shader::Builder{}
                            .setLang<Shader::Lang::eGLSL>()
                            .setStage(Shader::Stage::eCompute)
                            .setPath(path)
                            .build();
    const auto pipeline = ComputePipeline::Builder{}
                              .setComputeShader(gfx::ResourceRef<const Shader>(shader))
                              .build();
    ASSERT_TRUE(pipeline.poisoned());

    const auto cb = CommandBuffer::Create(CommandBuffer::Usage::eCompute);

    ASSERT_NO_THROW({
        cb->Begin()
           .BindComputePipeline(gfx::ResourceRef<const ComputePipeline>(pipeline))
           .Dispatch(1, 1, 1);
        cb->End();
    });

    EXPECT_FALSE(cb->ok());
    ASSERT_FALSE(cb->errors().empty());

    const auto result = cb->result();
    ASSERT_FALSE(result.has_value());
}

// ---------------------------------------------------------------------------------------------
// Recovery. This is what the whole design is for: a pipeline that could not be built because its
// shader would not compile must come back — by itself, in place — once that shader is fixed. And
// the refs handed out while it was broken must see the repaired pipeline without being re-issued.
// ---------------------------------------------------------------------------------------------
TEST_F(GpuTest, FixingTheShaderBringsThePipelineBack) {
    const auto path = writeBrokenShader("gfx_recover.comp.glsl", kBrokenCompute);

    // getOrBuild registers the shader (poisoned) so it is watched and can be repaired.
    const auto shader = Shader::Builder{}
                            .setLang<Shader::Lang::eGLSL>()
                            .setStage(Shader::Stage::eCompute)
                            .setPath(path)
                            .getOrBuild("test.recover");
    ASSERT_TRUE(shader.poisoned());

    auto pipeline = ComputePipeline::Builder{}.setComputeShader(shader).build();
    ASSERT_TRUE(pipeline.poisoned());

    // Hand a ref out *while the pipeline is broken*, as a renderer or scene would.
    const gfx::ResourceRef<const ComputePipeline> ref = pipeline;
    ASSERT_FALSE(ref.valid());

    // The user fixes the shader. (Driving requestRepair directly rather than waiting on the file
    // watcher keeps the test deterministic; the watcher calls exactly this.)
    std::ofstream(path) << kWorkingCompute;
    shader.requestRepair();

    // One frame's worth of repository maintenance: the shader recompiles, and the pipeline — whose
    // only problem was that shader — is rebuilt behind it in the same pass.
    gfx::Context::Repository().update();

    EXPECT_TRUE(shader.valid()) << "the shader should have recompiled";
    EXPECT_TRUE(pipeline.valid())
        << "the pipeline should have been rebuilt once its shader came back";
    EXPECT_FALSE(pipeline.poisoned());

    // The ref issued while everything was broken now resolves to the repaired pipeline, without
    // having been re-issued. This is the property the state-block redesign exists to provide.
    EXPECT_TRUE(ref.valid());
    EXPECT_EQ(ref.get(), pipeline.get());
}

// A repair that does not help must not spin: a still-broken pipeline may not rebuild itself on
// every frame, or a broken shader would peg the CPU recompiling for as long as it stays broken.
TEST_F(GpuTest, RepairDoesNotSpinWhileStillBroken) {
    const auto path = writeBrokenShader("gfx_nospin.comp.glsl", kBrokenCompute);

    const auto shader = Shader::Builder{}
                            .setLang<Shader::Lang::eGLSL>()
                            .setStage(Shader::Stage::eCompute)
                            .setPath(path)
                            .getOrBuild("test.nospin");
    const auto pipeline = ComputePipeline::Builder{}.setComputeShader(shader).build();
    ASSERT_TRUE(pipeline.poisoned());

    const auto generationBefore = pipeline.generation();

    // No edit, no repair request: several frames must change nothing at all.
    for (int frame = 0; frame < 5; ++frame) gfx::Context::Repository().update();

    EXPECT_TRUE(pipeline.poisoned());
    EXPECT_EQ(pipeline.generation(), generationBefore) << "a hopeless repair must not be retried";
}

// A shader that compiles is not poisoned, and neither is the pipeline built from it. Guards
// against the propagation being so eager that it poisons everything.
TEST_F(GpuTest, GoodShaderYieldsAUsablePipeline) {
    const auto shader = Shader::Builder{}
                            .setLang<Shader::Lang::eGLSL>()
                            .setStage(Shader::Stage::eCompute)
                            .setPath(gfx::shaderPath("doubleValues.comp.glsl"))
                            .build();
    ASSERT_TRUE(shader.valid()) << shader.error()->history();

    const auto pipeline = ComputePipeline::Builder{}
                              .setComputeShader(gfx::ResourceRef<const Shader>(shader))
                              .build();
    EXPECT_TRUE(pipeline.valid()) << (pipeline.error() ? pipeline.error()->history() : "");
    EXPECT_FALSE(pipeline.poisoned());
}

} // namespace
