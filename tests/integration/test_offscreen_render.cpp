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

#include <glm/glm.hpp>

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

using Pixel = glm::u8vec4; // RGBA8

constexpr std::uint32_t kW = 16;
constexpr std::uint32_t kH = 16;

// Render a full-screen green triangle into an offscreen RGBA8 image and verify
// the rasterizer filled every texel. This is the first test to drive a graphics
// pipeline end-to-end without a swap chain.
TEST_F(GpuTest, OffscreenTriangleFillsTarget) {
    // --- offscreen color target ------------------------------------------
    Image::Builder ib;
    ib.setType(Image::Type::e2D)
      .setFormat(Image::Format::eRGBA8_UNORM)
      .setExtent(glm::uvec2{kW, kH})
      .setUsage(Image::Usage::eColorAttachment | Image::Usage::eTransferSrc);    // read back afterwards
    auto colorImage = ib.build();

    auto colorView = ImageView::Builder(colorImage)
                         .build();

    auto framebuffer =
        Framebuffer::Builder{}
            .addColor({ .view = colorView, .clear = glm::vec4{0.f, 0.f, 0.f, 1.f} })
            .build();

    // --- shaders + graphics pipeline -------------------------------------
    const ResourceRef<const Shader> vert =
        Shader::Builder{}
            .setLang<Shader::Lang::eGLSL>()
            .setStage(Shader::Stage::eVertex)
            .setPath(kor::shaderPath("flatTriangle.vert.glsl"))
            .getOrBuild("test.flatTriangle.vert");

    const ResourceRef<const Shader> frag =
        Shader::Builder{}
            .setLang<Shader::Lang::eGLSL>()
            .setStage(Shader::Stage::eFragment)
            .setPath(kor::shaderPath("flatTriangle.frag.glsl"))
            .getOrBuild("test.flatTriangle.frag");

    auto pipeline =
        GraphicsPipeline::Builder{}
            .setVertexShader(vert)
            .setFragmentShader(frag)
            .setFramebuffer(framebuffer)
            .build();

    // --- record the draw --------------------------------------------------
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        // BeginRendering clears any bound pipeline, so bind inside the render scope.
        cb.BeginRendering(framebuffer);
        cb.BindGraphicsPipeline(pipeline);
        cb.SetViewport(0, 0, kW, kH);
        cb.SetScissor(0, 0, kW, kH);
        cb.Draw(3); // full-screen triangle, no vertex/descriptor inputs
        cb.EndRendering();
    }, CommandBuffer::Usage::eGraphics);

    // --- read the target back and verify ---------------------------------
    Buffer::RawBuilder rb;
    rb.setRawSize(static_cast<glm::i64>(kW) * kH * sizeof(Pixel))
      .setUsage(Buffer::Usage::eTransferDst)
      .setType(Buffer::Type::eReadback);
    auto readback = rb.build();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(colorImage, readback);
    }, CommandBuffer::Usage::eTransfer);

    const std::vector<Pixel> out = readback->Read<Pixel>();
    ASSERT_EQ(out.size(), static_cast<std::size_t>(kW) * kH);
    for (std::size_t i = 0; i < out.size(); ++i) {
        EXPECT_EQ(out[i].r, 0)   << "texel " << i << " r";
        EXPECT_EQ(out[i].g, 255) << "texel " << i << " g";
        EXPECT_EQ(out[i].b, 0)   << "texel " << i << " b";
        EXPECT_EQ(out[i].a, 255) << "texel " << i << " a";
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
    ib.setType(Image::Type::e2D)
      .setFormat(Image::Format::eRGBA8_UNORM)
      .setExtent(glm::uvec2{kW, kH})
      .setUsage(Image::Usage::eColorAttachment | Image::Usage::eTransferSrc);
    auto colorImage = ib.build();

    auto colorView = ImageView::Builder(colorImage).build();
    auto framebuffer =
        Framebuffer::Builder{}
            .addColor({ .view = colorView, .clear = glm::vec4{0.f, 0.f, 0.f, 1.f} })
            .build();

    const ResourceRef<const Shader> vert =
        Shader::Builder{}.setLang<Shader::Lang::eGLSL>().setStage(Shader::Stage::eVertex)
            .setPath(kor::shaderPath("flatTriangle.vert.glsl")).getOrBuild("test.flatTriangle.vert");
    const ResourceRef<const Shader> frag =
        Shader::Builder{}.setLang<Shader::Lang::eGLSL>().setStage(Shader::Stage::eFragment)
            .setPath(kor::shaderPath("flatTriangle.frag.glsl")).getOrBuild("test.flatTriangle.frag");

    auto pipeline =
        GraphicsPipeline::Builder{}
            .setVertexShader(vert)
            .setFragmentShader(frag)
            .setFramebuffer(framebuffer)
            .build();

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
    }, CommandBuffer::Usage::eGraphics);

    Buffer::RawBuilder rb;
    rb.setRawSize(static_cast<glm::i64>(kW) * kH * sizeof(Pixel))
      .setUsage(Buffer::Usage::eTransferDst)
      .setType(Buffer::Type::eReadback);
    auto readback = rb.build();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(colorImage, readback);
    }, CommandBuffer::Usage::eTransfer);

    const std::vector<Pixel> out = readback->Read<Pixel>();
    ASSERT_EQ(out.size(), static_cast<std::size_t>(kW) * kH);
    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            const bool inScissor = x >= kSx && x < kSx + kSw && y >= kSy && y < kSy + kSh;
            const Pixel& got = out[y * kW + x];
            if (inScissor) {
                EXPECT_EQ(got.g, 255) << "scissored texel (" << x << "," << y << ") should be green";
                EXPECT_EQ(got.r, 0)   << "scissored texel (" << x << "," << y << ") r";
            } else {
                EXPECT_EQ(got.g, 0) << "clipped texel (" << x << "," << y << ") should stay black";
                EXPECT_EQ(got.r, 0) << "clipped texel (" << x << "," << y << ") r";
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
                     .setType(Image::Type::e2D)
                     .setFormat(Image::Format::eRGBA8_UNORM)
                     .setExtent(glm::uvec2{kW, kH})
                     .setUsage(Image::Usage::eColorAttachment | Image::Usage::eTransferSrc)
                     .build();
    auto depth = Image::Builder{}
                     .setType(Image::Type::e2D)
                     .setFormat(Image::Format::eD32_SFLOAT)
                     .setExtent(glm::uvec2{kW, kH})
                     .setUsage(Image::Usage::eDepthStencilAttachment)
                     .build();

    auto colorView = ImageView::Builder(color).build();
    auto depthView = ImageView::Builder(depth).build();

    auto framebuffer = Framebuffer::Builder{}
                           .addColor({ .view = colorView, .clear = glm::vec4{0.f, 0.f, 0.f, 1.f} })
                           .setDepth({ .view = depthView, .depth = 1.f })
                           .build();

    const ResourceRef<const Shader> vert =
        Shader::Builder{}.setLang<Shader::Lang::eGLSL>().setStage(Shader::Stage::eVertex)
            .setPath(kor::shaderPath("flatTriangle.vert.glsl")).getOrBuild("test.flatTriangle.vert");
    const ResourceRef<const Shader> frag =
        Shader::Builder{}.setLang<Shader::Lang::eGLSL>().setStage(Shader::Stage::eFragment)
            .setPath(kor::shaderPath("flatTriangle.frag.glsl")).getOrBuild("test.flatTriangle.frag");

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
                        .setVertexShader(vert)
                        .setFragmentShader(frag)
                        .setFramebuffer(framebuffer)
                        .setColorBlendState(blend)
                        .setDepthStencilState(depthState)
                        .build();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginRendering(framebuffer);
        cb.BindGraphicsPipeline(pipeline);
        cb.SetViewport(0, 0, kW, kH);
        cb.SetScissor(0, 0, kW, kH);
        cb.Draw(3);
        cb.EndRendering();
    }, CommandBuffer::Usage::eGraphics);

    Buffer::RawBuilder rb;
    rb.setRawSize(static_cast<glm::i64>(kW) * kH * sizeof(Pixel))
      .setUsage(Buffer::Usage::eTransferDst)
      .setType(Buffer::Type::eReadback);
    auto readback = rb.build();
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(color, readback);
    }, CommandBuffer::Usage::eTransfer);

    const std::vector<Pixel> out = readback->Read<Pixel>();
    ASSERT_EQ(out.size(), static_cast<std::size_t>(kW) * kH);
    // Opaque green (alpha 1) over black with src-alpha blending stays green.
    for (std::size_t i = 0; i < out.size(); ++i) {
        EXPECT_EQ(out[i].g, 255) << "texel " << i << " g";
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
        PosVertex{ glm::vec3{-1.0f, -1.0f, 0.0f} },
        PosVertex{ glm::vec3{ 3.0f, -1.0f, 0.0f} },
        PosVertex{ glm::vec3{-1.0f,  3.0f, 0.0f} },
    };
    std::vector<std::uint32_t> indices = {0, 1, 2};
    auto mesh = PosMesh::Create(verts, indices);
    ASSERT_TRUE(static_cast<bool>(mesh));

    auto color = Image::Builder{}
                     .setType(Image::Type::e2D)
                     .setFormat(Image::Format::eRGBA8_UNORM)
                     .setExtent(glm::uvec2{kW, kH})
                     .setUsage(Image::Usage::eColorAttachment | Image::Usage::eTransferSrc)
                     .build();
    auto colorView = ImageView::Builder(color).build();
    auto framebuffer = Framebuffer::Builder{}
                           .addColor({ .view = colorView, .clear = glm::vec4{0.f, 0.f, 0.f, 1.f} })
                           .build();

    const ResourceRef<const Shader> vert =
        Shader::Builder{}.setLang<Shader::Lang::eGLSL>().setStage(Shader::Stage::eVertex)
            .setPath(kor::shaderPath("meshTriangle.vert.glsl")).getOrBuild("test.meshTriangle.vert");
    const ResourceRef<const Shader> frag =
        Shader::Builder{}.setLang<Shader::Lang::eGLSL>().setStage(Shader::Stage::eFragment)
            .setPath(kor::shaderPath("flatTriangle.frag.glsl")).getOrBuild("test.flatTriangle.frag");

    auto pipeline = GraphicsPipeline::Builder{}
                        .setVertexShader(vert, PosMesh::Layout()) // vertex-input state from the mesh layout
                        .setFragmentShader(frag)
                        .setFramebuffer(framebuffer)
                        .build();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginRendering(framebuffer);
        cb.BindGraphicsPipeline(pipeline);
        cb.SetViewport(0, 0, kW, kH);
        cb.SetScissor(0, 0, kW, kH);
        cb.BindMesh(mesh);
        cb.DrawIndexed(); // uses the bound mesh's index count
        cb.EndRendering();
    }, CommandBuffer::Usage::eGraphics);

    Buffer::RawBuilder rb;
    rb.setRawSize(static_cast<glm::i64>(kW) * kH * sizeof(Pixel))
      .setUsage(Buffer::Usage::eTransferDst)
      .setType(Buffer::Type::eReadback);
    auto readback = rb.build();
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(color, readback);
    }, CommandBuffer::Usage::eTransfer);

    const std::vector<Pixel> out = readback->Read<Pixel>();
    ASSERT_EQ(out.size(), static_cast<std::size_t>(kW) * kH);
    int green = 0;
    for (const auto& p : out) if (p.g == 255) ++green;
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
        PosVertex{ glm::vec3{-1.0f, -1.0f, 0.0f} },
        PosVertex{ glm::vec3{ 1.0f, -1.0f, 0.0f} },
        PosVertex{ glm::vec3{ 1.0f,  0.0f, 0.0f} },
        PosVertex{ glm::vec3{-1.0f,  0.0f, 0.0f} },
    };
    std::vector<std::uint32_t> indices = {0, 1, 2, 0, 2, 3};
    auto mesh = PosMesh::Create(verts, indices);
    ASSERT_TRUE(static_cast<bool>(mesh));

    auto color = Image::Builder{}
                     .setType(Image::Type::e2D)
                     .setFormat(Image::Format::eRGBA8_UNORM)
                     .setExtent(glm::uvec2{kW, kH})
                     .setUsage(Image::Usage::eColorAttachment | Image::Usage::eTransferSrc)
                     .build();
    auto colorView = ImageView::Builder(color).build();
    auto framebuffer = Framebuffer::Builder{}
                           .addColor({ .view = colorView, .clear = glm::vec4{0.f, 0.f, 0.f, 1.f} })
                           .build();

    const ResourceRef<const Shader> vert =
        Shader::Builder{}.setPath("meshTriangle.vert.glsl").getOrBuild("orient.meshTriangle.vert");
    const ResourceRef<const Shader> frag =
        Shader::Builder{}.setPath("flatTriangle.frag.glsl").getOrBuild("orient.flatTriangle.frag");

    auto pipeline = GraphicsPipeline::Builder{}
                        .setVertexShader(vert, PosMesh::Layout())
                        .setFragmentShader(frag)
                        .setFramebuffer(framebuffer)
                        .build();   // culling is off by default, so winding cannot mask the result
    ASSERT_TRUE(pipeline.valid()) << pipeline.error()->history();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginRendering(framebuffer);
        cb.BindGraphicsPipeline(pipeline);
        cb.SetViewport(0, 0, kW, kH);
        cb.SetScissor(0, 0, kW, kH);
        cb.BindMesh(mesh);
        cb.DrawIndexed();
        cb.EndRendering();
    }, CommandBuffer::Usage::eGraphics);

    Buffer::RawBuilder rb;
    rb.setRawSize(static_cast<glm::i64>(kW) * kH * sizeof(Pixel))
      .setUsage(Buffer::Usage::eTransferDst)
      .setType(Buffer::Type::eReadback);
    auto readback = rb.build();
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(color, readback);
    }, CommandBuffer::Usage::eTransfer);

    const std::vector<Pixel> out = readback->Read<Pixel>();
    ASSERT_EQ(out.size(), static_cast<std::size_t>(kW) * kH);

    // Sanity: the draw must have covered something, or the assertions below pass vacuously.
    int green = 0;
    for (const auto& p : out) if (p.g == 255) ++green;
    ASSERT_GT(green, 0) << "the quad covered nothing; the orientation check would be vacuous";

    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            const Pixel& got = out[y * kW + x];
            if (y < kH / 2) {
                EXPECT_EQ(got.g, 255)
                    << "row " << y << " is in the top half of clip space and must be green; "
                       "green in the bottom rows instead means Y is inverted";
            } else {
                EXPECT_EQ(got.g, 0) << "row " << y << " is below the quad and must stay black";
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
    constexpr glm::u32 kSize = 16;

    Image::Builder ib;
    ib.setFormat(Image::Format::eRGBA8_UNORM)
      .setExtent(glm::uvec2{kSize, kSize})
      .setUsage(Image::Usage::eColorAttachment | Image::Usage::eSampled);
    auto colorImage = ib.build();
    ASSERT_TRUE(colorImage.valid());

    auto colorView = ImageView::Builder(colorImage).build();
    auto framebuffer = Framebuffer::Builder{}
                           .addColor({ .view = colorView, .clear = glm::vec4{0.f, 0.f, 0.f, 1.f} })
                           .build();
    auto sampler = Sampler::Builder{}.build();
    ASSERT_TRUE(sampler.valid());

    const ResourceRef<const Shader> vert =
        Shader::Builder{}.setLang<Shader::Lang::eGLSL>().setStage(Shader::Stage::eVertex)
            .setPath(kor::shaderPath("sampleTexture.vert.glsl")).getOrBuild("test.feedback.vert");
    const ResourceRef<const Shader> frag =
        Shader::Builder{}.setLang<Shader::Lang::eGLSL>().setStage(Shader::Stage::eFragment)
            .setPath(kor::shaderPath("sampleTexture.frag.glsl")).getOrBuild("test.feedback.frag");

    auto pipeline = GraphicsPipeline::Builder{}
                        .setVertexShader(vert)
                        .setFragmentShader(frag)
                        .setFramebuffer(framebuffer)
                        .build();
    ASSERT_TRUE(pipeline.valid());

    // The attachment, bound as a texture to the pipeline drawing into it.
    auto descriptorSet = DescriptorSet::Builder(pipeline, 0)
                             .write(0, colorView, sampler)
                             .build();
    ASSERT_TRUE(descriptorSet.valid());

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
    for (const auto& error : cb->errors()) {
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
        .setType(Image::Type::e2D)
        .setFormat(Image::Format::eRGBA8_UNORM)
        .setExtent(glm::uvec2{kSize, kSize})
        .setUsage(Image::Usage::eColorAttachment | Image::Usage::eTransferSrc)
        .build();
    auto colorView = ImageView::Builder(color).build();
    auto framebuffer = Framebuffer::Builder{}
        .addColor({ .view = colorView, .clear = glm::vec4{0.f, 0.f, 0.f, 1.f} })
        .build();

    const ResourceRef<const Shader> vert = Shader::Builder{}
        .setLang<Shader::Lang::eGLSL>().setStage(Shader::Stage::eVertex)
        .setPath(kor::shaderPath("flatTriangle.vert.glsl")).getOrBuild("test.flatTriangle.vert");
    const ResourceRef<const Shader> frag = Shader::Builder{}
        .setLang<Shader::Lang::eGLSL>().setStage(Shader::Stage::eFragment)
        .setPath(kor::shaderPath("flatTriangle.frag.glsl")).getOrBuild("test.flatTriangle.frag");

    auto pipeline = GraphicsPipeline::Builder{}
        .setVertexShader(vert)
        .setFragmentShader(frag)
        .setFramebuffer(framebuffer)
        .build();
    ASSERT_TRUE(static_cast<bool>(pipeline)) << (pipeline.error() ? pipeline.error()->message : "");

    Buffer::RawBuilder rb;
    rb.setRawSize(static_cast<glm::i64>(kSize) * kSize * 4)
      .setUsage(Buffer::Usage::eTransferDst)
      .setType(Buffer::Type::eReadback);
    auto readback = rb.build();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginRendering(framebuffer)
          .BindGraphicsPipeline(pipeline)
          // No SetViewport, no SetScissor: that is the case under test.
          .Draw(3)
          .EndRendering();
        cb.CopyImageToBuffer(color, readback);
    }, CommandBuffer::Usage::eGraphics);

    const auto texels = readback->Read<glm::u8vec4>();
    ASSERT_EQ(texels.size(), static_cast<std::size_t>(kSize) * kSize);

    // The shader paints the whole clip volume, so every texel of *this* target must be painted. With a
    // window-sized viewport only the top-left corner of the triangle would land here, leaving the far
    // side of the image at the clear colour.
    std::size_t painted = 0;
    for (const auto& texel : texels) if (texel != glm::u8vec4(0, 0, 0, 255)) ++painted;
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
        .setType(Image::Type::e2D)
        .setFormat(Image::Format::eRGBA8_UNORM)
        .setExtent(glm::uvec2{kFirst, kFirst})
        .setUsage(Image::Usage::eColorAttachment | Image::Usage::eTransferSrc)
        .build();
    auto depth = Image::Builder{}
        .setType(Image::Type::e2D)
        .setFormat(Image::Format::eD32_SFLOAT)
        .setExtent(glm::uvec2{kFirst, kFirst})
        .setUsage(Image::Usage::eDepthStencilAttachment)
        .build();

    auto colorView = ImageView::Builder(color).build();
    auto depthView = ImageView::Builder(depth).build();

    auto framebuffer = Framebuffer::Builder{}
        .addColor({ .view = colorView, .clear = glm::vec4{0.f, 0.f, 0.f, 1.f} })
        .setDepth({ .view = depthView })
        .build();
    ASSERT_TRUE(static_cast<bool>(framebuffer));
    const auto generationBefore = color->generation();

    framebuffer->Resize(glm::uvec2{kSecond, kSecond});

    EXPECT_EQ(framebuffer->extent(), glm::uvec2(kSecond, kSecond));
    EXPECT_EQ(color->extent(), glm::uvec3(kSecond, kSecond, 1)) << "the colour attachment followed";
    EXPECT_EQ(depth->extent(), glm::uvec3(kSecond, kSecond, 1)) << "and so did the depth one";
    // The image was *replaced*, which is what tells a view holding the old one to rebuild.
    EXPECT_GT(color->generation(), generationBefore);

    // And it is usable at the new size: rendering into it and reading it back is the whole point of
    // having resized it. A view that had not noticed the replacement would fault here.
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginRendering(framebuffer);
        cb.EndRendering();
    }, CommandBuffer::Usage::eGraphics);

    Buffer::RawBuilder rb;
    rb.setRawSize(static_cast<glm::i64>(kSecond) * kSecond * 4)
      .setUsage(Buffer::Usage::eTransferDst)
      .setType(Buffer::Type::eReadback);
    auto readback = rb.build();
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(color, readback);
    }, CommandBuffer::Usage::eTransfer);

    const auto texels = readback->Read<glm::u8vec4>();
    ASSERT_EQ(texels.size(), static_cast<std::size_t>(kSecond) * kSecond);
    // Cleared to the framebuffer's own colour, at the new size.
    EXPECT_EQ(texels.front(), glm::u8vec4(0, 0, 0, 255));
}

