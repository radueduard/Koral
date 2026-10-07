// Integration test for the graphics/rendering path against a real Vulkan device,
// entirely offscreen (no window, surface or swap chain — the harness is headless).
//
// It exercises the parts of the framework that the buffer/compute/image tests
// never touch: GLSL vertex+fragment compilation, GraphicsPipeline creation
// (dynamic-rendering PipelineRenderingCreateInfo, viewport/scissor/blend state),
// ImageView + Framebuffer construction, and the command-buffer draw path
// (BindGraphicsPipeline, BeginRendering/EndRendering, SetViewport, SetScissor,
// applyDynamicDefaults, Draw). We render a solid-green full-screen triangle into
// an offscreen color image, read it back, and assert every texel is green.

#include "gpu_fixture.h"

#include <cstdint>
#include <vector>

#include <kmath/matrix.h>

#include "buffer.h"
#include "commandBuffer.h"
#include "context.h"
#include "framebuffer.h"
#include "graphicsPipeline.h"
#include "image.h"
#include "imageView.h"
#include "mesh.h"
#include <koralMesh.h>
#include "shader.h"
#include "sampler.h"
#include "descriptor.h"
#include "descriptorSet.h"

using kor::Buffer;
using kor::CommandBuffer;
using kor::Framebuffer;
using kor::GraphicsPipeline;
using kor::Image;
using kor::ImageView;
using kor::ResourceRef;
using kor::Shader;
using kor::Sampler;
using kor::Descriptor;
using kor::DescriptorSet;

namespace {

using Pixel = kor::U8Vec4; // RGBA8

constexpr std::uint32_t kW = 16;
constexpr std::uint32_t kH = 16;

// Render a full-screen green triangle into an offscreen RGBA8 image and verify
// the rasterizer filled every texel. This is the first test to drive a graphics
// pipeline end-to-end without a swap chain.
TEST_F(GpuTest, OffscreenTriangleFillsTarget) {
    // --- offscreen color target ------------------------------------------
    Image::Builder ib;
    ib.SetType(Image::Type::e2D)
      .SetFormat(Image::Format::eRGBA8_UNORM)
      .SetExtent(kor::UVec2{kW, kH})
      .SetUsage(Image::Usage::eColorAttachment | Image::Usage::eTransferSrc);    // read back afterwards
    auto colorImage = ib.Build();

    auto colorView = ImageView::Builder(colorImage)
                         .Build();

    auto framebuffer =
        Framebuffer::Builder{}
            .AddColor({ .view = colorView, .clear = kor::Vec4{0.f, 0.f, 0.f, 1.f} })
            .Build();

    // --- shaders + graphics pipeline -------------------------------------
    const ResourceRef<const Shader> vert =
        Shader::Builder{}
            .SetLang<Shader::Lang::eGLSL>()
            .SetStage(Shader::Stage::eVertex)
            .SetPath(kor::ShaderPath("flatTriangle.vert.glsl"))
            .GetOrBuild("test.flatTriangle.vert");

    const ResourceRef<const Shader> frag =
        Shader::Builder{}
            .SetLang<Shader::Lang::eGLSL>()
            .SetStage(Shader::Stage::eFragment)
            .SetPath(kor::ShaderPath("flatTriangle.frag.glsl"))
            .GetOrBuild("test.flatTriangle.frag");

    auto pipeline =
        GraphicsPipeline::Builder{}
            .SetVertexShader(vert)
            .SetFragmentShader(frag)
            .SetFramebuffer(framebuffer)
            .Build();

    // --- record the draw --------------------------------------------------
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        // BeginRendering clears any bound pipeline, so bind inside the render scope.
        cb.BeginRendering(framebuffer);
        cb.BindGraphicsPipeline(pipeline);
        cb.SetViewport(0, 0, kW, kH);
        cb.SetScissor(0, 0, kW, kH);
        cb.Draw(3); // full-screen triangle, no vertex/descriptor inputs
        cb.EndRendering();
    }, CommandBuffer::Usage::eGraphics).Wait();

    // --- read the target back and verify ---------------------------------
    Buffer::RawBuilder rb;
    rb.SetRawSize(static_cast<kor::i64>(kW) * kH * sizeof(Pixel))
      .SetUsage(Buffer::Usage::eTransferDst)
      .SetType(Buffer::Type::eReadback);
    auto readback = rb.Build();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(colorImage, readback);
    }, CommandBuffer::Usage::eTransfer).Wait();

    const std::vector<Pixel> out = readback->Read<Pixel>();
    ASSERT_EQ(out.size(), static_cast<std::size_t>(kW) * kH);
    for (std::size_t i = 0; i < out.size(); ++i) {
        EXPECT_EQ(out[i].x, 0)   << "texel " << i << " r";
        EXPECT_EQ(out[i].y, 255) << "texel " << i << " g";
        EXPECT_EQ(out[i].z, 0)   << "texel " << i << " b";
        EXPECT_EQ(out[i].w, 255) << "texel " << i << " a";
    }
}

// Second graphics test: same full-screen triangle, but clip it to a sub-rect
// with SetScissor and drive a handful of the dynamic-state setters that no other
// test touches (SetCullMode / SetFrontFace / SetDepth*/SetRasterizerDiscardEnable).
// Only the scissored region must come out green; everything else keeps the black
// clear color. This proves scissor clipping works and that the dynamic-state
// commands record without disturbing the draw.
TEST_F(GpuTest, ScissorAndDynamicStateClipDraw) {
    constexpr std::uint32_t kSx = 4, kSy = 4, kSw = 8, kSh = 8;

    Image::Builder ib;
    ib.SetType(Image::Type::e2D)
      .SetFormat(Image::Format::eRGBA8_UNORM)
      .SetExtent(kor::UVec2{kW, kH})
      .SetUsage(Image::Usage::eColorAttachment | Image::Usage::eTransferSrc);
    auto colorImage = ib.Build();

    auto colorView = ImageView::Builder(colorImage).Build();
    auto framebuffer =
        Framebuffer::Builder{}
            .AddColor({ .view = colorView, .clear = kor::Vec4{0.f, 0.f, 0.f, 1.f} })
            .Build();

    const ResourceRef<const Shader> vert =
        Shader::Builder{}.SetLang<Shader::Lang::eGLSL>().SetStage(Shader::Stage::eVertex)
            .SetPath(kor::ShaderPath("flatTriangle.vert.glsl")).GetOrBuild("test.flatTriangle.vert");
    const ResourceRef<const Shader> frag =
        Shader::Builder{}.SetLang<Shader::Lang::eGLSL>().SetStage(Shader::Stage::eFragment)
            .SetPath(kor::ShaderPath("flatTriangle.frag.glsl")).GetOrBuild("test.flatTriangle.frag");

    auto pipeline =
        GraphicsPipeline::Builder{}
            .SetVertexShader(vert)
            .SetFragmentShader(frag)
            .SetFramebuffer(framebuffer)
            .Build();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginRendering(framebuffer);
        cb.BindGraphicsPipeline(pipeline);
        cb.SetViewport(0, 0, kW, kH);
        cb.SetScissor(kSx, kSy, kSw, kSh); // clip the draw to a sub-rect
        // Exercise the dynamic-state railway. None of these change the visible
        // result (no culling, depth disabled, discard off) but they must record.
        cb.SetFrontFace(kor::FrontFace::eCounterClockwise);
        cb.SetDepthTestEnable(false);
        cb.SetDepthWriteEnable(false);
        cb.SetRasterizerDiscardEnable(false);
        cb.Draw(3);
        cb.EndRendering();
    }, CommandBuffer::Usage::eGraphics).Wait();

    Buffer::RawBuilder rb;
    rb.SetRawSize(static_cast<kor::i64>(kW) * kH * sizeof(Pixel))
      .SetUsage(Buffer::Usage::eTransferDst)
      .SetType(Buffer::Type::eReadback);
    auto readback = rb.Build();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(colorImage, readback);
    }, CommandBuffer::Usage::eTransfer).Wait();

    const std::vector<Pixel> out = readback->Read<Pixel>();
    ASSERT_EQ(out.size(), static_cast<std::size_t>(kW) * kH);
    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            const bool inScissor = x >= kSx && x < kSx + kSw && y >= kSy && y < kSy + kSh;
            const Pixel& got = out[y * kW + x];
            if (inScissor) {
                EXPECT_EQ(got.y, 255) << "scissored texel (" << x << "," << y << ") should be green";
                EXPECT_EQ(got.x, 0)   << "scissored texel (" << x << "," << y << ") r";
            } else {
                EXPECT_EQ(got.y, 0) << "clipped texel (" << x << "," << y << ") should stay black";
                EXPECT_EQ(got.x, 0) << "clipped texel (" << x << "," << y << ") r";
            }
        }
    }
}

// Render into a color+depth framebuffer with an explicit color-blend attachment
// state and depth testing enabled. This drives the graphics-pipeline branches for
// the depth attachment format, the explicit (non-default) blend attachment loop,
// and the depth/stencil state — none of which the plain color-only test reaches.
TEST_F(GpuTest, OffscreenColorDepthBlend) {
    auto color = Image::Builder{}
                     .SetType(Image::Type::e2D)
                     .SetFormat(Image::Format::eRGBA8_UNORM)
                     .SetExtent(kor::UVec2{kW, kH})
                     .SetUsage(Image::Usage::eColorAttachment | Image::Usage::eTransferSrc)
                     .Build();
    auto depth = Image::Builder{}
                     .SetType(Image::Type::e2D)
                     .SetFormat(Image::Format::eD32_SFLOAT)
                     .SetExtent(kor::UVec2{kW, kH})
                     .SetUsage(Image::Usage::eDepthStencilAttachment)
                     .Build();

    auto colorView = ImageView::Builder(color).Build();
    auto depthView = ImageView::Builder(depth).Build();

    auto framebuffer = Framebuffer::Builder{}
                           .AddColor({ .view = colorView, .clear = kor::Vec4{0.f, 0.f, 0.f, 1.f} })
                           .SetDepth({ .view = depthView, .depth = 1.f })
                           .Build();

    const ResourceRef<const Shader> vert =
        Shader::Builder{}.SetLang<Shader::Lang::eGLSL>().SetStage(Shader::Stage::eVertex)
            .SetPath(kor::ShaderPath("flatTriangle.vert.glsl")).GetOrBuild("test.flatTriangle.vert");
    const ResourceRef<const Shader> frag =
        Shader::Builder{}.SetLang<Shader::Lang::eGLSL>().SetStage(Shader::Stage::eFragment)
            .SetPath(kor::ShaderPath("flatTriangle.frag.glsl")).GetOrBuild("test.flatTriangle.frag");

    // Explicit alpha-blend attachment state + depth test on.
    kor::ColorBlendState blend;
    blend.attachments.push_back(kor::ColorBlendState::AttachmentState{
        .blendEnable = true,
        .srcColorBlendFactor = kor::BlendFactor::eSrcAlpha,
        .dstColorBlendFactor = kor::BlendFactor::eOneMinusSrcAlpha,
    });
    kor::DepthStencilState depthState;
    depthState.depthTestEnable = true;
    depthState.depthWriteEnable = true;
    depthState.depthCompareOp = kor::CompareOp::eLessOrEqual;

    auto pipeline = GraphicsPipeline::Builder{}
                        .SetVertexShader(vert)
                        .SetFragmentShader(frag)
                        .SetFramebuffer(framebuffer)
                        .SetColorBlendState(blend)
                        .SetDepthStencilState(depthState)
                        .Build();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginRendering(framebuffer);
        cb.BindGraphicsPipeline(pipeline);
        cb.SetViewport(0, 0, kW, kH);
        cb.SetScissor(0, 0, kW, kH);
        cb.Draw(3);
        cb.EndRendering();
    }, CommandBuffer::Usage::eGraphics).Wait();

    Buffer::RawBuilder rb;
    rb.SetRawSize(static_cast<kor::i64>(kW) * kH * sizeof(Pixel))
      .SetUsage(Buffer::Usage::eTransferDst)
      .SetType(Buffer::Type::eReadback);
    auto readback = rb.Build();
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(color, readback);
    }, CommandBuffer::Usage::eTransfer).Wait();

    const std::vector<Pixel> out = readback->Read<Pixel>();
    ASSERT_EQ(out.size(), static_cast<std::size_t>(kW) * kH);
    // Opaque green (alpha 1) over black with src-alpha blending stays green.
    for (std::size_t i = 0; i < out.size(); ++i) {
        EXPECT_EQ(out[i].y, 255) << "texel " << i << " g";
    }
}

