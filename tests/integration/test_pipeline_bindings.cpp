// A pipeline that numbers its shader's descriptors itself (SetBinding), and specialization constants by name:
// what a tool that splits a shader's parameters between a pipeline and its entities builds on. Plus the
// reflection such a tool lists them from, in C++ and through the C API.

#include "gpu_fixture.h"

#include <algorithm>
#include <map>
#include <cstring>
#include <ranges>
#include <string>
#include <vector>

#include "buffer.h"
#include "commandBuffer.h"
#include "computePipeline.h"
#include "descriptorSet.h"
#include "koral_c.h"
#include "log.h"
#include "shader.h"

namespace {

using kor::Buffer;
using kor::CommandBuffer;
using kor::ComputePipeline;
using kor::DescriptorSet;
using kor::Shader;

constexpr kor::u32 Count = 100;

void AddTestShaders() { Shader::AddSearchPath(KORAL_TEST_SHADERS_DIR); }

kor::ResourceRef<const Shader> SplitShader()
{
    AddTestShaders();
    return Shader::Builder{}.SetEntryPoint("bindingSplit", "computeMain").GetOrBuild();
}

const Shader::Descriptor* Find(const Shader& shader, const std::string_view name, kor::u32* set = nullptr, kor::u32* binding = nullptr)
{
    for (const auto& [s, description] : shader.BlockLayout().descriptorSets)
        for (const auto& [b, descriptor] : description.descriptors)
            if (descriptor.name == name) {
                if (set) *set = s;
                if (binding) *binding = b;
                return &descriptor;
            }
    return nullptr;
}

/** Runs @p pipeline over Count floats 0, 1, 2, ... with each of its sets written by name, and reads the result. */
std::vector<float> RunOver(const kor::Resource<ComputePipeline>& pipeline, const float bias)
{
    std::vector<float> values(Count);
    for (kor::u32 i = 0; i < Count; ++i) values[i] = static_cast<float>(i);
    const auto input = Buffer::Builder<float>{}.SetData(values).Build();
    const auto output = Buffer::Builder<float>{}.SetInstanceCount(Count).Build();

    // Whichever sets the descriptors ended up in, written by the names the shader gave them.
    const auto& bindings = pipeline->Bindings();
    std::map<kor::u32, std::vector<std::pair<std::string, const kor::Resource<Buffer>*>>> bySet;
    for (const auto& [name, buffer] : { std::pair { "input", &input }, std::pair { "output", &output } }) {
        const auto it = bindings.find(name);
        bySet[it != bindings.end() ? it->second.set : 0u].emplace_back(name, buffer);
    }
    std::vector<kor::Resource<DescriptorSet>> sets;
    std::vector<kor::u32> indices;
    for (const auto& [index, writes] : bySet) {
        auto builder = DescriptorSet::Builder(pipeline, index);
        for (const auto& [name, buffer] : writes) builder.Write(name, *buffer);
        sets.push_back(builder.Build());
        indices.push_back(index);
    }

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BindComputePipeline(pipeline);
        for (std::size_t i = 0; i < sets.size(); ++i) cb.BindDescriptorSet(indices[i], sets[i]);
        cb.PushConstant("count", Count).PushConstant("bias", bias).Dispatch((Count + 63) / 64, 1, 1);
    }, CommandBuffer::Usage::eCompute).Wait();
    return output->Read<float>();
}

void ExpectNoValidationErrors(const std::uint64_t since)
{
    for (const auto& record : kor::log::HistorySince(since))
        EXPECT_EQ(record.message.find("VUID"), std::string::npos) << record.message;
}

