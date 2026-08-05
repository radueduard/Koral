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
      .addUsage(Image::Usage::eColorAttachment) // rendered into
      .addUsage(Image::Usage::eTransferSrc);    // read back afterwards
    auto colorImage = ib.build();

    auto colorView = ImageView::Builder(ResourceRef<const Image>(colorImage))
                         .build();

    auto framebuffer =
        Framebuffer::Builder{}
            .addColorAttachment(ResourceRef<const ImageView>(colorView), glm::vec4{0.f, 0.f, 0.f, 1.f})
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
            .setFramebuffer(ResourceRef<Framebuffer>(framebuffer))
            .build();

    // --- record the draw --------------------------------------------------
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        // BeginRendering clears any bound pipeline, so bind inside the render scope.
        cb.BeginRendering(ResourceRef<const Framebuffer>(framebuffer));
        cb.BindGraphicsPipeline(ResourceRef<const GraphicsPipeline>(pipeline));
        cb.SetViewport(0, 0, kW, kH);
        cb.SetScissor(0, 0, kW, kH);
        cb.Draw(3); // full-screen triangle, no vertex/descriptor inputs
        cb.EndRendering();
    }, CommandBuffer::Usage::eGraphics);

    // --- read the target back and verify ---------------------------------
    Buffer::RawBuilder rb;
    rb.setRawSize(static_cast<glm::i64>(kW) * kH * sizeof(Pixel))
      .addUsage(Buffer::Usage::eTransferDst)
      .setType(Buffer::Type::eReadback);
    auto readback = rb.build();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(ResourceRef<const Image>(colorImage), ResourceRef<const Buffer>(readback));
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
      .addUsage(Image::Usage::eColorAttachment)
      .addUsage(Image::Usage::eTransferSrc);
    auto colorImage = ib.build();

    auto colorView = ImageView::Builder(ResourceRef<const Image>(colorImage)).build();
    auto framebuffer =
        Framebuffer::Builder{}
            .addColorAttachment(ResourceRef<const ImageView>(colorView), glm::vec4{0.f, 0.f, 0.f, 1.f})
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
            .setFramebuffer(ResourceRef<Framebuffer>(framebuffer))
            .build();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginRendering(ResourceRef<const Framebuffer>(framebuffer));
        cb.BindGraphicsPipeline(ResourceRef<const GraphicsPipeline>(pipeline));
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
      .addUsage(Buffer::Usage::eTransferDst)
      .setType(Buffer::Type::eReadback);
    auto readback = rb.build();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(ResourceRef<const Image>(colorImage), ResourceRef<const Buffer>(readback));
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
                     .addUsage(Image::Usage::eColorAttachment)
                     .addUsage(Image::Usage::eTransferSrc)
                     .build();
    auto depth = Image::Builder{}
                     .setType(Image::Type::e2D)
                     .setFormat(Image::Format::eD32_SFLOAT)
                     .setExtent(glm::uvec2{kW, kH})
                     .addUsage(Image::Usage::eDepthStencilAttachment)
                     .build();

    auto colorView = ImageView::Builder(ResourceRef<const Image>(color)).build();
    auto depthView = ImageView::Builder(ResourceRef<const Image>(depth)).build();

    auto framebuffer = Framebuffer::Builder{}
                           .addColorAttachment(ResourceRef<const ImageView>(colorView), glm::vec4{0.f, 0.f, 0.f, 1.f})
                           .setDepthAttachment(ResourceRef<const ImageView>(depthView), 1.f)
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
                        .setFramebuffer(ResourceRef<Framebuffer>(framebuffer))
                        .setColorBlendState(blend)
                        .setDepthStencilState(depthState)
                        .build();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginRendering(ResourceRef<const Framebuffer>(framebuffer));
        cb.BindGraphicsPipeline(ResourceRef<const GraphicsPipeline>(pipeline));
        cb.SetViewport(0, 0, kW, kH);
        cb.SetScissor(0, 0, kW, kH);
        cb.Draw(3);
        cb.EndRendering();
    }, CommandBuffer::Usage::eGraphics);

    Buffer::RawBuilder rb;
    rb.setRawSize(static_cast<glm::i64>(kW) * kH * sizeof(Pixel))
      .addUsage(Buffer::Usage::eTransferDst)
      .setType(Buffer::Type::eReadback);
    auto readback = rb.build();
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(ResourceRef<const Image>(color), ResourceRef<const Buffer>(readback));
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
                     .addUsage(Image::Usage::eColorAttachment)
                     .addUsage(Image::Usage::eTransferSrc)
                     .build();
    auto colorView = ImageView::Builder(ResourceRef<const Image>(color)).build();
    auto framebuffer = Framebuffer::Builder{}
                           .addColorAttachment(ResourceRef<const ImageView>(colorView), glm::vec4{0.f, 0.f, 0.f, 1.f})
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
                        .setFramebuffer(ResourceRef<Framebuffer>(framebuffer))
                        .build();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginRendering(ResourceRef<const Framebuffer>(framebuffer));
        cb.BindGraphicsPipeline(ResourceRef<const GraphicsPipeline>(pipeline));
        cb.SetViewport(0, 0, kW, kH);
        cb.SetScissor(0, 0, kW, kH);
        cb.BindMesh(ResourceRef<const kor::Mesh>(mesh));
        cb.DrawIndexed(); // uses the bound mesh's index count
        cb.EndRendering();
    }, CommandBuffer::Usage::eGraphics);

    Buffer::RawBuilder rb;
    rb.setRawSize(static_cast<glm::i64>(kW) * kH * sizeof(Pixel))
      .addUsage(Buffer::Usage::eTransferDst)
      .setType(Buffer::Type::eReadback);
    auto readback = rb.build();
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(ResourceRef<const Image>(color), ResourceRef<const Buffer>(readback));
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
                     .addUsage(Image::Usage::eColorAttachment)
                     .addUsage(Image::Usage::eTransferSrc)
                     .build();
    auto colorView = ImageView::Builder(ResourceRef<const Image>(color)).build();
    auto framebuffer = Framebuffer::Builder{}
                           .addColorAttachment(ResourceRef<const ImageView>(colorView), glm::vec4{0.f, 0.f, 0.f, 1.f})
                           .build();

    const ResourceRef<const Shader> vert =
        Shader::Builder{}.setPath("meshTriangle.vert.glsl").getOrBuild("orient.meshTriangle.vert");
    const ResourceRef<const Shader> frag =
        Shader::Builder{}.setPath("flatTriangle.frag.glsl").getOrBuild("orient.flatTriangle.frag");

    auto pipeline = GraphicsPipeline::Builder{}
                        .setVertexShader(vert, PosMesh::Layout())
                        .setFragmentShader(frag)
                        .setFramebuffer(ResourceRef<Framebuffer>(framebuffer))
                        .build();   // culling is off by default, so winding cannot mask the result
    ASSERT_TRUE(pipeline.valid()) << pipeline.error()->history();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginRendering(ResourceRef<const Framebuffer>(framebuffer));
        cb.BindGraphicsPipeline(ResourceRef<const GraphicsPipeline>(pipeline));
        cb.SetViewport(0, 0, kW, kH);
        cb.SetScissor(0, 0, kW, kH);
        cb.BindMesh(ResourceRef<const kor::Mesh>(mesh));
        cb.DrawIndexed();
        cb.EndRendering();
    }, CommandBuffer::Usage::eGraphics);

    Buffer::RawBuilder rb;
    rb.setRawSize(static_cast<glm::i64>(kW) * kH * sizeof(Pixel))
      .addUsage(Buffer::Usage::eTransferDst)
      .setType(Buffer::Type::eReadback);
    auto readback = rb.build();
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(ResourceRef<const Image>(color), ResourceRef<const Buffer>(readback));
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
      .addUsage(Image::Usage::eColorAttachment)
      .addUsage(Image::Usage::eSampled);
    auto colorImage = ib.build();
    ASSERT_TRUE(colorImage.valid());

    auto colorView = ImageView::Builder(ResourceRef<const Image>(colorImage)).build();
    auto framebuffer = Framebuffer::Builder{}
                           .addColorAttachment(ResourceRef<const ImageView>(colorView), glm::vec4{0.f, 0.f, 0.f, 1.f})
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
                        .setFramebuffer(ResourceRef<Framebuffer>(framebuffer))
                        .build();
    ASSERT_TRUE(pipeline.valid());

    // The attachment, bound as a texture to the pipeline drawing into it.
    auto descriptorSet = DescriptorSet::Builder(kor::ResourceRef<const kor::Pipeline>(pipeline), 0)
                             .write(0, Descriptor(ResourceRef<const ImageView>(colorView),
                                                  ResourceRef<const Sampler>(sampler)))
                             .build();
    ASSERT_TRUE(descriptorSet.valid());

    const auto cb = CommandBuffer::Create(CommandBuffer::Usage::eGraphics);
    cb->Begin();
    cb->BeginRendering(ResourceRef<const Framebuffer>(framebuffer));
    cb->BindGraphicsPipeline(ResourceRef<const GraphicsPipeline>(pipeline));
    cb->SetViewport(0, 0, kSize, kSize);
    cb->SetScissor(0, 0, kSize, kSize);
    cb->BindDescriptorSet(0, ResourceRef<const DescriptorSet>(descriptorSet));
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
        .addUsage(Image::Usage::eColorAttachment)
        .addUsage(Image::Usage::eTransferSrc)
        .build();
    auto colorView = ImageView::Builder(ResourceRef<const Image>(color)).build();
    auto framebuffer = Framebuffer::Builder{}
        .addColorAttachment(ResourceRef<const ImageView>(colorView), glm::vec4{0.f, 0.f, 0.f, 1.f})
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
        .setFramebuffer(ResourceRef<Framebuffer>(framebuffer))
        .build();
    ASSERT_TRUE(static_cast<bool>(pipeline)) << (pipeline.error() ? pipeline.error()->message : "");

    Buffer::RawBuilder rb;
    rb.setRawSize(static_cast<glm::i64>(kSize) * kSize * 4)
      .addUsage(Buffer::Usage::eTransferDst)
      .setType(Buffer::Type::eReadback);
    auto readback = rb.build();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginRendering(ResourceRef<Framebuffer>(framebuffer))
          .BindGraphicsPipeline(ResourceRef<const GraphicsPipeline>(pipeline))
          // No SetViewport, no SetScissor: that is the case under test.
          .Draw(3)
          .EndRendering();
        cb.CopyImageToBuffer(ResourceRef<const Image>(color), ResourceRef<const Buffer>(readback));
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
        .addUsage(Image::Usage::eColorAttachment)
        .addUsage(Image::Usage::eTransferSrc)
        .build();
    auto depth = Image::Builder{}
        .setType(Image::Type::e2D)
        .setFormat(Image::Format::eD32_SFLOAT)
        .setExtent(glm::uvec2{kFirst, kFirst})
        .addUsage(Image::Usage::eDepthStencilAttachment)
        .build();

    auto colorView = ImageView::Builder(ResourceRef<const Image>(color)).build();
    auto depthView = ImageView::Builder(ResourceRef<const Image>(depth)).build();

    auto framebuffer = Framebuffer::Builder{}
        .addColorAttachment(ResourceRef<const ImageView>(colorView), glm::vec4{0.f, 0.f, 0.f, 1.f})
        .setDepthAttachment(ResourceRef<const ImageView>(depthView))
        .build();
    ASSERT_TRUE(static_cast<bool>(framebuffer));
    const auto generationBefore = color->generation();

    framebuffer->Resize(glm::uvec2{kSecond, kSecond});

    EXPECT_EQ(framebuffer->getExtent(), glm::uvec2(kSecond, kSecond));
    EXPECT_EQ(color->getExtent(), glm::uvec3(kSecond, kSecond, 1)) << "the colour attachment followed";
    EXPECT_EQ(depth->getExtent(), glm::uvec3(kSecond, kSecond, 1)) << "and so did the depth one";
    // The image was *replaced*, which is what tells a view holding the old one to rebuild.
    EXPECT_GT(color->generation(), generationBefore);

    // And it is usable at the new size: rendering into it and reading it back is the whole point of
    // having resized it. A view that had not noticed the replacement would fault here.
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginRendering(ResourceRef<Framebuffer>(framebuffer));
        cb.EndRendering();
    }, CommandBuffer::Usage::eGraphics);

    Buffer::RawBuilder rb;
    rb.setRawSize(static_cast<glm::i64>(kSecond) * kSecond * 4)
      .addUsage(Buffer::Usage::eTransferDst)
      .setType(Buffer::Type::eReadback);
    auto readback = rb.build();
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(ResourceRef<const Image>(color), ResourceRef<const Buffer>(readback));
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
        .addUsage(Image::Usage::eColorAttachment)
        .build();
    auto colorView = ImageView::Builder(ResourceRef<const Image>(color)).build();
    auto framebuffer = Framebuffer::Builder{}
        .addColorAttachment(ResourceRef<const ImageView>(colorView), glm::vec4{0.f})
        .build();

    const auto generation = color->generation();
    framebuffer->Resize(glm::uvec2{32, 32});
    framebuffer->Resize(glm::uvec2{0, 16});     // a zero extent names nothing and is ignored
    EXPECT_EQ(color->generation(), generation) << "the image was not replaced";
    EXPECT_EQ(color->getExtent(), glm::uvec3(32, 32, 1));
}

} // namespace