// Draw an indexed triangle mesh (vertex buffer + index buffer) through a graphics
// pipeline whose vertex-input state is derived from the mesh layout. Covers the
// pipeline's vertex binding/attribute path plus BindMesh + DrawIndexed and the
// buffer vertex/index barrier paths, none of which the vertex-index-only tests hit.
TEST_F(GpuTest, MeshIndexedDraw) {
    using PosVertex = kmesh::ParamVertex<kmesh::Position>;
    using PosMesh = kmesh::ParamMesh<PosVertex>;

    std::vector<PosVertex> verts = {
        PosVertex{ kor::Vec3{-1.0f, -1.0f, 0.0f} },
        PosVertex{ kor::Vec3{ 3.0f, -1.0f, 0.0f} },
        PosVertex{ kor::Vec3{-1.0f,  3.0f, 0.0f} },
    };
    std::vector<std::uint32_t> indices = {0, 1, 2};
    auto mesh = PosMesh::Create(verts, indices);
    ASSERT_TRUE(static_cast<bool>(mesh));

    auto color = Image::Builder{}
                     .SetType(Image::Type::e2D)
                     .SetFormat(Image::Format::eRGBA8_UNORM)
                     .SetExtent(kor::UVec2{kW, kH})
                     .SetUsage(Image::Usage::eColorAttachment | Image::Usage::eTransferSrc)
                     .Build();
    auto colorView = ImageView::Builder(color).Build();
    auto framebuffer = Framebuffer::Builder{}
                           .AddColor({ .view = colorView, .clear = kor::Vec4{0.f, 0.f, 0.f, 1.f} })
                           .Build();

    const ResourceRef<const Shader> vert =
        Shader::Builder{}.SetLang<Shader::Lang::eGLSL>().SetStage(Shader::Stage::eVertex)
            .SetPath(kor::ShaderPath("meshTriangle.vert.glsl")).GetOrBuild("test.meshTriangle.vert");
    const ResourceRef<const Shader> frag =
        Shader::Builder{}.SetLang<Shader::Lang::eGLSL>().SetStage(Shader::Stage::eFragment)
            .SetPath(kor::ShaderPath("flatTriangle.frag.glsl")).GetOrBuild("test.flatTriangle.frag");

    auto pipeline = GraphicsPipeline::Builder{}
                        .SetVertexShader(vert, PosMesh::Layout()) // vertex-input state from the mesh layout
                        .SetFragmentShader(frag)
                        .SetFramebuffer(framebuffer)
                        .Build();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginRendering(framebuffer);
        cb.BindGraphicsPipeline(pipeline);
        cb.SetViewport(0, 0, kW, kH);
        cb.SetScissor(0, 0, kW, kH);
        cb.BindMesh(mesh);
        cb.DrawIndexed(); // uses the bound mesh's index count
        cb.EndRendering();
    }, CommandBuffer::Usage::eGraphics).Wait();

    Buffer::RawBuilder rb;
    rb.SetRawSize(static_cast<kor::i64>(kW) * kH * sizeof(Pixel))
      .SetUsage(Buffer::Usage::eTransferDst)
      .SetType(Buffer::Type::eReadback);
    auto readback = rb.Build();
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(color, readback);
    }, CommandBuffer::Usage::eTransfer).Wait();

    const std::vector<Pixel> out = readback->Read<Pixel>();
    ASSERT_EQ(out.size(), static_cast<std::size_t>(kW) * kH);
    int green = 0;
    for (const auto& p : out) if (p.y == 255) ++green;
    EXPECT_GT(green, 0) << "the mesh triangle should have covered some pixels";
}

// Pins the canonical clip-space orientation: Y points down in NDC, so clip y = -1 is the TOP of
// the image and must land in memory row 0. Everything downstream depends on this — the swapchain
// blit presents row 0 at the top of the window without flipping anything.
//
// The geometry is deliberately asymmetric in Y: a quad covering the top half of clip space only.
// A full-screen triangle cannot detect a flip (it is symmetric), which is why the older
// ScissorAndDynamicStateClipDraw and a scissor-only check both pass even with an inverted
// viewport. Reintroduce a negative-height viewport and this test fails.
TEST_F(GpuTest, CanonicalOrientationPutsClipTopInRowZero) {
    using PosVertex = kmesh::ParamVertex<kmesh::Position>;
    using PosMesh = kmesh::ParamMesh<PosVertex>;

    // Clip y in [-1, 0] = the top half under a Y-down NDC; full width.
    std::vector<PosVertex> verts = {
        PosVertex{ kor::Vec3{-1.0f, -1.0f, 0.0f} },
        PosVertex{ kor::Vec3{ 1.0f, -1.0f, 0.0f} },
        PosVertex{ kor::Vec3{ 1.0f,  0.0f, 0.0f} },
        PosVertex{ kor::Vec3{-1.0f,  0.0f, 0.0f} },
    };
    std::vector<std::uint32_t> indices = {0, 1, 2, 0, 2, 3};
    auto mesh = PosMesh::Create(verts, indices);
    ASSERT_TRUE(static_cast<bool>(mesh));

    auto color = Image::Builder{}
                     .SetType(Image::Type::e2D)
                     .SetFormat(Image::Format::eRGBA8_UNORM)
                     .SetExtent(kor::UVec2{kW, kH})
                     .SetUsage(Image::Usage::eColorAttachment | Image::Usage::eTransferSrc)
                     .Build();
    auto colorView = ImageView::Builder(color).Build();
    auto framebuffer = Framebuffer::Builder{}
                           .AddColor({ .view = colorView, .clear = kor::Vec4{0.f, 0.f, 0.f, 1.f} })
                           .Build();

    const ResourceRef<const Shader> vert =
        Shader::Builder{}.SetPath("meshTriangle.vert.glsl").GetOrBuild("orient.meshTriangle.vert");
    const ResourceRef<const Shader> frag =
        Shader::Builder{}.SetPath("flatTriangle.frag.glsl").GetOrBuild("orient.flatTriangle.frag");

    auto pipeline = GraphicsPipeline::Builder{}
                        .SetVertexShader(vert, PosMesh::Layout())
                        .SetFragmentShader(frag)
                        .SetFramebuffer(framebuffer)
                        .Build();   // culling is off by default, so winding cannot mask the result
    ASSERT_TRUE(pipeline.Valid()) << pipeline.Failure()->History();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginRendering(framebuffer);
        cb.BindGraphicsPipeline(pipeline);
        cb.SetViewport(0, 0, kW, kH);
        cb.SetScissor(0, 0, kW, kH);
        cb.BindMesh(mesh);
        cb.DrawIndexed();
        cb.EndRendering();
    }, CommandBuffer::Usage::eGraphics).Wait();

    Buffer::RawBuilder rb;
    rb.SetRawSize(static_cast<kor::i64>(kW) * kH * sizeof(Pixel))
      .SetUsage(Buffer::Usage::eTransferDst)
      .SetType(Buffer::Type::eReadback);
    auto readback = rb.Build();
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(color, readback);
    }, CommandBuffer::Usage::eTransfer).Wait();

    const std::vector<Pixel> out = readback->Read<Pixel>();
    ASSERT_EQ(out.size(), static_cast<std::size_t>(kW) * kH);

    // Sanity: the draw must have covered something, or the assertions below pass vacuously.
    int green = 0;
    for (const auto& p : out) if (p.y == 255) ++green;
    ASSERT_GT(green, 0) << "the quad covered nothing; the orientation check would be vacuous";

    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            const Pixel& got = out[y * kW + x];
            if (y < kH / 2) {
                EXPECT_EQ(got.y, 255)
                    << "row " << y << " is in the top half of clip space and must be green; "
                       "green in the bottom rows instead means Y is inverted";
            } else {
                EXPECT_EQ(got.y, 0) << "row " << y << " is below the quad and must stay black";
            }
        }
    }
}


// A hazard that no amount of barrier placement can fix: a draw samples the very image the
// open render pass is rendering into. The transition it needs cannot go inside the pass
// (Vulkan forbids it) and cannot go in front of the pass either, because that is before the
// write it would have to wait on. The engine has to say so rather than emit a barrier
// somewhere harmless-looking, which is what it used to do.
TEST_F(GpuTest, FeedbackLoopInsideRenderPassIsReported) {
    constexpr kor::u32 kSize = 16;

    Image::Builder ib;
    ib.SetFormat(Image::Format::eRGBA8_UNORM)
      .SetExtent(kor::UVec2{kSize, kSize})
      .SetUsage(Image::Usage::eColorAttachment | Image::Usage::eSampled);
    auto colorImage = ib.Build();
    ASSERT_TRUE(colorImage.Valid());

    auto colorView = ImageView::Builder(colorImage).Build();
    auto framebuffer = Framebuffer::Builder{}
                           .AddColor({ .view = colorView, .clear = kor::Vec4{0.f, 0.f, 0.f, 1.f} })
                           .Build();
    auto sampler = Sampler::Builder{}.Build();
    ASSERT_TRUE(sampler.Valid());

    const ResourceRef<const Shader> vert =
        Shader::Builder{}.SetLang<Shader::Lang::eGLSL>().SetStage(Shader::Stage::eVertex)
            .SetPath(kor::ShaderPath("sampleTexture.vert.glsl")).GetOrBuild("test.feedback.vert");
    const ResourceRef<const Shader> frag =
        Shader::Builder{}.SetLang<Shader::Lang::eGLSL>().SetStage(Shader::Stage::eFragment)
            .SetPath(kor::ShaderPath("sampleTexture.frag.glsl")).GetOrBuild("test.feedback.frag");

    auto pipeline = GraphicsPipeline::Builder{}
                        .SetVertexShader(vert)
                        .SetFragmentShader(frag)
                        .SetFramebuffer(framebuffer)
                        .Build();
    ASSERT_TRUE(pipeline.Valid());

    // The attachment, bound as a texture to the pipeline drawing into it.
    auto descriptorSet = DescriptorSet::Builder(pipeline, 0)
                             .Write(0, colorView, sampler)
                             .Build();
    ASSERT_TRUE(descriptorSet.Valid());

    const auto cb = CommandBuffer::Create(CommandBuffer::Usage::eGraphics);
    cb->Begin();
    cb->BeginRendering(framebuffer);
    cb->BindGraphicsPipeline(pipeline);
    cb->SetViewport(0, 0, kSize, kSize);
    cb->SetScissor(0, 0, kSize, kSize);
    cb->BindDescriptorSet(0, descriptorSet);
    cb->Draw(3);
    cb->EndRendering();
    cb->End();   // resolution, and therefore the diagnostic, happens here

    bool reported = false;
    std::string message;
    for (const auto& error : cb->Errors()) {
        if (error.code == kor::ErrorCode::eMissingBarrier) {
            reported = true;
            message = error.message;
        }
    }

    ASSERT_TRUE(reported) << "the engine hoisted a barrier that synchronises nothing "
                             "instead of reporting an unplaceable one";
    EXPECT_NE(message.find("BeginRendering"), std::string::npos) << message;
    EXPECT_NE(message.find("Draw"), std::string::npos) << message;
    // Named as the feedback loop it is, rather than as a barrier-placement problem: there is
    // no ordering that makes sampling the attachment you are rendering into legal.
    EXPECT_NE(message.find("rendering into it"), std::string::npos) << message;
}