TEST_F(GpuTest, AShaderWithoutBindingsReflectsItsParametersAndConstants) {
    const auto shader = SplitShader();
    ASSERT_TRUE(shader.Valid()) << shader.Failure()->History();

    EXPECT_NE(Find(*shader, "input"), nullptr);
    EXPECT_NE(Find(*shader, "output"), nullptr);
    EXPECT_TRUE(shader->DeclaresDescriptor("input"));
    EXPECT_FALSE(shader->DeclaresDescriptor("nothing"));

    const auto& constants = shader->BlockLayout().specializationConstants;
    ASSERT_TRUE(constants.contains(3));
    EXPECT_EQ(constants.at(3).name, "scale");
    EXPECT_EQ(constants.at(3).scalar, 1);
    EXPECT_EQ(constants.at(3).defaultValue, 2u);
    ASSERT_TRUE(constants.contains(7));
    EXPECT_EQ(constants.at(7).name, "offset");
    EXPECT_EQ(constants.at(7).scalar, 0);
    float offset = 0.f;
    const auto bits = static_cast<kor::u32>(constants.at(7).defaultValue);
    std::memcpy(&offset, &bits, 4);
    EXPECT_FLOAT_EQ(offset, 0.5f);

    EXPECT_EQ(shader->BlockLayout().localSize, (std::array<kor::u32, 3> { 64, 1, 1 }));

    std::vector<std::string> pushed;
    for (const auto& block : shader->BlockLayout().pushConstants | std::views::values)
        for (const auto& field : block.members) pushed.push_back(field.name);
    EXPECT_NE(std::ranges::find(pushed, "count"), pushed.end());
    EXPECT_NE(std::ranges::find(pushed, "bias"), pushed.end());
}

// A mesh shader compiles to the EXT execution model, which is not the NV one under another name:
// taken for an unknown stage, no mesh shader written in Slang could be loaded at all.
TEST_F(GpuTest, AMeshShaderIsKnownForOne) {
    AddTestShaders();
    const auto shader = Shader::Builder{}.SetEntryPoint("meshlet", "meshMain").GetOrBuild();
    ASSERT_TRUE(shader.Valid()) << shader.Failure()->History();
    EXPECT_EQ(shader->ShaderStage(), Shader::Stage::eMesh);
    EXPECT_EQ(shader->BlockLayout().localSize, (std::array<kor::u32, 3> { 1, 1, 1 })) << "a mesh shader has a work group too";
}

TEST_F(GpuTest, GlslWithoutBindingsCompilesAndReflects) {
    AddTestShaders();
    const auto shader = Shader::Builder{}.SetPath("unbound.comp.glsl").GetOrBuild();
    ASSERT_TRUE(shader.Valid()) << shader.Failure()->History();
    kor::u32 sourceSet = 9, sourceBinding = 9, targetSet = 9, targetBinding = 9;
    ASSERT_NE(Find(*shader, "source", &sourceSet, &sourceBinding), nullptr);
    ASSERT_NE(Find(*shader, "target", &targetSet, &targetBinding), nullptr);
    EXPECT_NE(std::pair(sourceSet, sourceBinding), std::pair(targetSet, targetBinding)) << "two descriptors numbered alike";
    ASSERT_TRUE(shader->BlockLayout().specializationConstants.contains(0));
    EXPECT_EQ(shader->BlockLayout().specializationConstants.at(0).name, "factor");
    EXPECT_EQ(shader->BlockLayout().localSize, (std::array<kor::u32, 3> { 64, 1, 1 }));
}

// The same shader, built into a pipeline that keeps the compiler's numbering and one that moves its descriptors
// to other sets: both compute the same thing, and the shader is compiled once for both.
TEST_F(GpuTest, APipelineNumbersItsShadersDescriptorsItself) {
    const auto shader = SplitShader();
    ASSERT_TRUE(shader.Valid()) << shader.Failure()->History();
    const auto since = kor::log::LastSequence();

    const auto asCompiled = ComputePipeline::Builder{}.SetComputeShader(shader).Build();
    ASSERT_TRUE(asCompiled.Valid()) << asCompiled.Failure()->History();
    const auto moved = ComputePipeline::Builder{}
                           .SetComputeShader(shader)
                           .SetBinding("input", 0, 3)
                           .SetBinding("output", 2, 0)
                           .Build();
    ASSERT_TRUE(moved.Valid()) << moved.Failure()->History();

    // The moved pipeline's layouts are where it said: input alone in set 0 at 3, output alone in set 2 — and
    // set 1, which nothing uses, empty, so that Vulkan counts set 2 as the third.
    ASSERT_NO_THROW((void)moved->SetLayout(2));
    EXPECT_TRUE(moved->SetLayout(1).Bindings().empty());
    EXPECT_TRUE(moved->SetLayout(2).Bindings().contains(0));

    const auto expected = [](const kor::u32 i, const float bias) { return static_cast<float>(i) * 2.f + 0.5f + bias; };
    const auto a = RunOver(asCompiled, 1.f);
    const auto b = RunOver(moved, 1.f);
    ASSERT_EQ(a.size(), Count);
    ASSERT_EQ(b.size(), Count);
    for (kor::u32 i = 0; i < Count; ++i) {
        EXPECT_FLOAT_EQ(a[i], expected(i, 1.f)) << i;
        EXPECT_FLOAT_EQ(b[i], expected(i, 1.f)) << i;
    }
    ExpectNoValidationErrors(since);

    // One shader object behind both: renumbering is the pipeline's, not a second compile.
    const auto again = Shader::Builder{}.SetEntryPoint("bindingSplit", "computeMain").GetOrBuild();
    EXPECT_EQ(again.Get(), shader.Get());
}

