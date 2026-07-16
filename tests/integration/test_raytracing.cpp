// Ray-tracing integration test: builds a bottom-level acceleration structure from
// a triangle mesh, wraps it in a top-level structure, creates a ray-tracing
// pipeline (raygen + miss + closest-hit) with its shader binding table, traces one
// ray per pixel into a storage image, and reads the result back. This is the only
// test that exercises accelerationStructure.cpp and rayTracingPipeline.cpp.
//
// The ray-tracing extensions are only enabled when the selected device actually
// advertises them (not every GPU does -- older/integrated GPUs and MoltenVK on
// macOS commonly do not), so this test additionally skips itself via
// kor::Context::SupportsRayTracing() on top of the fixture's own "no device at
// all" skip.

#include "gpu_fixture.h"

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "accelerationStructure.h"
#include "buffer.h"
#include "commandBuffer.h"
#include "context.h"
#include "descriptor.h"
#include "descriptorSet.h"
#include "image.h"
#include "imageView.h"
#include "mesh.h"
#include "meshLayout.h"
#include "rayTracingPipeline.h"
#include "shader.h"

using kor::AccelerationStructure;
using kor::Buffer;
using kor::CommandBuffer;
using kor::Descriptor;
using kor::DescriptorSet;
using kor::Image;
using kor::ImageView;
using kor::RayTracingPipeline;
using kor::ResourceRef;
using kor::Shader;

namespace {

using Pixel = glm::u8vec4;
using PosVertex = kor::ParamVertex<kor::Position>;
using PosMesh = kor::ParamMesh<PosVertex>;

constexpr std::uint32_t kW = 16;
constexpr std::uint32_t kH = 16;

TEST_F(GpuTest, TraceTriangleIntoStorageImage) {
    if (!kor::Context::SupportsRayTracing()) {
        GTEST_SKIP() << "Device has no ray tracing support; skipping.";
    }

    // --- triangle mesh (position-only) -----------------------------------
    std::vector<PosVertex> verts = {
        PosVertex{ glm::vec3{-0.8f, -0.8f, 0.0f} },
        PosVertex{ glm::vec3{ 0.8f, -0.8f, 0.0f} },
        PosVertex{ glm::vec3{ 0.0f,  0.8f, 0.0f} },
    };
    std::vector<std::uint32_t> indices = {0, 1, 2};
    auto mesh = PosMesh::Create(verts, indices);
    ASSERT_TRUE(static_cast<bool>(mesh));

    // --- BLAS + TLAS ------------------------------------------------------
    auto blas = AccelerationStructure::Builder{}
                    .addMesh(ResourceRef<const kor::Mesh>(mesh))
                    .build();
    ASSERT_EQ(blas->getType(), AccelerationStructure::Type::eBottomLevel);

    auto tlas = AccelerationStructure::Builder{}
                    .addInstance(AccelerationStructure::Instance{
                        .blas = ResourceRef<const AccelerationStructure>(blas),
                        .transform = glm::mat4(1.0f),
                    })
                    .build();
    ASSERT_EQ(tlas->getType(), AccelerationStructure::Type::eTopLevel);

    // --- storage image (ray-tracing output) ------------------------------
    auto outImage = Image::Builder{}
                        .setType(Image::Type::e2D)
                        .setFormat(Image::Format::eRGBA8_UNORM)
                        .setExtent(glm::uvec2{kW, kH})
                        .addUsage(Image::Usage::eStorage)
                        .addUsage(Image::Usage::eTransferSrc)
                        .build();
    auto outView = ImageView::Builder(ResourceRef<const Image>(outImage)).build();

    // --- ray-tracing pipeline --------------------------------------------
    const auto raygen = Shader::Builder{}.setLang<Shader::Lang::eGLSL>().setStage(Shader::Stage::eRaygen)
        .setPath(kor::shaderPath("simpleRT.rgen.glsl")).getOrBuild("test.rt.rgen");
    const auto miss = Shader::Builder{}.setLang<Shader::Lang::eGLSL>().setStage(Shader::Stage::eMiss)
        .setPath(kor::shaderPath("simpleRT.rmiss.glsl")).getOrBuild("test.rt.rmiss");
    const auto chit = Shader::Builder{}.setLang<Shader::Lang::eGLSL>().setStage(Shader::Stage::eClosestHit)
        .setPath(kor::shaderPath("simpleRT.rchit.glsl")).getOrBuild("test.rt.rchit");

    auto pipeline = RayTracingPipeline::Builder{}
                        .setRaygenShader(raygen)
                        .addMissShader(miss)
                        .addHitGroup(RayTracingPipeline::HitGroup{ .closestHitShader = chit })
                        .setMaxRecursionDepth(1)
                        .build();
    ASSERT_TRUE(static_cast<bool>(pipeline));

    // --- descriptor set: TLAS at 0, storage image at 1 -------------------
    auto descriptorSet = DescriptorSet::Builder(kor::ResourceRef<const kor::Pipeline>(pipeline), 0)
                             .write(0, Descriptor(ResourceRef<const AccelerationStructure>(tlas)))
                             .write(1, Descriptor(ResourceRef<const ImageView>(outView)))
                             .build();

    // --- trace ------------------------------------------------------------
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BindRayTracingPipeline(ResourceRef<const RayTracingPipeline>(pipeline));
        cb.BindDescriptorSet(0, ResourceRef<const DescriptorSet>(descriptorSet));
        cb.ImageBarrier(kor::ImageBarrier(ResourceRef<const Image>(outImage), kor::ResourceAccess::AllShaderWrite));
        cb.TraceRays(kW, kH, 1);
    }, CommandBuffer::Usage::eCompute);

    // --- read the image back and verify the trace ran --------------------
    Buffer::RawBuilder rb;
    rb.setRawSize(static_cast<glm::i64>(kW) * kH * sizeof(Pixel))
      .addUsage(Buffer::Usage::eTransferDst)
      .setType(Buffer::Type::eReadback);
    auto readback = rb.build();
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(ResourceRef<const Image>(outImage), ResourceRef<const Buffer>(readback));
    }, CommandBuffer::Usage::eTransfer);

    const std::vector<Pixel> out = readback->Read<Pixel>();
    ASSERT_EQ(out.size(), static_cast<std::size_t>(kW) * kH);
    // The raygen shader writes green on hit and blue on miss; with a centered
    // triangle we expect both to appear.
    int green = 0, blue = 0;
    for (const auto& p : out) {
        if (p.g > 200 && p.r < 50 && p.b < 50) ++green;
        if (p.b > 200 && p.r < 50 && p.g < 50) ++blue;
    }
    EXPECT_GT(green, 0) << "expected some rays to hit the triangle";
    EXPECT_GT(blue, 0) << "expected some rays to miss the triangle";
}

} // namespace
