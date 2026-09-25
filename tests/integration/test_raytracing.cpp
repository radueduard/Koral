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
#include <koralMesh.h>
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
using PosVertex = kmesh::ParamVertex<kmesh::Position>;
using PosMesh = kmesh::ParamMesh<PosVertex>;

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
                    .AddMesh(mesh)
                    .Build();
    ASSERT_EQ(blas->StructureType(), AccelerationStructure::Type::eBottomLevel);

    auto tlas = AccelerationStructure::Builder{}
                    .AddInstance(AccelerationStructure::Instance{
                        .blas = ResourceRef<const AccelerationStructure>(blas),
                        .transform = glm::mat4(1.0f),
                    })
                    .Build();
    ASSERT_EQ(tlas->StructureType(), AccelerationStructure::Type::eTopLevel);

    // --- storage image (ray-tracing output) ------------------------------
    auto outImage = Image::Builder{}
                        .SetType(Image::Type::e2D)
                        .SetFormat(Image::Format::eRGBA8_UNORM)
                        .SetExtent(glm::uvec2{kW, kH})
                        .SetUsage(Image::Usage::eStorage | Image::Usage::eTransferSrc)
                        .Build();
    auto outView = ImageView::Builder(outImage).Build();

    // --- ray-tracing pipeline --------------------------------------------
    const auto raygen = Shader::Builder{}.SetLang<Shader::Lang::eGLSL>().SetStage(Shader::Stage::eRaygen)
        .SetPath(kor::ShaderPath("simpleRT.rgen.glsl")).GetOrBuild("test.rt.rgen");
    const auto miss = Shader::Builder{}.SetLang<Shader::Lang::eGLSL>().SetStage(Shader::Stage::eMiss)
        .SetPath(kor::ShaderPath("simpleRT.rmiss.glsl")).GetOrBuild("test.rt.rmiss");
    const auto chit = Shader::Builder{}.SetLang<Shader::Lang::eGLSL>().SetStage(Shader::Stage::eClosestHit)
        .SetPath(kor::ShaderPath("simpleRT.rchit.glsl")).GetOrBuild("test.rt.rchit");

    auto pipeline = RayTracingPipeline::Builder{}
                        .SetRaygenShader(raygen)
                        .AddMissShader(miss)
                        .AddHitGroup(RayTracingPipeline::HitGroup{ .closestHitShader = chit })
                        .SetMaxRecursionDepth(1)
                        .Build();
    ASSERT_TRUE(static_cast<bool>(pipeline));

    // --- descriptor set: TLAS at 0, storage image at 1 -------------------
    auto descriptorSet = DescriptorSet::Builder(pipeline, 0)
                             .Write(0, tlas)
                             .Write(1, outView)
                             .Build();

    // --- trace ------------------------------------------------------------
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BindRayTracingPipeline(pipeline);
        cb.BindDescriptorSet(0, descriptorSet);
        // No barrier: the storage image is bound at set 0 binding 1, so the engine transitions
        // it to the layout the raygen shader writes through.
        cb.TraceRays(kW, kH, 1);
    }, CommandBuffer::Usage::eCompute).Wait();

    // --- read the image back and verify the trace ran --------------------
    Buffer::RawBuilder rb;
    rb.SetRawSize(static_cast<glm::i64>(kW) * kH * sizeof(Pixel))
      .SetUsage(Buffer::Usage::eTransferDst)
      .SetType(Buffer::Type::eReadback);
    auto readback = rb.Build();
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(outImage, readback);
    }, CommandBuffer::Usage::eTransfer).Wait();

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