// A draw with no viewport set covers the framebuffer it renders into — *not* the window.
//
// The default used to be the window's extent, so a pass rendering into a target of its own was
// rasterised for a surface it was not: inside a small target you saw a crop of a picture drawn at
// window scale, and the target's size appeared to do nothing. Checked by rendering a full-screen
// triangle into a target much smaller than the window and requiring it to cover every texel.
TEST_F(GpuTest, ADrawWithNoViewportCoversItsOwnFramebuffer) {
    // Deliberately far from any plausible window size, so a window-derived viewport would show.
    constexpr std::uint32_t kSize = 37;

    auto color = Image::Builder{}
        .SetType(Image::Type::e2D)
        .SetFormat(Image::Format::eRGBA8_UNORM)
        .SetExtent(kor::UVec2{kSize, kSize})
        .SetUsage(Image::Usage::eColorAttachment | Image::Usage::eTransferSrc)
        .Build();
    auto colorView = ImageView::Builder(color).Build();
    auto framebuffer = Framebuffer::Builder{}
        .AddColor({ .view = colorView, .clear = kor::Vec4{0.f, 0.f, 0.f, 1.f} })
        .Build();

    const ResourceRef<const Shader> vert = Shader::Builder{}
        .SetLang<Shader::Lang::eGLSL>().SetStage(Shader::Stage::eVertex)
        .SetPath(kor::ShaderPath("flatTriangle.vert.glsl")).GetOrBuild("test.flatTriangle.vert");
    const ResourceRef<const Shader> frag = Shader::Builder{}
        .SetLang<Shader::Lang::eGLSL>().SetStage(Shader::Stage::eFragment)
        .SetPath(kor::ShaderPath("flatTriangle.frag.glsl")).GetOrBuild("test.flatTriangle.frag");

    auto pipeline = GraphicsPipeline::Builder{}
        .SetVertexShader(vert)
        .SetFragmentShader(frag)
        .SetFramebuffer(framebuffer)
        .Build();
    ASSERT_TRUE(static_cast<bool>(pipeline)) << (pipeline.Failure() ? pipeline.Failure()->message : "");

    Buffer::RawBuilder rb;
    rb.SetRawSize(static_cast<kor::i64>(kSize) * kSize * 4)
      .SetUsage(Buffer::Usage::eTransferDst)
      .SetType(Buffer::Type::eReadback);
    auto readback = rb.Build();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginRendering(framebuffer)
          .BindGraphicsPipeline(pipeline)
          // No SetViewport, no SetScissor: that is the case under test.
          .Draw(3)
          .EndRendering();
        cb.CopyImageToBuffer(color, readback);
    }, CommandBuffer::Usage::eGraphics).Wait();

    const auto texels = readback->Read<kor::U8Vec4>();
    ASSERT_EQ(texels.size(), static_cast<std::size_t>(kSize) * kSize);

    // The shader paints the whole clip volume, so every texel of *this* target must be painted. With a
    // window-sized viewport only the top-left corner of the triangle would land here, leaving the far
    // side of the image at the clear colour.
    std::size_t painted = 0;
    for (const auto& texel : texels) if (texel != kor::U8Vec4(0, 0, 0, 255)) ++painted;
    EXPECT_EQ(painted, texels.size()) << "the draw did not cover its own framebuffer";
}

// Resizing a framebuffer resizes what it renders into.
//
// It used to resize nothing at all: the base implementation was empty and only the *default*
// framebuffer's override did anything, because there the swap chain owns the images. So a scene
// following a viewport's size called Resize every frame and kept rendering at its original one.
TEST_F(GpuTest, FramebufferResizeResizesItsAttachments) {
    constexpr std::uint32_t kFirst = 64;
    constexpr std::uint32_t kSecond = 96;

    auto color = Image::Builder{}
        .SetType(Image::Type::e2D)
        .SetFormat(Image::Format::eRGBA8_UNORM)
        .SetExtent(kor::UVec2{kFirst, kFirst})
        .SetUsage(Image::Usage::eColorAttachment | Image::Usage::eTransferSrc)
        .Build();
    auto depth = Image::Builder{}
        .SetType(Image::Type::e2D)
        .SetFormat(Image::Format::eD32_SFLOAT)
        .SetExtent(kor::UVec2{kFirst, kFirst})
        .SetUsage(Image::Usage::eDepthStencilAttachment)
        .Build();

    auto colorView = ImageView::Builder(color).Build();
    auto depthView = ImageView::Builder(depth).Build();

    auto framebuffer = Framebuffer::Builder{}
        .AddColor({ .view = colorView, .clear = kor::Vec4{0.f, 0.f, 0.f, 1.f} })
        .SetDepth({ .view = depthView })
        .Build();
    ASSERT_TRUE(static_cast<bool>(framebuffer));
    const auto generationBefore = color->Generation();

    framebuffer->Resize(kor::UVec2{kSecond, kSecond});

    EXPECT_EQ(framebuffer->Extent(), kor::UVec2(kSecond, kSecond));
    EXPECT_EQ(color->Extent(), kor::UVec3(kSecond, kSecond, 1)) << "the colour attachment followed";
    EXPECT_EQ(depth->Extent(), kor::UVec3(kSecond, kSecond, 1)) << "and so did the depth one";
    // The image was *replaced*, which is what tells a view holding the old one to rebuild.
    EXPECT_GT(color->Generation(), generationBefore);

    // And it is usable at the new size: rendering into it and reading it back is the whole point of
    // having resized it. A view that had not noticed the replacement would fault here.
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginRendering(framebuffer);
        cb.EndRendering();
    }, CommandBuffer::Usage::eGraphics).Wait();

    Buffer::RawBuilder rb;
    rb.SetRawSize(static_cast<kor::i64>(kSecond) * kSecond * 4)
      .SetUsage(Buffer::Usage::eTransferDst)
      .SetType(Buffer::Type::eReadback);
    auto readback = rb.Build();
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(color, readback);
    }, CommandBuffer::Usage::eTransfer).Wait();

    const auto texels = readback->Read<kor::U8Vec4>();
    ASSERT_EQ(texels.size(), static_cast<std::size_t>(kSecond) * kSecond);
    // Cleared to the framebuffer's own colour, at the new size.
    EXPECT_EQ(texels.front(), kor::U8Vec4(0, 0, 0, 255));
}

// Resizing to the size it already is changes nothing, so a scene may call it every frame.
TEST_F(GpuTest, FramebufferResizeToTheSameSizeIsANoOp) {
    auto color = Image::Builder{}
        .SetType(Image::Type::e2D)
        .SetFormat(Image::Format::eRGBA8_UNORM)
        .SetExtent(kor::UVec2{32, 32})
        .SetUsage(Image::Usage::eColorAttachment)
        .Build();
    auto colorView = ImageView::Builder(color).Build();
    auto framebuffer = Framebuffer::Builder{}
        .AddColor({ .view = colorView, .clear = kor::Vec4{0.f} })
        .Build();

    const auto generation = color->Generation();
    framebuffer->Resize(kor::UVec2{32, 32});
    framebuffer->Resize(kor::UVec2{0, 16});     // a zero extent names nothing and is ignored
    EXPECT_EQ(color->Generation(), generation) << "the image was not replaced";
    EXPECT_EQ(color->Extent(), kor::UVec3(32, 32, 1));
}

// Push constants addressed by name, declared once in a shared header and read by two stages.
//
// The vertex stage reads `offset` and the fragment stage reads `color`, out of one block — so the
// pipeline's merge has to union the two stages' declarations rather than treat them as rivals, and
// each constant has to be written at the offset the compiler chose without the CPU ever naming a
// byte. The shifted triangle proves the vertex half landed and the colour proves the fragment half.
TEST_F(GpuTest, PushConstantsByNameAcrossStages) {
    auto color = Image::Builder{}
                     .SetType(Image::Type::e2D)
                     .SetFormat(Image::Format::eRGBA8_UNORM)
                     .SetExtent(kor::UVec2{kW, kH})
                     .SetUsage(Image::Usage::eColorAttachment | Image::Usage::eTransferSrc)
                     .Build();
    auto colorView = ImageView::Builder(color).Build();
    auto framebuffer = Framebuffer::Builder{}
                           .AddColor({ .view = colorView, .clear = kor::Vec4{0.f, 0.f, 0.f, 1.f} })
                           .Build();

    const ResourceRef<const Shader> vert =
        Shader::Builder{}.SetPath("pushMultiStage.vert.glsl").GetOrBuild("push.multi.vert");
    const ResourceRef<const Shader> frag =
        Shader::Builder{}.SetPath("pushMultiStage.frag.glsl").GetOrBuild("push.multi.frag");

    auto pipeline = GraphicsPipeline::Builder{}
                        .SetVertexShader(vert)
                        .SetFragmentShader(frag)
                        .SetFramebuffer(framebuffer)
                        .Build();
    ASSERT_TRUE(pipeline.Valid()) << (pipeline.Failure() ? pipeline.Failure()->History() : "");

    // Both constants are on the pipeline, each with the stage that reads it.
    const auto* offsetConstant = pipeline->FindPushConstant("offset");
    const auto* colorConstant = pipeline->FindPushConstant("color");
    ASSERT_NE(offsetConstant, nullptr);
    ASSERT_NE(colorConstant, nullptr);
    EXPECT_EQ(offsetConstant->size, sizeof(kor::Vec2));
    EXPECT_EQ(colorConstant->size, sizeof(kor::Vec4));
    EXPECT_NE(colorConstant->offset, offsetConstant->offset) << "two constants cannot share bytes";
    EXPECT_TRUE(offsetConstant->stages & Shader::Stage::eVertex);
    EXPECT_TRUE(colorConstant->stages & Shader::Stage::eFragment);

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginRendering(framebuffer);
        cb.BindGraphicsPipeline(pipeline);
        cb.SetViewport(0, 0, kW, kH);
        cb.SetScissor(0, 0, kW, kH);
        // No offsets, no struct mirroring the block: the names are the whole contract.
        cb.PushConstant("offset", kor::Vec2{2.f, 0.f});   // shifts the triangle off to the right
        cb.PushConstant("color", kor::Vec4{0.f, 0.f, 1.f, 1.f});
        cb.Draw(3);
        cb.EndRendering();
    }, CommandBuffer::Usage::eGraphics).Wait();

    Buffer::RawBuilder rb;
    rb.SetRawSize(static_cast<kor::i64>(kW) * kH * sizeof(Pixel))
      .SetUsage(Buffer::Usage::eTransferDst)
      .SetType(Buffer::Type::eReadback);
    auto shifted = rb.Build();
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(color, shifted);
    }, CommandBuffer::Usage::eTransfer).Wait();

    // Shifted two clip units right, the triangle misses the target entirely: the vertex stage
    // really did read its half of the block.
    const std::vector<Pixel> offscreen = shifted->Read<Pixel>();
    EXPECT_EQ(offscreen.front(), Pixel(0, 0, 0, 255)) << "the vertex push constant was ignored";

    // Again with no shift: now the triangle covers everything, in the fragment stage's colour.
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginRendering(framebuffer);
        cb.BindGraphicsPipeline(pipeline);
        cb.SetViewport(0, 0, kW, kH);
        cb.SetScissor(0, 0, kW, kH);
        cb.PushConstant("offset", kor::Vec2{0.f, 0.f});
        cb.PushConstant("color", kor::Vec4{0.f, 0.f, 1.f, 1.f});
        cb.Draw(3);
        cb.EndRendering();
    }, CommandBuffer::Usage::eGraphics).Wait();

    auto covered = rb.Build();
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(color, covered);
    }, CommandBuffer::Usage::eTransfer).Wait();

    const std::vector<Pixel> out = covered->Read<Pixel>();
    ASSERT_EQ(out.size(), static_cast<std::size_t>(kW) * kH);
    for (std::size_t i = 0; i < out.size(); ++i) {
        EXPECT_EQ(out[i], Pixel(0, 0, 255, 255)) << "texel " << i;
    }
}