// Resizing to the size it already is changes nothing, so a scene may call it every frame.
TEST_F(GpuTest, FramebufferResizeToTheSameSizeIsANoOp) {
    auto color = Image::Builder{}
        .setType(Image::Type::e2D)
        .setFormat(Image::Format::eRGBA8_UNORM)
        .setExtent(glm::uvec2{32, 32})
        .setUsage(Image::Usage::eColorAttachment)
        .build();
    auto colorView = ImageView::Builder(color).build();
    auto framebuffer = Framebuffer::Builder{}
        .addColor({ .view = colorView, .clear = glm::vec4{0.f} })
        .build();

    const auto generation = color->generation();
    framebuffer->Resize(glm::uvec2{32, 32});
    framebuffer->Resize(glm::uvec2{0, 16});     // a zero extent names nothing and is ignored
    EXPECT_EQ(color->generation(), generation) << "the image was not replaced";
    EXPECT_EQ(color->extent(), glm::uvec3(32, 32, 1));
}

// Push constants addressed by name, declared once in a shared header and read by two stages.
//
// The vertex stage reads `offset` and the fragment stage reads `color`, out of one block — so the
// pipeline's merge has to union the two stages' declarations rather than treat them as rivals, and
// each constant has to be written at the offset the compiler chose without the CPU ever naming a
// byte. The shifted triangle proves the vertex half landed and the colour proves the fragment half.
TEST_F(GpuTest, PushConstantsByNameAcrossStages) {
    auto color = Image::Builder{}
                     .setType(Image::Type::e2D)
                     .setFormat(Image::Format::eRGBA8_UNORM)
                     .setExtent(glm::uvec2{kW, kH})
                     .setUsage(Image::Usage::eColorAttachment | Image::Usage::eTransferSrc)
                     .build();
    auto colorView = ImageView::Builder(color).build();
    auto framebuffer = Framebuffer::Builder{}
                           .addColor({ .view = colorView, .clear = glm::vec4{0.f, 0.f, 0.f, 1.f} })
                           .build();

    const ResourceRef<const Shader> vert =
        Shader::Builder{}.setPath("pushMultiStage.vert.glsl").getOrBuild("push.multi.vert");
    const ResourceRef<const Shader> frag =
        Shader::Builder{}.setPath("pushMultiStage.frag.glsl").getOrBuild("push.multi.frag");

    auto pipeline = GraphicsPipeline::Builder{}
                        .setVertexShader(vert)
                        .setFragmentShader(frag)
                        .setFramebuffer(framebuffer)
                        .build();
    ASSERT_TRUE(pipeline.valid()) << (pipeline.error() ? pipeline.error()->history() : "");

    // Both constants are on the pipeline, each with the stage that reads it.
    const auto* offsetConstant = pipeline->findPushConstant("offset");
    const auto* colorConstant = pipeline->findPushConstant("color");
    ASSERT_NE(offsetConstant, nullptr);
    ASSERT_NE(colorConstant, nullptr);
    EXPECT_EQ(offsetConstant->size, sizeof(glm::vec2));
    EXPECT_EQ(colorConstant->size, sizeof(glm::vec4));
    EXPECT_NE(colorConstant->offset, offsetConstant->offset) << "two constants cannot share bytes";
    EXPECT_TRUE(offsetConstant->stages & Shader::Stage::eVertex);
    EXPECT_TRUE(colorConstant->stages & Shader::Stage::eFragment);

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginRendering(framebuffer);
        cb.BindGraphicsPipeline(pipeline);
        cb.SetViewport(0, 0, kW, kH);
        cb.SetScissor(0, 0, kW, kH);
        // No offsets, no struct mirroring the block: the names are the whole contract.
        cb.PushConstant("offset", glm::vec2{2.f, 0.f});   // shifts the triangle off to the right
        cb.PushConstant("color", glm::vec4{0.f, 0.f, 1.f, 1.f});
        cb.Draw(3);
        cb.EndRendering();
    }, CommandBuffer::Usage::eGraphics);

    Buffer::RawBuilder rb;
    rb.setRawSize(static_cast<glm::i64>(kW) * kH * sizeof(Pixel))
      .setUsage(Buffer::Usage::eTransferDst)
      .setType(Buffer::Type::eReadback);
    auto shifted = rb.build();
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(color, shifted);
    }, CommandBuffer::Usage::eTransfer);

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
        cb.PushConstant("offset", glm::vec2{0.f, 0.f});
        cb.PushConstant("color", glm::vec4{0.f, 0.f, 1.f, 1.f});
        cb.Draw(3);
        cb.EndRendering();
    }, CommandBuffer::Usage::eGraphics);

    auto covered = rb.build();
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(color, covered);
    }, CommandBuffer::Usage::eTransfer);

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
                     .setType(Image::Type::e2D)
                     .setFormat(Image::Format::eRGBA8_UNORM)
                     .setExtent(glm::uvec2{kW, kH})
                     .setUsage(Image::Usage::eColorAttachment)
                     .build();
    auto colorView = ImageView::Builder(color).build();
    auto framebuffer = Framebuffer::Builder{}
                           .addColor({ .view = colorView, .clear = glm::vec4{0.f} })
                           .build();

    const ResourceRef<const Shader> vert =
        Shader::Builder{}.setPath("pushMultiStage.vert.glsl").getOrBuild("push.multi.vert");
    const ResourceRef<const Shader> frag =
        Shader::Builder{}.setPath("pushMultiStage.frag.glsl").getOrBuild("push.multi.frag");
    auto pipeline = GraphicsPipeline::Builder{}
                        .setVertexShader(vert)
                        .setFragmentShader(frag)
                        .setFramebuffer(framebuffer)
                        .build();
    ASSERT_TRUE(pipeline.valid());

    EXPECT_EQ(pipeline->findPushConstant("tint"), nullptr);

    const auto unknown = CommandBuffer::Create(CommandBuffer::Usage::eGraphics);
    unknown->Begin();
    unknown->BeginRendering(framebuffer);
    unknown->BindGraphicsPipeline(pipeline);
    unknown->PushConstant("tint", glm::vec4{1.f});
    unknown->EndRendering();
    unknown->End();

    ASSERT_FALSE(unknown->errors().empty());
    EXPECT_EQ(unknown->errors().front().code, kor::ErrorCode::ePushConstantMismatch);
    // The message has to name what there *is*, since the usual cause is a rename on one side.
    EXPECT_NE(unknown->errors().front().message.find("color"), std::string::npos)
        << unknown->errors().front().message;

    const auto wrongSize = CommandBuffer::Create(CommandBuffer::Usage::eGraphics);
    wrongSize->Begin();
    wrongSize->BeginRendering(framebuffer);
    wrongSize->BindGraphicsPipeline(pipeline);
    wrongSize->PushConstant("color", glm::vec2{1.f});   // the shader declares a vec4
    wrongSize->EndRendering();
    wrongSize->End();

    ASSERT_FALSE(wrongSize->errors().empty());
    EXPECT_EQ(wrongSize->errors().front().code, kor::ErrorCode::ePushConstantMismatch);

    // And with nothing bound to look the name up on.
    const auto unbound = CommandBuffer::Create(CommandBuffer::Usage::eGraphics);
    unbound->Begin();
    unbound->PushConstant("color", glm::vec4{1.f});
    unbound->End();
    ASSERT_FALSE(unbound->errors().empty());
    EXPECT_EQ(unbound->errors().front().code, kor::ErrorCode::eNoPipelineBound);
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
                     .setType(Image::Type::e2D)
                     .setFormat(Image::Format::eRGBA8_UNORM)
                     .setExtent(glm::uvec2{kW, kH})
                     .setUsage(Image::Usage::eColorAttachment | Image::Usage::eTransferSrc)
                     .build();
    auto colorView = ImageView::Builder(color).build();
    auto framebuffer = Framebuffer::Builder{}
                           .addColor({ .view = colorView, .clear = glm::vec4{0.f, 0.f, 0.f, 1.f} })
                           .build();

    const ResourceRef<const Shader> vert =
        Shader::Builder{}.setPath("flatTriangle.vert.glsl").getOrBuild("nested.flat.vert");
    const ResourceRef<const Shader> frag =
        Shader::Builder{}.setPath("pushNested.frag.glsl").getOrBuild("nested.push.frag");

    auto pipeline = GraphicsPipeline::Builder{}
                        .setVertexShader(vert)
                        .setFragmentShader(frag)
                        .setFramebuffer(framebuffer)
                        .build();
    ASSERT_TRUE(pipeline.valid()) << (pipeline.error() ? pipeline.error()->history() : "");

    // Three constants, one per top-level member, whatever each one is made of.
    const auto* model = pipeline->findPushConstant("model");
    const auto* material = pipeline->findPushConstant("material");
    const auto* weights = pipeline->findPushConstant("weights");
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
    const auto* albedo = pipeline->findPushConstant("material.albedo");
    const auto* roughness = pipeline->findPushConstant("material.roughness");
    ASSERT_NE(albedo, nullptr);
    ASSERT_NE(roughness, nullptr);
    EXPECT_EQ(albedo->offset, 64u);
    EXPECT_EQ(albedo->size, 16u);
    EXPECT_EQ(roughness->offset, 80u);
    EXPECT_EQ(roughness->size, 4u);
    EXPECT_EQ(pipeline->findPushConstant("albedo"), nullptr);
    EXPECT_EQ(pipeline->findPushConstant("roughness"), nullptr);

    // Array elements likewise.
    const auto* thirdWeight = pipeline->findPushConstant("weights[2]");
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
    }, CommandBuffer::Usage::eGraphics);

    Buffer::RawBuilder rb;
    rb.setRawSize(static_cast<glm::i64>(kW) * kH * sizeof(Pixel))
      .setUsage(Buffer::Usage::eTransferDst)
      .setType(Buffer::Type::eReadback);
    auto readback = rb.build();
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(color, readback);
    }, CommandBuffer::Usage::eTransfer);

    // Blue from the struct's albedo, opaque from the array's third element: both landed where the
    // shader reads them.
    EXPECT_EQ(readback->Read<Pixel>().front(), Pixel(0, 0, 255, 255));

    // The obvious C++ mirror of that struct is also exactly 20 bytes — glm's vectors carry no
    // extra alignment by default — so a nested struct is pushed as itself, not as a byte blob.
    struct CppMaterial { glm::vec4 albedo; float roughness; };
    static_assert(sizeof(CppMaterial) == 20, "glm gained alignment; the mirror no longer matches");
    EXPECT_EQ(sizeof(CppMaterial), material->size);

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginRendering(framebuffer);
        cb.BindGraphicsPipeline(pipeline);
        cb.SetViewport(0, 0, kW, kH);
        cb.SetScissor(0, 0, kW, kH);
        cb.PushConstant("material", CppMaterial{ glm::vec4{0.f, 1.f, 0.f, 1.f}, 1.f });
        cb.PushConstant("weights", weightsValue);
        cb.Draw(3);
        cb.EndRendering();
    }, CommandBuffer::Usage::eGraphics);

    auto asStruct = rb.build();
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(color, asStruct);
    }, CommandBuffer::Usage::eTransfer);

    // Green this time, and still opaque: the struct went in whole and left `weights` alone.
    EXPECT_EQ(asStruct->Read<Pixel>().front(), Pixel(0, 255, 0, 255));

    // The same thing written field by field, which needs no agreement between the layouts at all.
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginRendering(framebuffer);
        cb.BindGraphicsPipeline(pipeline);
        cb.SetViewport(0, 0, kW, kH);
        cb.SetScissor(0, 0, kW, kH);
        cb.PushConstant("material.albedo", glm::vec4{1.f, 0.f, 0.f, 1.f});
        cb.PushConstant("material.roughness", 1.f);
        cb.PushConstant("weights[2]", 1.f);
        cb.Draw(3);
        cb.EndRendering();
    }, CommandBuffer::Usage::eGraphics);

    auto byField = rb.build();
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(color, byField);
    }, CommandBuffer::Usage::eTransfer);
    EXPECT_EQ(byField->Read<Pixel>().front(), Pixel(255, 0, 0, 255));
}