TEST_F(GpuTest, SpecializationConstantsAreSetByName) {
    const auto shader = SplitShader();
    ASSERT_TRUE(shader.Valid()) << shader.Failure()->History();
    const auto since = kor::log::LastSequence();
    const auto pipeline = ComputePipeline::Builder{}
                              .SetComputeShader(shader)
                              .SetSpecializationConstant("scale", 5)
                              .SetSpecializationConstant("offset", 0.25f)
                              .Build();
    ASSERT_TRUE(pipeline.Valid()) << pipeline.Failure()->History();
    const auto out = RunOver(pipeline, 0.f);
    ASSERT_EQ(out.size(), Count);
    for (kor::u32 i = 0; i < Count; ++i) EXPECT_FLOAT_EQ(out[i], static_cast<float>(i) * 5.f + 0.25f) << i;
    ExpectNoValidationErrors(since);
}

TEST_F(GpuTest, ABindingOrConstantNoShaderDeclaresFailsTheBuild) {
    const auto shader = SplitShader();
    ASSERT_TRUE(shader.Valid()) << shader.Failure()->History();

    const auto typo = ComputePipeline::Builder{}.SetComputeShader(shader).SetBinding("inptu", 0, 0).Build();
    ASSERT_FALSE(typo.Valid());
    EXPECT_NE(typo.Failure()->History().find("inptu"), std::string::npos) << typo.Failure()->History();

    const auto clash = ComputePipeline::Builder{}.SetComputeShader(shader).SetBinding("input", 2, 0).SetBinding("output", 2, 0).Build();
    ASSERT_FALSE(clash.Valid());
    EXPECT_EQ(clash.Failure()->code, kor::ErrorCode::eDescriptorConflict) << clash.Failure()->History();

    const auto constant = ComputePipeline::Builder{}.SetComputeShader(shader).SetSpecializationConstant("scael", 1).Build();
    ASSERT_FALSE(constant.Valid());
    EXPECT_NE(constant.Failure()->History().find("scael"), std::string::npos) << constant.Failure()->History();

    const auto wide = ComputePipeline::Builder{}.SetComputeShader(shader).SetSpecializationConstant("scale", 1.0).Build();
    ASSERT_FALSE(wide.Valid()) << "an 8-byte value for a 4-byte constant";
}

TEST_F(GpuTest, TheCInterfaceListsAShadersParameters) {
    AddTestShaders();
    KoralShaderBuilder* builder = koral_shader_builder_new();
    koral_shader_builder_set_entry_point(builder, "bindingSplit", "computeMain");
    KoralShader* shader = koral_shader_builder_get_or_build(builder, nullptr);
    koral_builder_destroy(reinterpret_cast<KoralBuilder*>(builder));
    ASSERT_NE(shader, nullptr);

    std::vector<std::string> names;
    for (size_t i = 0; i < koral_shader_parameter_count(shader); ++i) {
        KoralShaderParameter p {};
        ASSERT_TRUE(koral_shader_parameter(shader, i, &p));
        names.emplace_back(p.name);
    }
    EXPECT_NE(std::ranges::find(names, "input"), names.end());
    EXPECT_NE(std::ranges::find(names, "output"), names.end());
    KoralShaderParameter none {};
    EXPECT_FALSE(koral_shader_parameter(shader, 99, &none));

    std::vector<std::string> pushed;
    for (size_t i = 0; i < koral_shader_push_constant_count(shader); ++i) {
        KoralShaderPushConstant p {};
        ASSERT_TRUE(koral_shader_push_constant(shader, i, &p));
        pushed.emplace_back(p.name);
    }
    EXPECT_NE(std::ranges::find(pushed, "bias"), pushed.end());

    ASSERT_EQ(koral_shader_specialization_constant_count(shader), 2u);
    KoralShaderSpecializationConstant first {};
    ASSERT_TRUE(koral_shader_specialization_constant(shader, 0, &first));
    EXPECT_STREQ(first.name, "scale");
    EXPECT_EQ(first.id, 3u);
    EXPECT_EQ(first.default_value, 2u);
    koral_resource_release(reinterpret_cast<KoralResource*>(shader));
}

}