// A name the pipeline does not declare, or a value of the wrong size, fails the recording where it
// was written — rather than writing the wrong bytes to whatever happens to sit at that offset.
TEST_F(GpuTest, PushConstantByNameRejectsWhatTheShaderDoesNotDeclare) {
    auto color = Image::Builder{}
                     .SetType(Image::Type::e2D)
                     .SetFormat(Image::Format::eRGBA8_UNORM)
                     .SetExtent(kor::UVec2{kW, kH})
                     .SetUsage(Image::Usage::eColorAttachment)
                     .Build();
    auto colorView = ImageView::Builder(color).Build();
    auto framebuffer = Framebuffer::Builder{}
                           .AddColor({ .view = colorView, .clear = kor::Vec4{0.f} })
                           .Build();

    const ResourceRef<const Shader> vert =
        Shader::Builder{}.SetPath("pushMultiStage.vert.glsl").GetOrBuild("push.multi.vert");
    const ResourceRef<const Shader> frag =
        Shader::Builder{}.SetPath("pushMultiStage.frag.glsl").GetOrBuild("push.multi.frag");
    auto pipeline = GraphicsPipeline::Builder{}
                        .SetVertexShader(vert)
                        .SetFragmentShader(frag)
                        .SetFramebuffer(framebuffer)
                        .Build();
    ASSERT_TRUE(pipeline.Valid());

    EXPECT_EQ(pipeline->FindPushConstant("tint"), nullptr);

    const auto unknown = CommandBuffer::Create(CommandBuffer::Usage::eGraphics);
    unknown->Begin();
    unknown->BeginRendering(framebuffer);
    unknown->BindGraphicsPipeline(pipeline);
    unknown->PushConstant("tint", kor::Vec4{1.f});
    unknown->EndRendering();
    unknown->End();

    ASSERT_FALSE(unknown->Errors().empty());
    EXPECT_EQ(unknown->Errors().front().code, kor::ErrorCode::ePushConstantMismatch);
    // The message has to name what there *is*, since the usual cause is a rename on one side.
    EXPECT_NE(unknown->Errors().front().message.find("color"), std::string::npos)
        << unknown->Errors().front().message;

    const auto wrongSize = CommandBuffer::Create(CommandBuffer::Usage::eGraphics);
    wrongSize->Begin();
    wrongSize->BeginRendering(framebuffer);
    wrongSize->BindGraphicsPipeline(pipeline);
    wrongSize->PushConstant("color", kor::Vec2{1.f});   // the shader declares a vec4
    wrongSize->EndRendering();
    wrongSize->End();

    ASSERT_FALSE(wrongSize->Errors().empty());
    EXPECT_EQ(wrongSize->Errors().front().code, kor::ErrorCode::ePushConstantMismatch);

    // And with nothing bound to look the name up on.
    const auto unbound = CommandBuffer::Create(CommandBuffer::Usage::eGraphics);
    unbound->Begin();
    unbound->PushConstant("color", kor::Vec4{1.f});
    unbound->End();
    ASSERT_FALSE(unbound->Errors().empty());
    EXPECT_EQ(unbound->Errors().front().code, kor::ErrorCode::eNoPipelineBound);
}

// How far the by-name lookup reaches into a block: to its top-level members and no further.
//
// A nested struct and an array are each *one* constant — written whole, never field by field. That
// works because the size a member is declared with is the size that gets written: `Material` is 20
// bytes (vec4 + float) and the next member starts at 96, so the 12 bytes of alignment padding
// between them belong to nobody and are never touched. A C++ mirror of the struct has to be
// exactly those 20 bytes; when the shader's layout rules pad differently from C++'s — a vec4 after
// a float, where std430 aligns to 16 and C++ does not — the sizes disagree and the write is
// refused rather than landing half in the next constant.
TEST_F(GpuTest, NestedPushConstantMembersAreWholeConstants) {
    auto color = Image::Builder{}
                     .SetType(Image::Type::e2D)
                     .SetFormat(Image::Format::eRGBA8_UNORM)
                     .SetExtent(kor::UVec2{kW, kH})
                     .SetUsage(Image::Usage::eColorAttachment | Image::Usage::eTransferSrc)
                     .Build();
    auto colorView = ImageView::Builder(color).Build();
    auto framebuffer = Framebuffer::Builder{}
                           .AddColor({ .view = colorView, .clear = kor::Vec4{0.f, 0.f, 0.f, 1.f} })
                           .Build();

    const ResourceRef<const Shader> vert =
        Shader::Builder{}.SetPath("flatTriangle.vert.glsl").GetOrBuild("nested.flat.vert");
    const ResourceRef<const Shader> frag =
        Shader::Builder{}.SetPath("pushNested.frag.glsl").GetOrBuild("nested.push.frag");

    auto pipeline = GraphicsPipeline::Builder{}
                        .SetVertexShader(vert)
                        .SetFragmentShader(frag)
                        .SetFramebuffer(framebuffer)
                        .Build();
    ASSERT_TRUE(pipeline.Valid()) << (pipeline.Failure() ? pipeline.Failure()->History() : "");

    // Three constants, one per top-level member, whatever each one is made of.
    const auto* model = pipeline->FindPushConstant("model");
    const auto* material = pipeline->FindPushConstant("material");
    const auto* weights = pipeline->FindPushConstant("weights");
    ASSERT_NE(model, nullptr);
    ASSERT_NE(material, nullptr);
    ASSERT_NE(weights, nullptr);
    EXPECT_EQ(model->offset, 0u);
    EXPECT_EQ(model->size, 64u);                    // mat4
    EXPECT_EQ(material->offset, 64u);
    EXPECT_EQ(material->size, 20u);                 // vec4 + float, without the tail padding
    EXPECT_EQ(weights->offset, 96u);                // ...which the next member's offset does include
    EXPECT_EQ(weights->size, 16u);                  // float[4], the whole array

    // Nesting is flattened, so the inner fields are addressable by path — which is what makes a
    // struct whose C++ padding differs from the shader's a non-problem. The bare inner names are
    // not: a path is rooted at the block, so nothing is ambiguous between two structs.
    const auto* albedo = pipeline->FindPushConstant("material.albedo");
    const auto* roughness = pipeline->FindPushConstant("material.roughness");
    ASSERT_NE(albedo, nullptr);
    ASSERT_NE(roughness, nullptr);
    EXPECT_EQ(albedo->offset, 64u);
    EXPECT_EQ(albedo->size, 16u);
    EXPECT_EQ(roughness->offset, 80u);
    EXPECT_EQ(roughness->size, 4u);
    EXPECT_EQ(pipeline->FindPushConstant("albedo"), nullptr);
    EXPECT_EQ(pipeline->FindPushConstant("roughness"), nullptr);

    // Array elements likewise.
    const auto* thirdWeight = pipeline->FindPushConstant("weights[2]");
    ASSERT_NE(thirdWeight, nullptr);
    EXPECT_EQ(thirdWeight->offset, 104u);
    EXPECT_EQ(thirdWeight->size, 4u);

    // Written whole, at exactly the size the shader declared.
    const std::array<float, 5> materialValue{ 0.f, 0.f, 1.f, 1.f, 1.f };   // albedo = blue, roughness = 1
    const std::array<float, 4> weightsValue{ 0.f, 0.f, 1.f, 0.f };         // weights[2] = 1 -> alpha
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginRendering(framebuffer);
        cb.BindGraphicsPipeline(pipeline);
        cb.SetViewport(0, 0, kW, kH);
        cb.SetScissor(0, 0, kW, kH);
        cb.PushConstant("material", materialValue);
        cb.PushConstant("weights", weightsValue);
        cb.Draw(3);
        cb.EndRendering();
    }, CommandBuffer::Usage::eGraphics).Wait();

    Buffer::RawBuilder rb;
    rb.SetRawSize(static_cast<kor::i64>(kW) * kH * sizeof(Pixel))
      .SetUsage(Buffer::Usage::eTransferDst)
      .SetType(Buffer::Type::eReadback);
    auto readback = rb.Build();
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(color, readback);
    }, CommandBuffer::Usage::eTransfer).Wait();

    // Blue from the struct's albedo, opaque from the array's third element: both landed where the
    // shader reads them.
    EXPECT_EQ(readback->Read<Pixel>().front(), Pixel(0, 0, 255, 255));

    // The obvious C++ mirror of that struct is also exactly 20 bytes — kor's vectors carry no
    // extra alignment by default — so a nested struct is pushed as itself, not as a byte blob.
    struct CppMaterial { kor::Vec4 albedo; float roughness; };
    static_assert(sizeof(CppMaterial) == 20, "kor::Vec gained alignment; the mirror no longer matches");
    EXPECT_EQ(sizeof(CppMaterial), material->size);

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginRendering(framebuffer);
        cb.BindGraphicsPipeline(pipeline);
        cb.SetViewport(0, 0, kW, kH);
        cb.SetScissor(0, 0, kW, kH);
        cb.PushConstant("material", CppMaterial{ kor::Vec4{0.f, 1.f, 0.f, 1.f}, 1.f });
        cb.PushConstant("weights", weightsValue);
        cb.Draw(3);
        cb.EndRendering();
    }, CommandBuffer::Usage::eGraphics).Wait();

    auto asStruct = rb.Build();
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(color, asStruct);
    }, CommandBuffer::Usage::eTransfer).Wait();

    // Green this time, and still opaque: the struct went in whole and left `weights` alone.
    EXPECT_EQ(asStruct->Read<Pixel>().front(), Pixel(0, 255, 0, 255));

    // The same thing written field by field, which needs no agreement between the layouts at all.
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginRendering(framebuffer);
        cb.BindGraphicsPipeline(pipeline);
        cb.SetViewport(0, 0, kW, kH);
        cb.SetScissor(0, 0, kW, kH);
        cb.PushConstant("material.albedo", kor::Vec4{1.f, 0.f, 0.f, 1.f});
        cb.PushConstant("material.roughness", 1.f);
        cb.PushConstant("weights[2]", 1.f);
        cb.Draw(3);
        cb.EndRendering();
    }, CommandBuffer::Usage::eGraphics).Wait();

    auto byField = rb.Build();
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(color, byField);
    }, CommandBuffer::Usage::eTransfer).Wait();
    EXPECT_EQ(byField->Read<Pixel>().front(), Pixel(255, 0, 0, 255));
}

