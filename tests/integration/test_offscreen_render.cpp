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
#include "meshLayout.h"
#include "shader.h"

using gfx::Buffer;
using gfx::CommandBuffer;
using gfx::Framebuffer;
using gfx::GraphicsPipeline;
using gfx::Image;
using gfx::ImageView;
using gfx::ResourceRef;
using gfx::Shader;

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
            .setPath(gfx::shaderPath("flatTriangle.vert.glsl"))
            .getOrBuild("test.flatTriangle.vert");

    const ResourceRef<const Shader> frag =
        Shader::Builder{}
            .setLang<Shader::Lang::eGLSL>()
            .setStage(Shader::Stage::eFragment)
            .setPath(gfx::shaderPath("flatTriangle.frag.glsl"))
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
            .setPath(gfx::shaderPath("flatTriangle.vert.glsl")).getOrBuild("test.flatTriangle.vert");
    const ResourceRef<const Shader> frag =
        Shader::Builder{}.setLang<Shader::Lang::eGLSL>().setStage(Shader::Stage::eFragment)
            .setPath(gfx::shaderPath("flatTriangle.frag.glsl")).getOrBuild("test.flatTriangle.frag");

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
        cb.SetFrontFace(gfx::FrontFace::eCounterClockwise);
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
            .setPath(gfx::shaderPath("flatTriangle.vert.glsl")).getOrBuild("test.flatTriangle.vert");
    const ResourceRef<const Shader> frag =
        Shader::Builder{}.setLang<Shader::Lang::eGLSL>().setStage(Shader::Stage::eFragment)
            .setPath(gfx::shaderPath("flatTriangle.frag.glsl")).getOrBuild("test.flatTriangle.frag");

    // Explicit alpha-blend attachment state + depth test on.
    gfx::ColorBlendState blend;
    blend.attachments.push_back(gfx::ColorBlendState::AttachmentState{
        .blendEnable = true,
        .srcColorBlendFactor = gfx::BlendFactor::eSrcAlpha,
        .dstColorBlendFactor = gfx::BlendFactor::eOneMinusSrcAlpha,
    });
    gfx::DepthStencilState depthState;
    depthState.depthTestEnable = true;
    depthState.depthWriteEnable = true;
    depthState.depthCompareOp = gfx::CompareOp::eLessOrEqual;

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
    using PosVertex = gfx::ParamVertex<gfx::Position>;
    using PosMesh = gfx::ParamMesh<PosVertex>;

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
            .setPath(gfx::shaderPath("meshTriangle.vert.glsl")).getOrBuild("test.meshTriangle.vert");
    const ResourceRef<const Shader> frag =
        Shader::Builder{}.setLang<Shader::Lang::eGLSL>().setStage(Shader::Stage::eFragment)
            .setPath(gfx::shaderPath("flatTriangle.frag.glsl")).getOrBuild("test.flatTriangle.frag");

    auto pipeline = GraphicsPipeline::Builder{}
                        .setVertexShader<PosMesh>(vert) // vertex-input state from the mesh layout
                        .setFragmentShader(frag)
                        .setFramebuffer(ResourceRef<Framebuffer>(framebuffer))
                        .build();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginRendering(ResourceRef<const Framebuffer>(framebuffer));
        cb.BindGraphicsPipeline(ResourceRef<const GraphicsPipeline>(pipeline));
        cb.SetViewport(0, 0, kW, kH);
        cb.SetScissor(0, 0, kW, kH);
        cb.BindMesh(ResourceRef<const gfx::Mesh>(mesh));
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

} // namespace