// A mat3 and an array of vec3 pushed from their obvious C++ spellings, which are tightly packed
// and 12 bytes shorter apiece than what the shader reserves. Nothing in the test says so: the
// strides come from reflection and the value is reassembled into them.
TEST_F(GpuTest, PushConstantsAreLaidOutIntoTheShadersPadding) {
    auto color = Image::Builder{}
                     .setType(Image::Type::e2D)
                     .setFormat(Image::Format::eRGBA8_UNORM)
                     .setExtent(glm::uvec2{kW, kH})
                     .setUsage(Image::Usage::eColorAttachment | Image::Usage::eTransferSrc)
                     .build();
    auto colorView = ImageView::Builder(color).build();
    auto framebuffer = Framebuffer::Builder{}
                           .addColor({ .view = colorView, .clear = glm::vec4{0.f, 0.f, 0.f, 1.f} })
                           .build();

    const ResourceRef<const Shader> vert =
        Shader::Builder{}.setPath("flatTriangle.vert.glsl").getOrBuild("aligned.flat.vert");
    const ResourceRef<const Shader> frag =
        Shader::Builder{}.setPath("pushAligned.frag.glsl").getOrBuild("aligned.push.frag");

    auto pipeline = GraphicsPipeline::Builder{}
                        .setVertexShader(vert)
                        .setFragmentShader(frag)
                        .setFramebuffer(framebuffer)
                        .build();
    ASSERT_TRUE(pipeline.valid()) << (pipeline.error() ? pipeline.error()->history() : "");

    // What the shader reserves, against what C++ would have handed over.
    const auto* basis = pipeline->findPushConstant("basis");
    const auto* tints = pipeline->findPushConstant("tints");
    ASSERT_NE(basis, nullptr);
    ASSERT_NE(tints, nullptr);
    EXPECT_EQ(basis->size, 48u) << "three columns of four floats";
    EXPECT_EQ(basis->matrixStride, 16u);
    EXPECT_EQ(sizeof(glm::mat3), 36u) << "which is not what glm hands over";
    EXPECT_EQ(tints->arrayStride, 16u);
    EXPECT_EQ(sizeof(std::array<glm::vec3, 3>), 36u);

    // Columns and elements chosen so every one of them contributes a distinct, exactly
    // representable amount: a value that landed in the wrong column cannot produce this colour.
    glm::mat3 basisValue(0.f);
    basisValue[0] = glm::vec3{0.2f, 0.f, 0.f};
    basisValue[1] = glm::vec3{0.f, 0.4f, 0.f};
    basisValue[2] = glm::vec3{0.f, 0.f, 0.6f};
    const std::array tintsValue{ glm::vec3{0.2f, 0.f, 0.f}, glm::vec3{0.f, 0.2f, 0.f}, glm::vec3{0.f, 0.f, 0.2f} };

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginRendering(framebuffer);
        cb.BindGraphicsPipeline(pipeline);
        cb.SetViewport(0, 0, kW, kH);
        cb.SetScissor(0, 0, kW, kH);
        cb.PushConstant("basis", basisValue);
        cb.PushConstant("tints", tintsValue);
        cb.Draw(3);
        cb.EndRendering();
    }, CommandBuffer::Usage::eGraphics);

    Buffer::RawBuilder rb;
    rb.setRawSize(static_cast<glm::i64>(kW) * kH * sizeof(Pixel))
      .setUsage(Buffer::Usage::eTransferDst)
      .setType(Buffer::Type::eReadback);
    auto readback = rb.build();
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(color, readback);
    }, CommandBuffer::Usage::eTransfer);

    // (0.2 + 0.2, 0.4 + 0.2, 0.6 + 0.2) — every column and every element accounted for.
    EXPECT_EQ(readback->Read<Pixel>().front(), Pixel(102, 153, 204, 255));

    // A single element of the array addressed on its own, at the shader's stride.
    const auto* second = pipeline->findPushConstant("tints[1]");
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(second->offset, tints->offset + 16u);
    EXPECT_EQ(second->size, 16u);
}

