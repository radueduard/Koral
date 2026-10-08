// Every builder, described (builderDescriptions.h), and objects built from a description and values alone: what an
// editor that knows nothing of Koral's types in advance builds with.

#include "gpu_fixture.h"

#include <algorithm>
#include <cstring>
#include <vector>
#include <set>
#include <string>

#include "builderDescriptions.h"
#include "computePipeline.h"
#include "image.h"
#include "koral_c.h"
#include "sampler.h"
#include "shader.h"

namespace {

using namespace kor::builders;

TEST(BuilderDescriptions, CoverEveryObjectAnEditorMakes) {
    std::set<std::string> names;
    for (const auto& d : All()) names.emplace(d.name);
    for (const char* expected : { "Buffer", "Image", "ImageView", "Sampler", "BufferView", "Shader", "Framebuffer", "Mesh",
                                  "GraphicsPipeline", "ComputePipeline", "RayTracingPipeline", "AccelerationStructure" })
        EXPECT_TRUE(names.contains(expected)) << expected;
    EXPECT_FALSE(names.contains("DescriptorSet")) << "a descriptor set follows from a pipeline's split";

    const auto* image = Find("Image");
    ASSERT_NE(image, nullptr);
    const auto* format = image->FindSetting("format");
    ASSERT_NE(format, nullptr);
    ASSERT_EQ(format->arguments.size(), 1u);
    EXPECT_EQ(format->arguments[0].type->kind, Kind::eEnum);
    ASSERT_NE(format->arguments[0].type->enumeration, nullptr);
    EXPECT_EQ(format->arguments[0].type->enumeration->name, "kor::Image::Format");
    const auto& values = format->arguments[0].type->enumeration->values;
    const auto rgba = std::ranges::find(values, "eRGBA8_UNORM", &Enumerator::name);
    ASSERT_NE(rgba, values.end());
    EXPECT_EQ(rgba->value, static_cast<std::uint64_t>(kor::Image::Format::eRGBA8_UNORM));

    EXPECT_EQ(image->FindSetting("usage")->arguments[0].type->kind, Kind::eFlags);

    const auto* view = Find("ImageView");
    ASSERT_EQ(view->constructor.size(), 1u);
    EXPECT_EQ(view->constructor[0].type->kind, Kind::eResource);
    EXPECT_EQ(view->constructor[0].type->name, "Image");

    const auto* raster = Find("GraphicsPipeline")->FindSetting("rasterizationState");
    ASSERT_NE(raster, nullptr);
    const Type& state = *raster->arguments[0].type;
    EXPECT_EQ(state.kind, Kind::eStruct);
    const auto cull = std::ranges::find(state.fields, "cullMode", &Field::name);
    ASSERT_NE(cull, state.fields.end());
    EXPECT_EQ(cull->type->kind, Kind::eFlags);

    const auto* color = Find("Framebuffer")->FindSetting("color");
    ASSERT_NE(color, nullptr);
    EXPECT_TRUE(color->repeatable);
}

TEST_F(GpuTest, ObjectsAreBuiltFromADescriptionAndValues) {
    // An image.
    const auto* image = Find("Image");
    auto builtImage = Build(*image, {}, {
        { image->FindSetting("format"), { Value::Of(kor::Image::Format::eRGBA8_UNORM) } },
        { image->FindSetting("extent"), { Value::Of(16u), Value::Of(8u), Value::Of(1u) } },
        { image->FindSetting("usage"), { Value::Of(static_cast<std::uint64_t>(kor::Flags<kor::Image::Usage>(kor::Image::Usage::eSampled)
                                                                                | kor::Image::Usage::eTransferDst)) } },
    });
    ASSERT_TRUE(builtImage) << builtImage.error().message;
    ASSERT_TRUE(koral_resource_valid(*builtImage)) << koral_resource_error_history(*builtImage);
    EXPECT_EQ(koral_resource_kind(*builtImage), KORAL_RESOURCE_IMAGE);

    // A view of it: what the builder is made from.
    const auto* view = Find("ImageView");
    auto builtView = Build(*view, std::vector { Value::Of(*builtImage) }, {});
    ASSERT_TRUE(builtView) << builtView.error().message;
    EXPECT_TRUE(koral_resource_valid(*builtView)) << koral_resource_error_history(*builtView);

    // A sampler.
    const auto* sampler = Find("Sampler");
    auto builtSampler = Build(*sampler, {}, {
        { sampler->FindSetting("minFilter"), { Value::Of(kor::Filter::eNearest) } },
        { sampler->FindSetting("addressModeU"), { Value::Of(kor::Sampler::AddressMode::eClampToEdge) } },
        { sampler->FindSetting("maxAnisotropy"), { Value::Of(4.0) } },
    });
    ASSERT_TRUE(builtSampler) << builtSampler.error().message;
    EXPECT_TRUE(koral_resource_valid(*builtSampler)) << koral_resource_error_history(*builtSampler);

    // A shader by module and entry point, and a compute pipeline from it that numbers its descriptors itself.
    kor::Shader::AddSearchPath(KORAL_TEST_SHADERS_DIR);
    const auto* shader = Find("Shader");
    auto builtShader = Build(*shader, {}, { { shader->FindSetting("entryPoint"), { Value::Of("bindingSplit"), Value::Of("computeMain") } } });
    ASSERT_TRUE(builtShader) << builtShader.error().message;
    ASSERT_TRUE(koral_resource_valid(*builtShader)) << koral_resource_error_history(*builtShader);

    const auto* pipeline = Find("ComputePipeline");
    std::vector<std::byte> scale(4);
    const std::int32_t five = 5;
    std::memcpy(scale.data(), &five, 4);
    auto builtPipeline = Build(*pipeline, {}, {
        { pipeline->FindSetting("computeShader"), { Value::Of(*builtShader) } },
        { pipeline->FindSetting("binding"), { Value::Of("output"), Value::Of(1u), Value::Of(0u) } },
        { pipeline->FindSetting("specializationConstantNamed"), { Value::Of("scale"), Value::Of(std::move(scale)) } },
    });
    ASSERT_TRUE(builtPipeline) << builtPipeline.error().message;
    ASSERT_TRUE(koral_resource_valid(*builtPipeline)) << koral_resource_error_history(*builtPipeline);
    EXPECT_EQ(koral_resource_kind(*builtPipeline), KORAL_RESOURCE_COMPUTE_PIPELINE);

    // What the builder refuses comes back poisoned, as from its own Build, naming why.
    auto refused = Build(*pipeline, {}, {
        { pipeline->FindSetting("computeShader"), { Value::Of(*builtShader) } },
        { pipeline->FindSetting("binding"), { Value::Of("nothing"), Value::Of(0u), Value::Of(0u) } },
    });
    ASSERT_TRUE(refused);
    EXPECT_FALSE(koral_resource_valid(*refused));
    EXPECT_NE(std::string(koral_resource_error_history(*refused)).find("nothing"), std::string::npos);

    // A call that does not fit the description is an error, and makes nothing.
    auto wrong = Build(*image, {}, { { image->FindSetting("format"), { Value::Of(1u), Value::Of(2u) } } });
    EXPECT_FALSE(wrong);
    auto foreign = Build(*image, {}, { { sampler->FindSetting("minFilter"), { Value::Of(0u) } } });
    EXPECT_FALSE(foreign);

    for (auto* r : { *builtImage, *builtView, *builtSampler, *builtShader, *builtPipeline, *refused }) koral_resource_release(r);
}

}