// A mat3 and an array of vec3 pushed from their obvious C++ spellings, which are tightly packed
// and 12 bytes shorter apiece than what the shader reserves. Nothing in the test says so: the
// strides come from reflection and the value is reassembled into them.
TEST_F(GpuTest, PushConstantsAreLaidOutIntoTheShadersPadding) {
    auto color = Image::Builder{}
                     .SetType(Image::Type::e2D)
                     .SetFormat(Image::Format::eRGBA8_UNORM)
                     .SetExtent(kor::UVec2{kW, kH})
                     .SetUsage(Image::Usage::eColorAttachment | Image::Usage::eTransferSrc)
                     .Build();
    auto colorView = ImageView::Builder(color).Build();
    auto framebuffer = Framebuffer::Builder{}
                           .AddColor({ .view = colorView, .clear = kor::Vec4{0.f, 0.f, 0.f, 1.f} })
                           .Build();

    const ResourceRef<const Shader> vert =
        Shader::Builder{}.SetPath("flatTriangle.vert.glsl").GetOrBuild("aligned.flat.vert");
    const ResourceRef<const Shader> frag =
        Shader::Builder{}.SetPath("pushAligned.frag.glsl").GetOrBuild("aligned.push.frag");

    auto pipeline = GraphicsPipeline::Builder{}
                        .SetVertexShader(vert)
                        .SetFragmentShader(frag)
                        .SetFramebuffer(framebuffer)
                        .Build();
    ASSERT_TRUE(pipeline.Valid()) << (pipeline.Failure() ? pipeline.Failure()->History() : "");

    // What the shader reserves, against what C++ would have handed over.
    const auto* basis = pipeline->FindPushConstant("basis");
    const auto* tints = pipeline->FindPushConstant("tints");
    ASSERT_NE(basis, nullptr);
    ASSERT_NE(tints, nullptr);
    EXPECT_EQ(basis->size, 48u) << "three columns of four floats";
    EXPECT_EQ(basis->matrixStride, 16u);
    EXPECT_EQ(sizeof(kor::Mat3), 36u) << "which is not what kor::Mat3 hands over";
    EXPECT_EQ(tints->arrayStride, 16u);
    EXPECT_EQ(sizeof(std::array<kor::Vec3, 3>), 36u);

    // Columns and elements chosen so every one of them contributes a distinct, exactly
    // representable amount: a value that landed in the wrong column cannot produce this colour.
    kor::Mat3 basisValue(0.f);
    basisValue[0] = kor::Vec3{0.2f, 0.f, 0.f};
    basisValue[1] = kor::Vec3{0.f, 0.4f, 0.f};
    basisValue[2] = kor::Vec3{0.f, 0.f, 0.6f};
    const std::array tintsValue{ kor::Vec3{0.2f, 0.f, 0.f}, kor::Vec3{0.f, 0.2f, 0.f}, kor::Vec3{0.f, 0.f, 0.2f} };

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginRendering(framebuffer);
        cb.BindGraphicsPipeline(pipeline);
        cb.SetViewport(0, 0, kW, kH);
        cb.SetScissor(0, 0, kW, kH);
        cb.PushConstant("basis", basisValue);
        cb.PushConstant("tints", tintsValue);
        cb.Draw(3);
        cb.EndRendering();
    }, CommandBuffer::Usage::eGraphics).Wait();

    Buffer::RawBuilder rb;
    rb.SetRawSize(static_cast<kor::i64>(kW) * kH * sizeof(Pixel))
      .SetUsage(Buffer::Usage::eTransferDst)
      .SetType(Buffer::Type::eReadback);
    auto readback = rb.Build();
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(color, readback);
    }, CommandBuffer::Usage::eTransfer).Wait();

    // (0.2 + 0.2, 0.4 + 0.2, 0.6 + 0.2) — every column and every element accounted for.
    EXPECT_EQ(readback->Read<Pixel>().front(), Pixel(102, 153, 204, 255));

    // A single element of the array addressed on its own, at the shader's stride.
    const auto* second = pipeline->FindPushConstant("tints[1]");
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(second->offset, tints->offset + 16u);
    EXPECT_EQ(second->size, 16u);
}

// Two stages that place one push constant differently poison the pipeline, naming the constant.
// Left to the driver this is silent: both stages read the same bytes, and one of them reads them
// as something they are not.
TEST_F(GpuTest, PipelinePoisonsWhenTwoStagesDeclareAPushConstantDifferently) {
    auto color = Image::Builder{}
                     .SetType(Image::Type::e2D)
                     .SetFormat(Image::Format::eRGBA8_UNORM)
                     .SetExtent(kor::UVec2{kW, kH})
                     .SetUsage(Image::Usage::eColorAttachment)
                     .Build();
    auto colorView = ImageView::Builder(color).Build();
    auto framebuffer = Framebuffer::Builder{}
                           .AddColor({ .view = colorView, .clear = kor::Vec4{0.f} })
                           .Build();

    const ResourceRef<const Shader> vert =
        Shader::Builder{}.SetPath("pushConflict.vert.glsl").GetOrBuild("push.conflict.vert");
    const ResourceRef<const Shader> frag =
        Shader::Builder{}.SetPath("pushConflict.frag.glsl").GetOrBuild("push.conflict.frag");
    ASSERT_TRUE(vert.Valid());
    ASSERT_TRUE(frag.Valid());

    const auto pipeline = GraphicsPipeline::Builder{}
                              .SetVertexShader(vert)
                              .SetFragmentShader(frag)
                              .SetFramebuffer(framebuffer)
                              .Build();

    ASSERT_TRUE(pipeline.Poisoned()) << "the stages disagree about 'tint' and nothing said so";
    EXPECT_EQ(pipeline.Failure()->code, kor::ErrorCode::ePushConstantMismatch);
    EXPECT_NE(pipeline.Failure()->message.find("tint"), std::string::npos) << pipeline.Failure()->message;
}

// Two passes over one framebuffer in a single frame, each clearing it to a colour of its own.
//
// This is the case a clear value stored on the *framebuffer* cannot express, and the reason
// RenderInfo resolves its values while the pass is recorded: OpenGL replays its records after the
// fact, so a value read at replay time would be the last one written — green for both halves —
// while Vulkan, which bakes it in at record time, would show red then green. The two backends have
// to agree, and they only do because neither reads the framebuffer once the record is made.
TEST_F(GpuTest, TwoPassesClearOneFramebufferToDifferentColors) {
    auto color = Image::Builder{}
                     .SetType(Image::Type::e2D)
                     .SetFormat(Image::Format::eRGBA8_UNORM)
                     .SetExtent(kor::UVec2{kW, kH})
                     .SetUsage(Image::Usage::eColorAttachment | Image::Usage::eTransferSrc)
                     .Build();
    auto colorView = ImageView::Builder(color).Build();
    // Built with blue, which neither pass below asks for: what lands in the image is whichever
    // override was recorded, never the framebuffer's own.
    auto framebuffer = Framebuffer::Builder{}
                           .AddColor({ .view = colorView, .clear = kor::Vec4{0.f, 0.f, 1.f, 1.f} })
                           .Build();

    Buffer::RawBuilder rb;
    rb.SetRawSize(static_cast<kor::i64>(kW) * kH * sizeof(Pixel))
      .SetUsage(Buffer::Usage::eTransferDst)
      .SetType(Buffer::Type::eReadback);
    auto firstPass = rb.Build();
    auto secondPass = rb.Build();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        // Pass one: red. Copied out before the second pass overwrites it, so both records are in
        // one command buffer — replaying them in order is exactly what the GL backend does.
        cb.BeginRendering(kor::RenderInfo(framebuffer)
                              .SetClearColor(0, kor::Vec4{1.f, 0.f, 0.f, 1.f}));
        cb.EndRendering();
        cb.CopyImageToBuffer(color, firstPass);

        // Pass two: green, same framebuffer.
        cb.BeginRendering(kor::RenderInfo(framebuffer)
                              .SetClearColor(0, kor::Vec4{0.f, 1.f, 0.f, 1.f}));
        cb.EndRendering();
        cb.CopyImageToBuffer(color, secondPass);
    }, CommandBuffer::Usage::eGraphics).Wait();

    const std::vector<Pixel> first = firstPass->Read<Pixel>();
    const std::vector<Pixel> second = secondPass->Read<Pixel>();
    ASSERT_EQ(first.size(), static_cast<std::size_t>(kW) * kH);
    ASSERT_EQ(second.size(), static_cast<std::size_t>(kW) * kH);
    EXPECT_EQ(first.front(), Pixel(255, 0, 0, 255)) << "the first pass cleared to its own red";
    EXPECT_EQ(second.front(), Pixel(0, 255, 0, 255)) << "the second pass cleared to its own green";
}

// A pass that overrides nothing gets the framebuffer's own clear values, so everything written
// before RenderInfo existed keeps working.
TEST_F(GpuTest, APassWithoutOverridesUsesTheFramebuffersClearValues) {
    auto color = Image::Builder{}
                     .SetType(Image::Type::e2D)
                     .SetFormat(Image::Format::eRGBA8_UNORM)
                     .SetExtent(kor::UVec2{kW, kH})
                     .SetUsage(Image::Usage::eColorAttachment | Image::Usage::eTransferSrc)
                     .Build();
    auto colorView = ImageView::Builder(color).Build();
    auto framebuffer = Framebuffer::Builder{}
                           .AddColor({ .view = colorView, .clear = kor::Vec4{0.f, 0.f, 1.f, 1.f} })
                           .Build();

    Buffer::RawBuilder rb;
    rb.SetRawSize(static_cast<kor::i64>(kW) * kH * sizeof(Pixel))
      .SetUsage(Buffer::Usage::eTransferDst)
      .SetType(Buffer::Type::eReadback);
    auto readback = rb.Build();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginRendering(framebuffer);   // a framebuffer converts
        cb.EndRendering();
        cb.CopyImageToBuffer(color, readback);
    }, CommandBuffer::Usage::eGraphics).Wait();

    const std::vector<Pixel> out = readback->Read<Pixel>();
    ASSERT_EQ(out.size(), static_cast<std::size_t>(kW) * kH);
    EXPECT_EQ(out.front(), Pixel(0, 0, 255, 255));
}