// Two stages that place one push constant differently poison the pipeline, naming the constant.
// Left to the driver this is silent: both stages read the same bytes, and one of them reads them
// as something they are not.
TEST_F(GpuTest, PipelinePoisonsWhenTwoStagesDeclareAPushConstantDifferently) {
    auto color = Image::Builder{}
                     .setType(Image::Type::e2D)
                     .setFormat(Image::Format::eRGBA8_UNORM)
                     .setExtent(glm::uvec2{kW, kH})
                     .setUsage(Image::Usage::eColorAttachment)
                     .build();
    auto colorView = ImageView::Builder(color).build();
    auto framebuffer = Framebuffer::Builder{}
                           .addColor({ .view = colorView, .clear = glm::vec4{0.f} })
                           .build();

    const ResourceRef<const Shader> vert =
        Shader::Builder{}.setPath("pushConflict.vert.glsl").getOrBuild("push.conflict.vert");
    const ResourceRef<const Shader> frag =
        Shader::Builder{}.setPath("pushConflict.frag.glsl").getOrBuild("push.conflict.frag");
    ASSERT_TRUE(vert.valid());
    ASSERT_TRUE(frag.valid());

    const auto pipeline = GraphicsPipeline::Builder{}
                              .setVertexShader(vert)
                              .setFragmentShader(frag)
                              .setFramebuffer(framebuffer)
                              .build();

    ASSERT_TRUE(pipeline.poisoned()) << "the stages disagree about 'tint' and nothing said so";
    EXPECT_EQ(pipeline.error()->code, kor::ErrorCode::ePushConstantMismatch);
    EXPECT_NE(pipeline.error()->message.find("tint"), std::string::npos) << pipeline.error()->message;
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
                     .setType(Image::Type::e2D)
                     .setFormat(Image::Format::eRGBA8_UNORM)
                     .setExtent(glm::uvec2{kW, kH})
                     .setUsage(Image::Usage::eColorAttachment | Image::Usage::eTransferSrc)
                     .build();
    auto colorView = ImageView::Builder(color).build();
    // Built with blue, which neither pass below asks for: what lands in the image is whichever
    // override was recorded, never the framebuffer's own.
    auto framebuffer = Framebuffer::Builder{}
                           .addColor({ .view = colorView, .clear = glm::vec4{0.f, 0.f, 1.f, 1.f} })
                           .build();

    Buffer::RawBuilder rb;
    rb.setRawSize(static_cast<glm::i64>(kW) * kH * sizeof(Pixel))
      .setUsage(Buffer::Usage::eTransferDst)
      .setType(Buffer::Type::eReadback);
    auto firstPass = rb.build();
    auto secondPass = rb.build();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        // Pass one: red. Copied out before the second pass overwrites it, so both records are in
        // one command buffer — replaying them in order is exactly what the GL backend does.
        cb.BeginRendering(kor::RenderInfo(framebuffer)
                              .setClearColor(0, glm::vec4{1.f, 0.f, 0.f, 1.f}));
        cb.EndRendering();
        cb.CopyImageToBuffer(color, firstPass);

        // Pass two: green, same framebuffer.
        cb.BeginRendering(kor::RenderInfo(framebuffer)
                              .setClearColor(0, glm::vec4{0.f, 1.f, 0.f, 1.f}));
        cb.EndRendering();
        cb.CopyImageToBuffer(color, secondPass);
    }, CommandBuffer::Usage::eGraphics);

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
                     .setType(Image::Type::e2D)
                     .setFormat(Image::Format::eRGBA8_UNORM)
                     .setExtent(glm::uvec2{kW, kH})
                     .setUsage(Image::Usage::eColorAttachment | Image::Usage::eTransferSrc)
                     .build();
    auto colorView = ImageView::Builder(color).build();
    auto framebuffer = Framebuffer::Builder{}
                           .addColor({ .view = colorView, .clear = glm::vec4{0.f, 0.f, 1.f, 1.f} })
                           .build();

    Buffer::RawBuilder rb;
    rb.setRawSize(static_cast<glm::i64>(kW) * kH * sizeof(Pixel))
      .setUsage(Buffer::Usage::eTransferDst)
      .setType(Buffer::Type::eReadback);
    auto readback = rb.build();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginRendering(framebuffer);   // a framebuffer converts
        cb.EndRendering();
        cb.CopyImageToBuffer(color, readback);
    }, CommandBuffer::Usage::eGraphics);

    const std::vector<Pixel> out = readback->Read<Pixel>();
    ASSERT_EQ(out.size(), static_cast<std::size_t>(kW) * kH);
    EXPECT_EQ(out.front(), Pixel(0, 0, 255, 255));
}