// A program with a descriptor set for each of thousands of objects — an engine with one an entity — is an ordinary
// one. A Vulkan pool is a fixed size; the engine's makes another when one is full, and frees a set to the pool it
// came from.
TEST_F(GpuTest, MoreDescriptorSetsThanOnePoolHolds) {
    const auto pipeline = ComputePipeline::Builder{}.SetComputeShader(SplitShader()).Build();
    ASSERT_TRUE(pipeline.Valid());
    const auto input = Buffer::Builder<float>{}.SetInstanceCount(Count).Build();
    const auto output = Buffer::Builder<float>{}.SetInstanceCount(Count).Build();
    const std::uint64_t before = kor::log::LastSequence();

    const auto make = [&] { return DescriptorSet::Builder(pipeline, 0).Write("input", input).Write("output", output).Build(); };
    std::vector<kor::Resource<DescriptorSet>> sets;
    for (int i = 0; i < 2500; ++i) {
        sets.push_back(make());
        ASSERT_TRUE(sets.back().Valid()) << "set " << i << ": " << sets.back().Failure()->History();
    }
    // Freed and made again, over and over: the room freed is used, wherever it is.
    for (int round = 0; round < 3; ++round) {
        for (std::size_t i = 0; i < sets.size(); i += 2) sets[i] = {};
        for (std::size_t i = 0; i < sets.size(); i += 2) {
            sets[i] = make();
            ASSERT_TRUE(sets[i].Valid()) << "round " << round << ", set " << i;
        }
    }
    // The last of them is as good as the first.
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BindComputePipeline(pipeline).BindDescriptorSet(0, sets.back());
        cb.PushConstant("count", Count).PushConstant("bias", 1.f).Dispatch((Count + 63) / 64, 1, 1);
    }, CommandBuffer::Usage::eCompute).Wait();
    EXPECT_FLOAT_EQ(output->Read<float>()[5], 0.f * 2 + 0.5f + 1.f);
    ExpectNoValidationErrors(before);
}

// Push constants as bytes laid out from reflection: for code that has the shader's block and no C++ struct of it.
TEST_F(GpuTest, PushConstantsAreGivenAsBytes) {
    const auto pipeline = ComputePipeline::Builder{}.SetComputeShader(SplitShader()).Build();
    ASSERT_TRUE(pipeline.Valid());
    std::vector<float> values(Count, 3.f);
    const auto input = Buffer::Builder<float>{}.SetData(values).Build();
    const auto output = Buffer::Builder<float>{}.SetInstanceCount(Count).Build();
    const auto set = DescriptorSet::Builder(pipeline, 0).Write("input", input).Write("output", output).Build();

    // The block, as the pipeline says it is laid out.
    const auto* count = pipeline->FindPushConstant("count");
    const auto* bias = pipeline->FindPushConstant("bias");
    ASSERT_TRUE(count && bias);
    std::vector<std::byte> block(std::max(count->offset + count->size, bias->offset + bias->size));
    const float biasValue = 10.f;
    std::memcpy(block.data() + count->offset, &Count, sizeof(Count));
    std::memcpy(block.data() + bias->offset, &biasValue, sizeof(biasValue));

    const std::uint64_t before = kor::log::LastSequence();
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BindComputePipeline(pipeline).BindDescriptorSet(0, set).PushConstantBytes(block).Dispatch((Count + 63) / 64, 1, 1);
    }, CommandBuffer::Usage::eCompute).Wait();
    EXPECT_FLOAT_EQ(output->Read<float>()[7], 3.f * 2 + 0.5f + 10.f);
    ExpectNoValidationErrors(before);
}