// An integer attachment takes an integer clear value, which is the case a float-only override
// would silently get wrong — the sentinel a visibility buffer is seeded with is never 0.0f.
TEST_F(GpuTest, AnIntegerAttachmentIsClearedWithAnIntegerOverride) {
    auto ids = Image::Builder{}
                   .SetType(Image::Type::e2D)
                   .SetFormat(Image::Format::eR32_UINT)
                   .SetExtent(kor::UVec2{kW, kH})
                   .SetUsage(Image::Usage::eColorAttachment | Image::Usage::eTransferSrc)
                   .Build();
    auto idsView = ImageView::Builder(ids).Build();
    auto framebuffer = Framebuffer::Builder{}
                           .AddColor({ .view = idsView, .clear = kor::UVec4{0u} })
                           .Build();

    Buffer::RawBuilder rb;
    rb.SetRawSize(static_cast<kor::i64>(kW) * kH * sizeof(kor::u32))
      .SetUsage(Buffer::Usage::eTransferDst)
      .SetType(Buffer::Type::eReadback);
    auto readback = rb.Build();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginRendering(kor::RenderInfo(framebuffer)
                              .SetClearColor(0, kor::UVec4{0xFFFFFFFFu}));
        cb.EndRendering();
        cb.CopyImageToBuffer(ids, readback);
    }, CommandBuffer::Usage::eGraphics).Wait();

    const std::vector<kor::u32> out = readback->Read<kor::u32>();
    ASSERT_EQ(out.size(), static_cast<std::size_t>(kW) * kH);
    EXPECT_EQ(out.front(), 0xFFFFFFFFu);
}

// A vertex type the engine knows nothing about, described to kor::Mesh::Builder by hand. The
// colour deliberately does not sit at offset 0: if the layout's offsets were ignored the shader
// would read the position as a colour, and the readback below would not be flat blue.
struct HandWrittenVertex {
    kor::Vec3 position;
    kor::Vec3 color;
};

// The base Mesh built straight from buffers and a VertexLayout — no mesh module, no vertex type
// reflected over — and drawn through a pipeline whose vertex inputs are matched to that layout by
// semantic. This is the whole point of the builder: geometry whose format is decided at runtime.
TEST_F(GpuTest, MeshBuilderDrawsHandWrittenVertexFormat) {
    const std::vector<HandWrittenVertex> vertices = {
        { kor::Vec3{-1.f, -1.f, 0.f}, kor::Vec3{0.f, 0.f, 1.f} },
        { kor::Vec3{ 3.f, -1.f, 0.f}, kor::Vec3{0.f, 0.f, 1.f} },
        { kor::Vec3{-1.f,  3.f, 0.f}, kor::Vec3{0.f, 0.f, 1.f} },
    };
    const std::vector<std::uint32_t> indices = {0, 1, 2};

    auto vertexBuffer = kor::Mesh::MakeBuffer(vertices, Buffer::Usage::eVertex);
    auto indexBuffer = kor::Mesh::MakeBuffer(indices, Buffer::Usage::eIndex);

    auto mesh = kor::Mesh::Builder()
        .SetVertexBuffer(0, vertexBuffer)
        .SetIndexBuffer(indexBuffer)
        .SetVertexLayout(kor::VertexLayout {
            .bindings = {
                kor::VertexInputBindingDescription(0, sizeof(HandWrittenVertex)),
            },
            .attributes = {
                kor::VertexLayout::Attribute("POSITION", "vertex", 0, offsetof(HandWrittenVertex, position), kor::ChannelType::eFloat, 3),
                kor::VertexLayout::Attribute("COLOR", "vertex", 0, offsetof(HandWrittenVertex, color), kor::ChannelType::eFloat, 3),
            },
        })
        .Build();
    ASSERT_TRUE(mesh.Valid()) << (mesh.Failure() ? mesh.Failure()->History() : "");

    // The counts are derived from the buffers and the layout's stride, not passed in.
    EXPECT_EQ(mesh->VertexCount(), vertices.size());
    ASSERT_TRUE(mesh->HasIndexBuffer());
    EXPECT_EQ(mesh->IndexCount().value(), indices.size());
    EXPECT_EQ(mesh->IndexType().value(), kor::ChannelType::eUInt);
    // The position a ray-tracing build would read comes from the layout, unasked.
    ASSERT_TRUE(mesh->PositionAttribute().has_value());
    EXPECT_EQ(mesh->PositionAttribute()->offset, offsetof(HandWrittenVertex, position));

    auto color = Image::Builder{}
                     .SetType(Image::Type::e2D)
                     .SetFormat(Image::Format::eRGBA8_UNORM)
                     .SetExtent(kor::UVec2{kW, kH})
                     .SetUsage(Image::Usage::eColorAttachment | Image::Usage::eTransferSrc)
                     .Build();
    auto colorView = ImageView::Builder(color).Build();
    auto framebuffer = Framebuffer::Builder{}
                           .AddColor({ .view = colorView, .clear = kor::Vec4{0.f, 0.f, 0.f, 1.f} })
                           .Build();

    const ResourceRef<const Shader> vert =
        Shader::Builder{}.SetPath("vertexColor.vert.glsl").GetOrBuild("meshBuilder.vertexColor.vert");
    const ResourceRef<const Shader> frag =
        Shader::Builder{}.SetPath("vertexColor.frag.glsl").GetOrBuild("meshBuilder.vertexColor.frag");

    auto pipeline = GraphicsPipeline::Builder{}
                        .SetVertexShader(vert, mesh->Layout())
                        .SetFragmentShader(frag)
                        .SetFramebuffer(framebuffer)
                        .Build();
    ASSERT_TRUE(pipeline.Valid()) << (pipeline.Failure() ? pipeline.Failure()->History() : "");

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginRendering(framebuffer);
        cb.BindGraphicsPipeline(pipeline);
        cb.SetViewport(0, 0, kW, kH);
        cb.SetScissor(0, 0, kW, kH);
        cb.BindMesh(mesh);
        cb.DrawIndexed();
        cb.EndRendering();
    }, CommandBuffer::Usage::eGraphics).Wait();

    Buffer::RawBuilder rb;
    rb.SetRawSize(static_cast<kor::i64>(kW) * kH * sizeof(Pixel))
      .SetUsage(Buffer::Usage::eTransferDst)
      .SetType(Buffer::Type::eReadback);
    auto readback = rb.Build();
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(color, readback);
    }, CommandBuffer::Usage::eTransfer).Wait();

    const std::vector<Pixel> out = readback->Read<Pixel>();
    ASSERT_EQ(out.size(), static_cast<std::size_t>(kW) * kH);
    // The triangle covers the whole target, and every vertex carries the same blue: the colour
    // attribute was read from its own offset, at the layout's stride.
    for (std::size_t i = 0; i < out.size(); ++i) {
        EXPECT_EQ(out[i], Pixel(0, 0, 255, 255)) << "texel " << i;
    }
}

// The same geometry described without a single semantic: the layout says which location each
// attribute is read at, and the shader annotates nothing. The attributes are listed colour-first,
// so a result that is still correct proves the locations decided it and not the order.
TEST_F(GpuTest, MeshBuilderDrawsALayoutDescribedByLocationAlone) {
    const std::vector<HandWrittenVertex> vertices = {
        { kor::Vec3{-1.f, -1.f, 0.f}, kor::Vec3{1.f, 0.f, 0.f} },
        { kor::Vec3{ 3.f, -1.f, 0.f}, kor::Vec3{1.f, 0.f, 0.f} },
        { kor::Vec3{-1.f,  3.f, 0.f}, kor::Vec3{1.f, 0.f, 0.f} },
    };
    const std::vector<std::uint32_t> indices = {0, 1, 2};

    auto mesh = kor::Mesh::Builder()
        .SetVertexBuffer(0, kor::Mesh::MakeBuffer(vertices, Buffer::Usage::eVertex))
        .SetIndexBuffer(kor::Mesh::MakeBuffer(indices, Buffer::Usage::eIndex))
        .SetVertexLayout(kor::VertexLayout {
            .bindings = {
                kor::VertexInputBindingDescription(0, sizeof(HandWrittenVertex)),
            },
            .attributes = {
                kor::VertexLayout::Attribute::AtLocation(1, 0, offsetof(HandWrittenVertex, color), kor::ChannelType::eFloat, 3),
                kor::VertexLayout::Attribute::AtLocation(0, 0, offsetof(HandWrittenVertex, position), kor::ChannelType::eFloat, 3),
            },
        })
        .Build();
    ASSERT_TRUE(mesh.Valid()) << (mesh.Failure() ? mesh.Failure()->History() : "");
    EXPECT_EQ(mesh->VertexCount(), vertices.size());

    auto color = Image::Builder{}
                     .SetType(Image::Type::e2D)
                     .SetFormat(Image::Format::eRGBA8_UNORM)
                     .SetExtent(kor::UVec2{kW, kH})
                     .SetUsage(Image::Usage::eColorAttachment | Image::Usage::eTransferSrc)
                     .Build();
    auto colorView = ImageView::Builder(color).Build();
    auto framebuffer = Framebuffer::Builder{}
                           .AddColor({ .view = colorView, .clear = kor::Vec4{0.f, 0.f, 0.f, 1.f} })
                           .Build();

    const ResourceRef<const Shader> vert =
        Shader::Builder{}.SetPath("vertexColorLocations.vert.glsl").GetOrBuild("meshBuilder.locations.vert");
    const ResourceRef<const Shader> frag =
        Shader::Builder{}.SetPath("vertexColor.frag.glsl").GetOrBuild("meshBuilder.locations.frag");

    auto pipeline = GraphicsPipeline::Builder{}
                        .SetVertexShader(vert, mesh->Layout())
                        .SetFragmentShader(frag)
                        .SetFramebuffer(framebuffer)
                        .Build();
    ASSERT_TRUE(pipeline.Valid()) << (pipeline.Failure() ? pipeline.Failure()->History() : "");

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginRendering(framebuffer);
        cb.BindGraphicsPipeline(pipeline);
        cb.SetViewport(0, 0, kW, kH);
        cb.SetScissor(0, 0, kW, kH);
        cb.BindMesh(mesh);
        cb.DrawIndexed();
        cb.EndRendering();
    }, CommandBuffer::Usage::eGraphics).Wait();

    Buffer::RawBuilder rb;
    rb.SetRawSize(static_cast<kor::i64>(kW) * kH * sizeof(Pixel))
      .SetUsage(Buffer::Usage::eTransferDst)
      .SetType(Buffer::Type::eReadback);
    auto readback = rb.Build();
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(color, readback);
    }, CommandBuffer::Usage::eTransfer).Wait();

    const std::vector<Pixel> out = readback->Read<Pixel>();
    ASSERT_EQ(out.size(), static_cast<std::size_t>(kW) * kH);
    for (std::size_t i = 0; i < out.size(); ++i) {
        EXPECT_EQ(out[i], Pixel(255, 0, 0, 255)) << "texel " << i;
    }
}