// An integer attachment takes an integer clear value, which is the case a float-only override
// would silently get wrong — the sentinel a visibility buffer is seeded with is never 0.0f.
TEST_F(GpuTest, AnIntegerAttachmentIsClearedWithAnIntegerOverride) {
    auto ids = Image::Builder{}
                   .setType(Image::Type::e2D)
                   .setFormat(Image::Format::eR32_UINT)
                   .setExtent(glm::uvec2{kW, kH})
                   .setUsage(Image::Usage::eColorAttachment | Image::Usage::eTransferSrc)
                   .build();
    auto idsView = ImageView::Builder(ids).build();
    auto framebuffer = Framebuffer::Builder{}
                           .addColor({ .view = idsView, .clear = glm::uvec4{0u} })
                           .build();

    Buffer::RawBuilder rb;
    rb.setRawSize(static_cast<glm::i64>(kW) * kH * sizeof(glm::u32))
      .setUsage(Buffer::Usage::eTransferDst)
      .setType(Buffer::Type::eReadback);
    auto readback = rb.build();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginRendering(kor::RenderInfo(framebuffer)
                              .setClearColor(0, glm::uvec4{0xFFFFFFFFu}));
        cb.EndRendering();
        cb.CopyImageToBuffer(ids, readback);
    }, CommandBuffer::Usage::eGraphics);

    const std::vector<glm::u32> out = readback->Read<glm::u32>();
    ASSERT_EQ(out.size(), static_cast<std::size_t>(kW) * kH);
    EXPECT_EQ(out.front(), 0xFFFFFFFFu);
}

// A vertex type the engine knows nothing about, described to kor::Mesh::Builder by hand. The
// colour deliberately does not sit at offset 0: if the layout's offsets were ignored the shader
// would read the position as a colour, and the readback below would not be flat blue.
struct HandWrittenVertex {
    glm::vec3 position;
    glm::vec3 color;
};

// The base Mesh built straight from buffers and a VertexLayout — no mesh module, no vertex type
// reflected over — and drawn through a pipeline whose vertex inputs are matched to that layout by
// semantic. This is the whole point of the builder: geometry whose format is decided at runtime.
TEST_F(GpuTest, MeshBuilderDrawsHandWrittenVertexFormat) {
    const std::vector<HandWrittenVertex> vertices = {
        { glm::vec3{-1.f, -1.f, 0.f}, glm::vec3{0.f, 0.f, 1.f} },
        { glm::vec3{ 3.f, -1.f, 0.f}, glm::vec3{0.f, 0.f, 1.f} },
        { glm::vec3{-1.f,  3.f, 0.f}, glm::vec3{0.f, 0.f, 1.f} },
    };
    const std::vector<std::uint32_t> indices = {0, 1, 2};

    auto vertexBuffer = kor::Mesh::makeBuffer(vertices, Buffer::Usage::eVertex);
    auto indexBuffer = kor::Mesh::makeBuffer(indices, Buffer::Usage::eIndex);

    auto mesh = kor::Mesh::Builder()
        .setVertexBuffer(0, vertexBuffer)
        .setIndexBuffer(indexBuffer)
        .setVertexLayout(kor::VertexLayout {
            .bindings = {
                kor::VertexInputBindingDescription(0, sizeof(HandWrittenVertex)),
            },
            .attributes = {
                kor::VertexLayout::Attribute("POSITION", "vertex", 0, offsetof(HandWrittenVertex, position), kor::ChannelType::eFloat, 3),
                kor::VertexLayout::Attribute("COLOR", "vertex", 0, offsetof(HandWrittenVertex, color), kor::ChannelType::eFloat, 3),
            },
        })
        .build();
    ASSERT_TRUE(mesh.valid()) << (mesh.error() ? mesh.error()->history() : "");

    // The counts are derived from the buffers and the layout's stride, not passed in.
    EXPECT_EQ(mesh->vertexCount(), vertices.size());
    ASSERT_TRUE(mesh->hasIndexBuffer());
    EXPECT_EQ(mesh->indexCount().value(), indices.size());
    EXPECT_EQ(mesh->indexType().value(), kor::ChannelType::eUInt);
    // The position a ray-tracing build would read comes from the layout, unasked.
    ASSERT_TRUE(mesh->positionAttribute().has_value());
    EXPECT_EQ(mesh->positionAttribute()->offset, offsetof(HandWrittenVertex, position));

    auto color = Image::Builder{}
                     .setType(Image::Type::e2D)
                     .setFormat(Image::Format::eRGBA8_UNORM)
                     .setExtent(glm::uvec2{kW, kH})
                     .setUsage(Image::Usage::eColorAttachment | Image::Usage::eTransferSrc)
                     .build();
    auto colorView = ImageView::Builder(color).build();
    auto framebuffer = Framebuffer::Builder{}
                           .addColor({ .view = colorView, .clear = glm::vec4{0.f, 0.f, 0.f, 1.f} })
                           .build();

    const ResourceRef<const Shader> vert =
        Shader::Builder{}.setPath("vertexColor.vert.glsl").getOrBuild("meshBuilder.vertexColor.vert");
    const ResourceRef<const Shader> frag =
        Shader::Builder{}.setPath("vertexColor.frag.glsl").getOrBuild("meshBuilder.vertexColor.frag");

    auto pipeline = GraphicsPipeline::Builder{}
                        .setVertexShader(vert, mesh->vertexLayout())
                        .setFragmentShader(frag)
                        .setFramebuffer(framebuffer)
                        .build();
    ASSERT_TRUE(pipeline.valid()) << (pipeline.error() ? pipeline.error()->history() : "");

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginRendering(framebuffer);
        cb.BindGraphicsPipeline(pipeline);
        cb.SetViewport(0, 0, kW, kH);
        cb.SetScissor(0, 0, kW, kH);
        cb.BindMesh(mesh);
        cb.DrawIndexed();
        cb.EndRendering();
    }, CommandBuffer::Usage::eGraphics);

    Buffer::RawBuilder rb;
    rb.setRawSize(static_cast<glm::i64>(kW) * kH * sizeof(Pixel))
      .setUsage(Buffer::Usage::eTransferDst)
      .setType(Buffer::Type::eReadback);
    auto readback = rb.build();
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(color, readback);
    }, CommandBuffer::Usage::eTransfer);

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
        { glm::vec3{-1.f, -1.f, 0.f}, glm::vec3{1.f, 0.f, 0.f} },
        { glm::vec3{ 3.f, -1.f, 0.f}, glm::vec3{1.f, 0.f, 0.f} },
        { glm::vec3{-1.f,  3.f, 0.f}, glm::vec3{1.f, 0.f, 0.f} },
    };
    const std::vector<std::uint32_t> indices = {0, 1, 2};

    auto mesh = kor::Mesh::Builder()
        .setVertexBuffer(0, kor::Mesh::makeBuffer(vertices, Buffer::Usage::eVertex))
        .setIndexBuffer(kor::Mesh::makeBuffer(indices, Buffer::Usage::eIndex))
        .setVertexLayout(kor::VertexLayout {
            .bindings = {
                kor::VertexInputBindingDescription(0, sizeof(HandWrittenVertex)),
            },
            .attributes = {
                kor::VertexLayout::Attribute::AtLocation(1, 0, offsetof(HandWrittenVertex, color), kor::ChannelType::eFloat, 3),
                kor::VertexLayout::Attribute::AtLocation(0, 0, offsetof(HandWrittenVertex, position), kor::ChannelType::eFloat, 3),
            },
        })
        .build();
    ASSERT_TRUE(mesh.valid()) << (mesh.error() ? mesh.error()->history() : "");
    EXPECT_EQ(mesh->vertexCount(), vertices.size());

    auto color = Image::Builder{}
                     .setType(Image::Type::e2D)
                     .setFormat(Image::Format::eRGBA8_UNORM)
                     .setExtent(glm::uvec2{kW, kH})
                     .setUsage(Image::Usage::eColorAttachment | Image::Usage::eTransferSrc)
                     .build();
    auto colorView = ImageView::Builder(color).build();
    auto framebuffer = Framebuffer::Builder{}
                           .addColor({ .view = colorView, .clear = glm::vec4{0.f, 0.f, 0.f, 1.f} })
                           .build();

    const ResourceRef<const Shader> vert =
        Shader::Builder{}.setPath("vertexColorLocations.vert.glsl").getOrBuild("meshBuilder.locations.vert");
    const ResourceRef<const Shader> frag =
        Shader::Builder{}.setPath("vertexColor.frag.glsl").getOrBuild("meshBuilder.locations.frag");

    auto pipeline = GraphicsPipeline::Builder{}
                        .setVertexShader(vert, mesh->vertexLayout())
                        .setFragmentShader(frag)
                        .setFramebuffer(framebuffer)
                        .build();
    ASSERT_TRUE(pipeline.valid()) << (pipeline.error() ? pipeline.error()->history() : "");

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginRendering(framebuffer);
        cb.BindGraphicsPipeline(pipeline);
        cb.SetViewport(0, 0, kW, kH);
        cb.SetScissor(0, 0, kW, kH);
        cb.BindMesh(mesh);
        cb.DrawIndexed();
        cb.EndRendering();
    }, CommandBuffer::Usage::eGraphics);

    Buffer::RawBuilder rb;
    rb.setRawSize(static_cast<glm::i64>(kW) * kH * sizeof(Pixel))
      .setUsage(Buffer::Usage::eTransferDst)
      .setType(Buffer::Type::eReadback);
    auto readback = rb.build();
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(color, readback);
    }, CommandBuffer::Usage::eTransfer);

    const std::vector<Pixel> out = readback->Read<Pixel>();
    ASSERT_EQ(out.size(), static_cast<std::size_t>(kW) * kH);
    for (std::size_t i = 0; i < out.size(); ++i) {
        EXPECT_EQ(out[i], Pixel(255, 0, 0, 255)) << "texel " << i;
    }
}