// A buffer handed over as an rvalue belongs to the mesh, which is what geometry nothing else
// refers to wants: the mesh keeps it alive on its own.
TEST_F(GpuTest, MeshBuilderAdoptsBuffersGivenAsRvalues) {
    const std::vector<kor::Vec3> positions = {
        kor::Vec3{-1.f, -1.f, 0.f}, kor::Vec3{3.f, -1.f, 0.f}, kor::Vec3{-1.f, 3.f, 0.f},
    };

    kor::Resource<kor::Mesh> mesh;
    {
        auto vertexBuffer = kor::Mesh::MakeBuffer(positions, Buffer::Usage::eVertex);
        mesh = kor::Mesh::Builder()
            .SetVertexBuffer(0, std::move(vertexBuffer))
            .SetVertexLayout(kor::VertexLayout {
                .bindings = { kor::VertexInputBindingDescription(0, sizeof(kor::Vec3)) },
                .attributes = {
                    kor::VertexLayout::Attribute("POSITION", "vertex", 0, 0, kor::ChannelType::eFloat, 3),
                },
            })
            .Build();
    }   // the local Resource is gone; only the mesh's own hold on the buffer is left

    ASSERT_TRUE(mesh.Valid()) << (mesh.Failure() ? mesh.Failure()->History() : "");
    EXPECT_EQ(mesh->VertexCount(), positions.size());
    ASSERT_FALSE(mesh->VertexBuffers().empty());
    EXPECT_TRUE(mesh->VertexBuffers().front().Valid()) << "the adopted buffer outlives its handle";
    EXPECT_FALSE(mesh->HasIndexBuffer());
}

// Geometry that does not match its description is a poisoned mesh naming what is wrong, not a
// draw that reads the wrong bytes.
TEST_F(GpuTest, MeshBuilderPoisonsMisdescribedGeometry) {
    const std::vector<kor::Vec3> positions = {
        kor::Vec3{-1.f, -1.f, 0.f}, kor::Vec3{3.f, -1.f, 0.f}, kor::Vec3{-1.f, 3.f, 0.f},
    };
    const std::vector<kor::Vec2> uvs = { kor::Vec2{0.f}, kor::Vec2{1.f} };   // one vertex short

    auto positionBuffer = kor::Mesh::MakeBuffer(positions, Buffer::Usage::eVertex);
    auto uvBuffer = kor::Mesh::MakeBuffer(uvs, Buffer::Usage::eVertex);

    const kor::VertexLayout twoBindings {
        .bindings = {
            kor::VertexInputBindingDescription(0, sizeof(kor::Vec3)),
            kor::VertexInputBindingDescription(1, sizeof(kor::Vec2)),
        },
        .attributes = {
            kor::VertexLayout::Attribute("POSITION", "vertex", 0, 0, kor::ChannelType::eFloat, 3),
            kor::VertexLayout::Attribute("UV", "vertex", 1, 0, kor::ChannelType::eFloat, 2),
        },
    };

    // A binding the layout declares and nothing was set for.
    const auto missing = kor::Mesh::Builder()
        .SetVertexBuffer(0, positionBuffer)
        .SetVertexLayout(twoBindings)
        .Build();
    EXPECT_TRUE(missing.Poisoned()) << "binding 1 has no vertex buffer";

    // Buffers that imply different vertex counts.
    const auto disagreeing = kor::Mesh::Builder()
        .SetVertexBuffer(0, positionBuffer)
        .SetVertexBuffer(1, uvBuffer)
        .SetVertexLayout(twoBindings)
        .Build();
    EXPECT_TRUE(disagreeing.Poisoned()) << "3 positions against 2 UVs";

    // A buffer with nothing describing it.
    const auto undescribed = kor::Mesh::Builder()
        .SetVertexBuffer(0, positionBuffer)
        .Build();
    EXPECT_TRUE(undescribed.Poisoned()) << "no vertex layout was set";

    // A buffer that was never made a vertex buffer.
    auto plain = Buffer::Builder<kor::Vec3>()
        .SetDataView(positions)
        .SetUsage(Buffer::Usage::eStorage)
        .SetType(Buffer::Type::eDynamic)    // host-visible, so the data needs no staging copy
        .Build();
    const auto wrongUsage = kor::Mesh::Builder()
        .SetVertexBuffer(0, plain)
        .SetVertexLayout(kor::VertexLayout {
            .bindings = { kor::VertexInputBindingDescription(0, sizeof(kor::Vec3)) },
            .attributes = {
                kor::VertexLayout::Attribute("POSITION", "vertex", 0, 0, kor::ChannelType::eFloat, 3),
            },
        })
        .Build();
    EXPECT_TRUE(wrongUsage.Poisoned()) << "the buffer was not created with Usage::eVertex";
}





// Push-constant blocks are std430, and a shader that asks for anything else does not load.
//
// This is what makes a hand-matched C++ struct a reasonable thing to write: under std140 an array
// of floats strides sixteen bytes instead of four, so a struct that mirrors the block would write
// one value in four and look almost right. The layout is checked against spirv-cross's own std430
// rules, so the answer is the same one the compiler used.
TEST_F(GpuTest, APushConstantBlockThatIsNotStd430DoesNotLoad) {
    const auto shader = Shader::Builder{}
                            .SetLang<Shader::Lang::eGLSL>()
                            .SetStage(Shader::Stage::eFragment)
                            .SetPath(kor::ShaderPath("pushStd140.frag.glsl"))
                            .Build();

    ASSERT_TRUE(shader.Poisoned()) << "an explicitly std140 push-constant block was accepted";
    EXPECT_EQ(shader.Failure()->code, kor::ErrorCode::ePushConstantMismatch);
    // Named down to the member, since that is what has to move.
    EXPECT_NE(shader.Failure()->message.find("weights"), std::string::npos) << shader.Failure()->message;
    EXPECT_NE(shader.Failure()->message.find("std430"), std::string::npos) << shader.Failure()->message;
}


// An Image binds straight to a texture binding: the view is built from what the *shader* declared
// the binding as, and owned by the image, so nothing here constructs an ImageView at all.
TEST_F(GpuTest, AnImageBindsWithoutAViewBeingBuilt) {
    constexpr kor::u32 kSize = 8;

    auto texture = Image::Builder{}
        .SetFormat(Image::Format::eRGBA8_UNORM)
        .SetExtent(kor::UVec2{kSize, kSize})
        .SetUsage(Image::Usage::eSampled | Image::Usage::eTransferDst)
        .Build();
    ASSERT_TRUE(texture.Valid());

    auto sampler = Sampler::Builder{}.Build();
    ASSERT_TRUE(sampler.Valid());

    const auto vert = Shader::Builder{}.SetLang<Shader::Lang::eGLSL>().SetStage(Shader::Stage::eVertex)
        .SetPath(kor::ShaderPath("sampleTexture.vert.glsl")).GetOrBuild("test.imgbind.vert");
    const auto frag = Shader::Builder{}.SetLang<Shader::Lang::eGLSL>().SetStage(Shader::Stage::eFragment)
        .SetPath(kor::ShaderPath("sampleTexture.frag.glsl")).GetOrBuild("test.imgbind.frag");

    auto target = Image::Builder{}
        .SetFormat(Image::Format::eRGBA8_UNORM)
        .SetExtent(kor::UVec2{kSize, kSize})
        .SetUsage(Image::Usage::eColorAttachment)
        .Build();
    ASSERT_TRUE(target.Valid());

    // The framebuffer takes the Image too, and names the target so it can be found again.
    auto framebuffer = Framebuffer::Builder{}
        .AddColor({ .name = "color", .view = target })
        .Build();
    ASSERT_TRUE(framebuffer.Valid()) << (framebuffer.Failure() ? framebuffer.Failure()->History() : "");

    auto pipeline = GraphicsPipeline::Builder{}
        .SetVertexShader(vert).SetFragmentShader(frag)
        .SetFramebuffer(framebuffer)
        .Build();
    ASSERT_TRUE(pipeline.Valid());

    // `uniform sampler2D tex` — the binding says 2D, so that is the view the image is asked for.
    auto set = DescriptorSet::Builder(pipeline, 0).Write("tex", texture, sampler).Build();
    ASSERT_TRUE(set.Valid()) << (set.Failure() ? set.Failure()->History() : "");

    // The view is owned by the image and shared, so a second bind of the same texture is the same
    // view object rather than another one.
    EXPECT_EQ(texture->View(kor::ImageShape::e2D).Get(), texture->View(kor::ImageShape::e2D).Get());

    // And the framebuffer hands its target back by name.
    EXPECT_EQ(framebuffer->ImageNamed("color").Get(), target.Get());
    EXPECT_FALSE(framebuffer->ImageNamed("nosuchtarget").Valid());
}

// A resize replaces an image's storage, and the views it handed out follow it: they rebuild against
// the new storage the next time they are used. So a framebuffer made from the image — whose views
// are those — still renders after a Framebuffer::Resize. Dropping them instead once left every such
// framebuffer holding a dead view after its first resize.
TEST_F(GpuTest, ResizingAnImageKeepsTheViewsItHandedOut) {
    auto image = Image::Builder{}
        .SetFormat(Image::Format::eRGBA8_UNORM)
        .SetExtent(kor::UVec2{8, 8})
        .SetUsage(Image::Usage::eSampled | Image::Usage::eColorAttachment | Image::Usage::eTransferSrc)
        .Build();
    ASSERT_TRUE(image.Valid());
    const auto before = image->View(kor::ImageShape::e2D);
    ASSERT_TRUE(before.Valid());
    auto framebuffer = Framebuffer::Builder{}.AddColor({ .view = image, .clear = kor::Vec4(0.f, 0.f, 1.f, 1.f) }).Build();
    ASSERT_TRUE(framebuffer.Valid());

    const auto generationBefore = image->Generation();
    framebuffer->Resize({16, 16});
    ASSERT_NE(image->Generation(), generationBefore) << "the resize did not replace the image";
    EXPECT_TRUE(before.Valid()) << "the view outlives the storage it was made for";
    EXPECT_EQ(image->View(kor::ImageShape::e2D).Get(), before.Get());

    Buffer::RawBuilder rb;
    rb.SetRawSize(16 * 16 * 4).SetUsage(Buffer::Usage::eTransferDst).SetType(Buffer::Type::eReadback);
    auto readback = rb.Build();
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginRendering(framebuffer).EndRendering();
        cb.CopyImageToBuffer(image, readback);
    }, CommandBuffer::Usage::eGraphics).Wait();
    const auto texels = readback->Read<kor::u8>(16 * 16 * 4);
    EXPECT_EQ(texels[4 * (16 * 16 - 1) + 2], 255) << "the resized framebuffer rendered into all of the new storage";
}

// Binding an image to a binding its usage does not allow says which flag is missing, rather than
// leaving the driver to complain about usage bits.
TEST_F(GpuTest, AnImageMissingItsUsageIsReportedWithTheFlagToAdd) {
    auto texture = Image::Builder{}
        .SetFormat(Image::Format::eRGBA8_UNORM)
        .SetExtent(kor::UVec2{8, 8})
        .SetUsage(Image::Usage::eColorAttachment)   // deliberately not eSampled
        .Build();
    ASSERT_TRUE(texture.Valid());

    auto sampler = Sampler::Builder{}.Build();
    const auto vert = Shader::Builder{}.SetLang<Shader::Lang::eGLSL>().SetStage(Shader::Stage::eVertex)
        .SetPath(kor::ShaderPath("sampleTexture.vert.glsl")).GetOrBuild("test.usage.vert");
    const auto frag = Shader::Builder{}.SetLang<Shader::Lang::eGLSL>().SetStage(Shader::Stage::eFragment)
        .SetPath(kor::ShaderPath("sampleTexture.frag.glsl")).GetOrBuild("test.usage.frag");

    auto target = Image::Builder{}.SetFormat(Image::Format::eRGBA8_UNORM)
        .SetExtent(kor::UVec2{8, 8}).SetUsage(Image::Usage::eColorAttachment).Build();
    auto framebuffer = Framebuffer::Builder{}.AddColor({ .view = target }).Build();
    auto pipeline = GraphicsPipeline::Builder{}
        .SetVertexShader(vert).SetFragmentShader(frag)
        .SetFramebuffer(framebuffer).Build();
    ASSERT_TRUE(pipeline.Valid());

    auto set = DescriptorSet::Builder(pipeline, 0).Write("tex", texture, sampler).Build();
    ASSERT_FALSE(set.Valid()) << "an image with no eSampled usage was bound to a texture binding";
    ASSERT_NE(set.Failure(), nullptr);
    const auto message = set.Failure()->History();
    EXPECT_NE(message.find("eSampled"), std::string::npos) << message;
}