// A buffer handed over as an rvalue belongs to the mesh, which is what geometry nothing else
// refers to wants: the mesh keeps it alive on its own.
TEST_F(GpuTest, MeshBuilderAdoptsBuffersGivenAsRvalues) {
    const std::vector<glm::vec3> positions = {
        glm::vec3{-1.f, -1.f, 0.f}, glm::vec3{3.f, -1.f, 0.f}, glm::vec3{-1.f, 3.f, 0.f},
    };

    kor::Resource<kor::Mesh> mesh;
    {
        auto vertexBuffer = kor::Mesh::makeBuffer(positions, Buffer::Usage::eVertex);
        mesh = kor::Mesh::Builder()
            .setVertexBuffer(0, std::move(vertexBuffer))
            .setVertexLayout(kor::VertexLayout {
                .bindings = { kor::VertexInputBindingDescription(0, sizeof(glm::vec3)) },
                .attributes = {
                    kor::VertexLayout::Attribute("POSITION", "vertex", 0, 0, kor::ChannelType::eFloat, 3),
                },
            })
            .build();
    }   // the local Resource is gone; only the mesh's own hold on the buffer is left

    ASSERT_TRUE(mesh.valid()) << (mesh.error() ? mesh.error()->history() : "");
    EXPECT_EQ(mesh->vertexCount(), positions.size());
    ASSERT_FALSE(mesh->vertexBuffers().empty());
    EXPECT_TRUE(mesh->vertexBuffers().front().valid()) << "the adopted buffer outlives its handle";
    EXPECT_FALSE(mesh->hasIndexBuffer());
}

// Geometry that does not match its description is a poisoned mesh naming what is wrong, not a
// draw that reads the wrong bytes.
TEST_F(GpuTest, MeshBuilderPoisonsMisdescribedGeometry) {
    const std::vector<glm::vec3> positions = {
        glm::vec3{-1.f, -1.f, 0.f}, glm::vec3{3.f, -1.f, 0.f}, glm::vec3{-1.f, 3.f, 0.f},
    };
    const std::vector<glm::vec2> uvs = { glm::vec2{0.f}, glm::vec2{1.f} };   // one vertex short

    auto positionBuffer = kor::Mesh::makeBuffer(positions, Buffer::Usage::eVertex);
    auto uvBuffer = kor::Mesh::makeBuffer(uvs, Buffer::Usage::eVertex);

    const kor::VertexLayout twoBindings {
        .bindings = {
            kor::VertexInputBindingDescription(0, sizeof(glm::vec3)),
            kor::VertexInputBindingDescription(1, sizeof(glm::vec2)),
        },
        .attributes = {
            kor::VertexLayout::Attribute("POSITION", "vertex", 0, 0, kor::ChannelType::eFloat, 3),
            kor::VertexLayout::Attribute("UV", "vertex", 1, 0, kor::ChannelType::eFloat, 2),
        },
    };

    // A binding the layout declares and nothing was set for.
    const auto missing = kor::Mesh::Builder()
        .setVertexBuffer(0, positionBuffer)
        .setVertexLayout(twoBindings)
        .build();
    EXPECT_TRUE(missing.poisoned()) << "binding 1 has no vertex buffer";

    // Buffers that imply different vertex counts.
    const auto disagreeing = kor::Mesh::Builder()
        .setVertexBuffer(0, positionBuffer)
        .setVertexBuffer(1, uvBuffer)
        .setVertexLayout(twoBindings)
        .build();
    EXPECT_TRUE(disagreeing.poisoned()) << "3 positions against 2 UVs";

    // A buffer with nothing describing it.
    const auto undescribed = kor::Mesh::Builder()
        .setVertexBuffer(0, positionBuffer)
        .build();
    EXPECT_TRUE(undescribed.poisoned()) << "no vertex layout was set";

    // A buffer that was never made a vertex buffer.
    auto plain = Buffer::Builder<glm::vec3>()
        .setDataView(positions)
        .setUsage(Buffer::Usage::eStorage)
        .setType(Buffer::Type::eDynamic)    // host-visible, so the data needs no staging copy
        .build();
    const auto wrongUsage = kor::Mesh::Builder()
        .setVertexBuffer(0, plain)
        .setVertexLayout(kor::VertexLayout {
            .bindings = { kor::VertexInputBindingDescription(0, sizeof(glm::vec3)) },
            .attributes = {
                kor::VertexLayout::Attribute("POSITION", "vertex", 0, 0, kor::ChannelType::eFloat, 3),
            },
        })
        .build();
    EXPECT_TRUE(wrongUsage.poisoned()) << "the buffer was not created with Usage::eVertex";
}





// Push-constant blocks are std430, and a shader that asks for anything else does not load.
//
// This is what makes a hand-matched C++ struct a reasonable thing to write: under std140 an array
// of floats strides sixteen bytes instead of four, so a struct that mirrors the block would write
// one value in four and look almost right. The layout is checked against spirv-cross's own std430
// rules, so the answer is the same one the compiler used.
TEST_F(GpuTest, APushConstantBlockThatIsNotStd430DoesNotLoad) {
    const auto shader = Shader::Builder{}
                            .setLang<Shader::Lang::eGLSL>()
                            .setStage(Shader::Stage::eFragment)
                            .setPath(kor::shaderPath("pushStd140.frag.glsl"))
                            .build();

    ASSERT_TRUE(shader.poisoned()) << "an explicitly std140 push-constant block was accepted";
    EXPECT_EQ(shader.error()->code, kor::ErrorCode::ePushConstantMismatch);
    // Named down to the member, since that is what has to move.
    EXPECT_NE(shader.error()->message.find("weights"), std::string::npos) << shader.error()->message;
    EXPECT_NE(shader.error()->message.find("std430"), std::string::npos) << shader.error()->message;
}


// An Image binds straight to a texture binding: the view is built from what the *shader* declared
// the binding as, and owned by the image, so nothing here constructs an ImageView at all.
TEST_F(GpuTest, AnImageBindsWithoutAViewBeingBuilt) {
    constexpr glm::u32 kSize = 8;

    auto texture = Image::Builder{}
        .setFormat(Image::Format::eRGBA8_UNORM)
        .setExtent(glm::uvec2{kSize, kSize})
        .setUsage(Image::Usage::eSampled | Image::Usage::eTransferDst)
        .build();
    ASSERT_TRUE(texture.valid());

    auto sampler = Sampler::Builder{}.build();
    ASSERT_TRUE(sampler.valid());

    const auto vert = Shader::Builder{}.setLang<Shader::Lang::eGLSL>().setStage(Shader::Stage::eVertex)
        .setPath(kor::shaderPath("sampleTexture.vert.glsl")).getOrBuild("test.imgbind.vert");
    const auto frag = Shader::Builder{}.setLang<Shader::Lang::eGLSL>().setStage(Shader::Stage::eFragment)
        .setPath(kor::shaderPath("sampleTexture.frag.glsl")).getOrBuild("test.imgbind.frag");

    auto target = Image::Builder{}
        .setFormat(Image::Format::eRGBA8_UNORM)
        .setExtent(glm::uvec2{kSize, kSize})
        .setUsage(Image::Usage::eColorAttachment)
        .build();
    ASSERT_TRUE(target.valid());

    // The framebuffer takes the Image too, and names the target so it can be found again.
    auto framebuffer = Framebuffer::Builder{}
        .addColor({ .name = "color", .view = target })
        .build();
    ASSERT_TRUE(framebuffer.valid()) << (framebuffer.error() ? framebuffer.error()->history() : "");

    auto pipeline = GraphicsPipeline::Builder{}
        .setVertexShader(vert).setFragmentShader(frag)
        .setFramebuffer(framebuffer)
        .build();
    ASSERT_TRUE(pipeline.valid());

    // `uniform sampler2D tex` — the binding says 2D, so that is the view the image is asked for.
    auto set = DescriptorSet::Builder(pipeline, 0).write("tex", texture, sampler).build();
    ASSERT_TRUE(set.valid()) << (set.error() ? set.error()->history() : "");

    // The view is owned by the image and shared, so a second bind of the same texture is the same
    // view object rather than another one.
    EXPECT_EQ(texture->view(kor::ImageShape::e2D).get(), texture->view(kor::ImageShape::e2D).get());

    // And the framebuffer hands its target back by name.
    EXPECT_EQ(framebuffer->image("color").get(), target.get());
    EXPECT_FALSE(framebuffer->image("nosuchtarget").valid());
}

// The view an image hands out is a view of *its storage*, and a resize replaces that storage. The
// cache has to be dropped with it or the next binding gets a view of freed memory.
TEST_F(GpuTest, ResizingAnImageDropsTheViewsItHandedOut) {
    auto image = Image::Builder{}
        .setFormat(Image::Format::eRGBA8_UNORM)
        .setExtent(glm::uvec2{8, 8})
        .setUsage(Image::Usage::eSampled | Image::Usage::eColorAttachment)
        .build();
    ASSERT_TRUE(image.valid());

    const auto before = image->view(kor::ImageShape::e2D);
    ASSERT_TRUE(before.valid());
    const auto generationBefore = image->generation();

    const_cast<Image&>(*image).Resize({16, 16, 1});
    ASSERT_NE(image->generation(), generationBefore) << "the resize did not replace the image";

    const auto after = image->view(kor::ImageShape::e2D);
    ASSERT_TRUE(after.valid());
    EXPECT_NE(after.get(), before.get())
        << "the image handed out a view of the storage the resize threw away";
}

// Binding an image to a binding its usage does not allow says which flag is missing, rather than
// leaving the driver to complain about usage bits.
TEST_F(GpuTest, AnImageMissingItsUsageIsReportedWithTheFlagToAdd) {
    auto texture = Image::Builder{}
        .setFormat(Image::Format::eRGBA8_UNORM)
        .setExtent(glm::uvec2{8, 8})
        .setUsage(Image::Usage::eColorAttachment)   // deliberately not eSampled
        .build();
    ASSERT_TRUE(texture.valid());

    auto sampler = Sampler::Builder{}.build();
    const auto vert = Shader::Builder{}.setLang<Shader::Lang::eGLSL>().setStage(Shader::Stage::eVertex)
        .setPath(kor::shaderPath("sampleTexture.vert.glsl")).getOrBuild("test.usage.vert");
    const auto frag = Shader::Builder{}.setLang<Shader::Lang::eGLSL>().setStage(Shader::Stage::eFragment)
        .setPath(kor::shaderPath("sampleTexture.frag.glsl")).getOrBuild("test.usage.frag");

    auto target = Image::Builder{}.setFormat(Image::Format::eRGBA8_UNORM)
        .setExtent(glm::uvec2{8, 8}).setUsage(Image::Usage::eColorAttachment).build();
    auto framebuffer = Framebuffer::Builder{}.addColor({ .view = target }).build();
    auto pipeline = GraphicsPipeline::Builder{}
        .setVertexShader(vert).setFragmentShader(frag)
        .setFramebuffer(framebuffer).build();
    ASSERT_TRUE(pipeline.valid());

    auto set = DescriptorSet::Builder(pipeline, 0).write("tex", texture, sampler).build();
    ASSERT_FALSE(set.valid()) << "an image with no eSampled usage was bound to a texture binding";
    ASSERT_NE(set.error(), nullptr);
    const auto message = set.error()->history();
    EXPECT_NE(message.find("eSampled"), std::string::npos) << message;
}

// The transfer usages are on by default, so the commands that need them work without anyone having
// to have thought about it. This is the case that used to fail as a driver validation message about
// usage bits, long after the line that actually caused it.
TEST_F(GpuTest, TransferUsageIsNotSomethingYouHaveToRemember) {
    // No setUsage at all, and every one of these needs a transfer role: the upload needs
    // eTransferDst on the image, the mip chain needs both, and the readback needs eTransferSrc.
    auto texture = Image::Builder{}
        .setFormat(Image::Format::eRGBA8_UNORM)
        .setExtent(glm::uvec2{8, 8})
        .setData(std::vector<glm::u8vec4>(8 * 8, glm::u8vec4{40, 80, 120, 255}))
        .build();
    ASSERT_TRUE(texture.valid()) << (texture.error() ? texture.error()->history() : "");

    Buffer::RawBuilder rb;
    rb.setRawSize(8 * 8 * 4).setType(Buffer::Type::eReadback);
    auto readback = rb.build();
    ASSERT_TRUE(readback.valid()) << (readback.error() ? readback.error()->history() : "");

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(texture, readback);
        EXPECT_TRUE(cb.ok()) << "a plain image and a plain buffer could not be copied between: "
                             << (cb.ok() ? "" : cb.result().error().toString());
    }, CommandBuffer::Usage::eTransfer);

    const auto pixels = readback->Read<glm::u8vec4>();
    ASSERT_EQ(pixels.size(), static_cast<std::size_t>(8 * 8));
    EXPECT_EQ(pixels[0], (glm::u8vec4{40, 80, 120, 255}));
}

// Opting out is still possible, and getting it wrong afterwards is now Koral's error rather than
// the driver's: it names the flag, the role and the command, at the line that recorded it.
TEST_F(GpuTest, AMissingTransferUsageIsNamedAtTheCommandThatNeededIt) {
    auto image = Image::Builder{}
        .setFormat(Image::Format::eRGBA8_UNORM)
        .setExtent(glm::uvec2{8, 8})
        .setUsage(Image::Usage::eSampled)   // exactly this: no transfer roles
        .build();
    ASSERT_TRUE(image.valid());

    Buffer::RawBuilder rb;
    rb.setRawSize(8 * 8 * 4).setType(Buffer::Type::eReadback);
    auto readback = rb.build();
    ASSERT_TRUE(readback.valid());

    std::string message;
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(image, readback);
        EXPECT_FALSE(cb.ok()) << "an image with no eTransferSrc was copied from anyway";
        if (!cb.ok()) message = cb.result().error().toString();
    }, CommandBuffer::Usage::eTransfer);

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
    small.setRawSize(1024);
    auto smallBuffer = small.build();
    ASSERT_TRUE(smallBuffer.valid()) << (smallBuffer.error() ? smallBuffer.error()->history() : "");
    EXPECT_TRUE(smallBuffer->usage() & Buffer::Usage::eUniform);

    // Too large to ever be one, so the role is not added — and, crucially, the buffer still builds.
    // Blanket-defaulting eUniform is exactly what this would have broken.
    Buffer::RawBuilder large;
    large.setRawSize(16 * 1024 * 1024);
    auto largeBuffer = large.build();
    ASSERT_TRUE(largeBuffer.valid()) << (largeBuffer.error() ? largeBuffer.error()->history() : "");
    EXPECT_FALSE(largeBuffer->usage() & Buffer::Usage::eUniform);
    EXPECT_TRUE(largeBuffer->usage() & Buffer::Usage::eStorage)
        << "the free roles should still be on";

    // Asking for it outright on a buffer that cannot hold it is still an error: the caller said
    // something impossible, and is told so rather than quietly given a buffer that is not what it
    // asked for.
    Buffer::RawBuilder impossible;
    impossible.setRawSize(16 * 1024 * 1024).setUsage(Buffer::Usage::eUniform);
    auto impossibleBuffer = impossible.build();
    EXPECT_FALSE(impossibleBuffer.valid()) << "an oversized uniform buffer was accepted";

    // And setUsage still means exactly what it says — no deduction on top of an explicit set.
    Buffer::RawBuilder exact;
    exact.setRawSize(1024).setUsage(Buffer::Usage::eStorage);
    auto exactBuffer = exact.build();
    ASSERT_TRUE(exactBuffer.valid());
    EXPECT_FALSE(exactBuffer->usage() & Buffer::Usage::eUniform)
        << "setUsage named an exact set and something was added to it anyway";
}

} // namespace