// The transfer usages are on by default, so the commands that need them work without anyone having
// to have thought about it. This is the case that used to fail as a driver validation message about
// usage bits, long after the line that actually caused it.
TEST_F(GpuTest, TransferUsageIsNotSomethingYouHaveToRemember) {
    // No setUsage at all, and every one of these needs a transfer role: the upload needs
    // eTransferDst on the image, the mip chain needs both, and the readback needs eTransferSrc.
    auto texture = Image::Builder{}
        .SetFormat(Image::Format::eRGBA8_UNORM)
        .SetExtent(kor::UVec2{8, 8})
        .SetData(std::vector<kor::U8Vec4>(8 * 8, kor::U8Vec4{40, 80, 120, 255}))
        .Build();
    ASSERT_TRUE(texture.Valid()) << (texture.Failure() ? texture.Failure()->History() : "");

    Buffer::RawBuilder rb;
    rb.SetRawSize(8 * 8 * 4).SetType(Buffer::Type::eReadback);
    auto readback = rb.Build();
    ASSERT_TRUE(readback.Valid()) << (readback.Failure() ? readback.Failure()->History() : "");

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(texture, readback);
        EXPECT_TRUE(cb.Ok()) << "a plain image and a plain buffer could not be copied between: "
                             << (cb.Ok() ? "" : cb.Outcome().error().ToString());
    }, CommandBuffer::Usage::eTransfer).Wait();

    const auto pixels = readback->Read<kor::U8Vec4>();
    ASSERT_EQ(pixels.size(), static_cast<std::size_t>(8 * 8));
    EXPECT_EQ(pixels[0], (kor::U8Vec4{40, 80, 120, 255}));
}

// Opting out is still possible, and getting it wrong afterwards is now Koral's error rather than
// the driver's: it names the flag, the role and the command, at the line that recorded it.
TEST_F(GpuTest, AMissingTransferUsageIsNamedAtTheCommandThatNeededIt) {
    auto image = Image::Builder{}
        .SetFormat(Image::Format::eRGBA8_UNORM)
        .SetExtent(kor::UVec2{8, 8})
        .SetUsage(Image::Usage::eSampled)   // exactly this: no transfer roles
        .Build();
    ASSERT_TRUE(image.Valid());

    Buffer::RawBuilder rb;
    rb.SetRawSize(8 * 8 * 4).SetType(Buffer::Type::eReadback);
    auto readback = rb.Build();
    ASSERT_TRUE(readback.Valid());

    std::string message;
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(image, readback);
        EXPECT_FALSE(cb.Ok()) << "an image with no eTransferSrc was copied from anyway";
        if (!cb.Ok()) message = cb.Outcome().error().ToString();
    }, CommandBuffer::Usage::eTransfer).Wait();

    EXPECT_NE(message.find("eTransferSrc"), std::string::npos) << message;
    EXPECT_NE(message.find("CopyImageToBuffer"), std::string::npos) << message;
    // And the fix, spelled the way it would be typed.
    EXPECT_NE(message.find("setUsage"), std::string::npos) << message;
}

// eUniform is the one buffer role that cannot simply be on by default: it carries a 64 KiB ceiling
// checked at build time, so defaulting it would make every larger buffer fail to build over a role
// it never asked for. It is deduced from the size instead, which is the one piece of information
// the builder does have.
TEST_F(GpuTest, UniformUsageIsDeducedFromTheBuffersSize) {
    // Small enough to be a uniform block, so it is given the role without anyone saying so — this
    // is the camera-and-constants case, the one people had to remember eUniform for.
    Buffer::RawBuilder small;
    small.SetRawSize(1024);
    auto smallBuffer = small.Build();
    ASSERT_TRUE(smallBuffer.Valid()) << (smallBuffer.Failure() ? smallBuffer.Failure()->History() : "");
    EXPECT_TRUE(smallBuffer->UsageFlags() & Buffer::Usage::eUniform);

    // Too large to ever be one, so the role is not added — and, crucially, the buffer still builds.
    // Blanket-defaulting eUniform is exactly what this would have broken.
    Buffer::RawBuilder large;
    large.SetRawSize(16 * 1024 * 1024);
    auto largeBuffer = large.Build();
    ASSERT_TRUE(largeBuffer.Valid()) << (largeBuffer.Failure() ? largeBuffer.Failure()->History() : "");
    EXPECT_FALSE(largeBuffer->UsageFlags() & Buffer::Usage::eUniform);
    EXPECT_TRUE(largeBuffer->UsageFlags() & Buffer::Usage::eStorage)
        << "the free roles should still be on";

    // Asking for it outright on a buffer that cannot hold it is still an error: the caller said
    // something impossible, and is told so rather than quietly given a buffer that is not what it
    // asked for.
    Buffer::RawBuilder impossible;
    impossible.SetRawSize(16 * 1024 * 1024).SetUsage(Buffer::Usage::eUniform);
    auto impossibleBuffer = impossible.Build();
    EXPECT_FALSE(impossibleBuffer.Valid()) << "an oversized uniform buffer was accepted";

    // And setUsage still means exactly what it says — no deduction on top of an explicit set.
    Buffer::RawBuilder exact;
    exact.SetRawSize(1024).SetUsage(Buffer::Usage::eStorage);
    auto exactBuffer = exact.Build();
    ASSERT_TRUE(exactBuffer.Valid());
    EXPECT_FALSE(exactBuffer->UsageFlags() & Buffer::Usage::eUniform)
        << "setUsage named an exact set and something was added to it anyway";
}

} // namespace

// A mat4 vertex input, fed an instance at a time: one quad drawn twice from one mesh, each copy squeezed into
// half the target by a transform of its own and coloured by a colour of its own — both per-instance attributes,
// bound beside the mesh with BindVertexBuffer. The matrix takes four locations, the colour the one after them.
TEST_F(GpuTest, AMatrixVertexInputIsFedAnInstanceAtATime) {
    auto colorImage = Image::Builder{}.SetType(Image::Type::e2D).SetFormat(Image::Format::eRGBA8_UNORM)
                          .SetExtent(kor::UVec2{kW, kH}).SetUsage(Image::Usage::eColorAttachment | Image::Usage::eTransferSrc).Build();
    auto colorView = ImageView::Builder(colorImage).Build();
    auto framebuffer = Framebuffer::Builder{}.AddColor({ .view = colorView, .clear = kor::Vec4{0.f, 0.f, 0.f, 1.f} }).Build();

    // The quad, a vertex at a time; the transforms and colours, an instance at a time.
    struct Instance { kor::Mat4 model; kor::Vec4 color; };
    const std::vector<kor::Vec2> quad { {-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1} };
    const auto half = [](const float x) { return kor::Mat4 { {0.5f, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {x, 0, 0, 1} }; };
    const std::vector<Instance> instances { { half(-0.5f), {1, 0, 0, 1} }, { half(0.5f), {0, 0, 1, 1} } };
    auto vertices = Buffer::Builder<kor::Vec2>{}.SetData(quad).SetUsage(Buffer::Usage::eVertex | Buffer::Usage::eTransferDst).SetType(Buffer::Type::eDeviceLocal).Build();
    auto perInstance = Buffer::Builder<Instance>{}.SetData(instances).SetUsage(Buffer::Usage::eVertex | Buffer::Usage::eTransferDst).SetType(Buffer::Type::eDeviceLocal).Build();

    kor::VertexLayout meshLayout;
    meshLayout.bindings.push_back({ .binding = 0, .stride = sizeof(kor::Vec2) });
    meshLayout.attributes.push_back(kor::VertexLayout::Attribute::AtLocation(0, 0, 0, kor::ChannelType::eFloat, 2));
    auto mesh = kor::Mesh::Builder{}.SetVertexLayout(meshLayout).SetVertexBuffer(0, std::move(vertices)).Build();

    kor::VertexLayout layout = meshLayout;
    layout.bindings.push_back({ .binding = 1, .stride = sizeof(Instance), .inputRate = kor::VertexInputRate::eInstance });
    auto model = kor::VertexLayout::Attribute::Matrix("", 1, offsetof(Instance, model));
    model.location = 1;
    layout.attributes.push_back(model);
    layout.attributes.push_back(kor::VertexLayout::Attribute::AtLocation(5, 1, offsetof(Instance, color), kor::ChannelType::eFloat, 4));

    const ResourceRef<const Shader> vert = Shader::Builder{}.SetLang<Shader::Lang::eGLSL>().SetStage(Shader::Stage::eVertex)
        .SetPath(kor::ShaderPath("instancedMatrix.vert.glsl")).GetOrBuild("test.instancedMatrix.vert");
    const ResourceRef<const Shader> frag = Shader::Builder{}.SetLang<Shader::Lang::eGLSL>().SetStage(Shader::Stage::eFragment)
        .SetPath(kor::ShaderPath("instancedMatrix.frag.glsl")).GetOrBuild("test.instancedMatrix.frag");
    ASSERT_TRUE(vert.Valid() && frag.Valid());
    ASSERT_EQ(vert->BlockLayout().inputs.size(), 3u);
    for (const auto& input : vert->BlockLayout().inputs)
        if (input.name == "model") { EXPECT_EQ(input.locationSpan, 4u) << "a column a location"; EXPECT_EQ(input.channelCount, 4u); }

    auto pipeline = GraphicsPipeline::Builder{}.SetVertexShader(vert, layout).SetFragmentShader(frag).SetFramebuffer(framebuffer).Build();
    ASSERT_TRUE(pipeline.Valid()) << (pipeline.Failure() ? pipeline.Failure()->message : "");

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginRendering(framebuffer);
        cb.BindGraphicsPipeline(pipeline);
        cb.BindMesh(mesh).BindVertexBuffer(1, perInstance);
        cb.Draw(6, 2);
        cb.EndRendering();
        EXPECT_TRUE(cb.Ok());
    }, CommandBuffer::Usage::eGraphics).Wait();

    auto readback = Buffer::RawBuilder{}.SetRawSize(static_cast<kor::i64>(kW) * kH * sizeof(Pixel))
                        .SetUsage(Buffer::Usage::eTransferDst).SetType(Buffer::Type::eReadback).Build();
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) { cb.CopyImageToBuffer(colorImage, readback); }, CommandBuffer::Usage::eTransfer).Wait();
    const std::vector<Pixel> out = readback->Read<Pixel>();
    const auto at = [&](const std::uint32_t x, const std::uint32_t y) { return out[y * kW + x]; };
    EXPECT_EQ(at(2, 8), Pixel(255, 0, 0, 255)) << "the first instance: the left half, red";
    EXPECT_EQ(at(13, 8), Pixel(0, 0, 255, 255)) << "the second: the right half, blue";
}
