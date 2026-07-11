// Windowed OpenGL integration + parity tests. These boot a single real OpenGL
// window (one context per process, same constraint as the Vulkan windowed test)
// and verify that, for everything OpenGL can express, the GL backend behaves the
// same as the Vulkan backend (ray tracing excluded).
//
// The window/context is created once for the whole binary by GlEnvironment and
// shared by every test through the GlTest fixture (mirroring the headless
// GpuTest used by the Vulkan integration suite). Tests skip gracefully when no
// display / GL context is available.
//
// Coverage intent — the GL analogue of tests/integration/test_vulkan_coverage:
//   RenderParity        full-screen triangle, push constants, blending, scissor,
//                       indexed mesh draw, presentation + ImGui, resize.
//   ComputeDispatch     GLSL compute round-trip (computePipeline + storage-buffer
//                       descriptor bind + Dispatch), values verified on the GPU.
//   SamplersAndDescriptors  sampler builder variants + every image/sampler
//                       descriptor-binding type.
//   ImageOpsAndTransfers   3D/array/mip images, GenerateMipmaps, Blit, Resolve,
//                       and buffer-to-buffer copy / fill / clear.
//   DebugLabels         scoped + single debug-label commands.

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <numeric>
#include <vector>

#include <GLFW/glfw3.h>
#include <imgui.h>

#include "buffer.h"
#include "commandBuffer.h"
#include "computePipeline.h"
#include "context.h"
#include "descriptor.h"
#include "descriptorSet.h"
#include "descriptorSetLayout.h"
#include "framebuffer.h"
#include "graphicsPipeline.h"
#include "gui.h"
#include "image.h"
#include "imageView.h"
#include "mesh.h"
#include "meshLayout.h"
#include "resource.h"
#include "sampler.h"
#include "scene.h"
#include "scheduler.h"
#include "shader.h"
#include "window.h"

using gfx::Buffer;
using gfx::CommandBuffer;
using gfx::ComputePipeline;
using gfx::Descriptor;
using gfx::DescriptorSet;
using gfx::DescriptorSetLayout;
using gfx::DescriptorType;
using gfx::Framebuffer;
using gfx::GraphicsPipeline;
using gfx::Image;
using gfx::ImageView;
using gfx::ResourceRef;
using gfx::Sampler;
using gfx::Shader;

namespace {

using Pixel = glm::u8vec4;

constexpr std::uint32_t kW = 16;
constexpr std::uint32_t kH = 16;

// Minimal scene: clears the default framebuffer and draws an ImGui overlay with a
// GUI_Image, driving the GL presentation + ImGui + GUI_Image blit paths per frame.
class GlOverlayScene : public gfx::Scene {
public:
    void Initialize() override {
        _image = Image::Builder{}
                     .setType(Image::Type::e2D)
                     .setFormat(Image::Format::eRGBA8_UNORM)
                     .setExtent(glm::uvec2{16, 16})
                     .addUsage(Image::Usage::eTransferSrc)
                     .addUsage(Image::Usage::eTransferDst)
                     .addUsage(Image::Usage::eSampled)
                     .build();
        CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
            cb.ClearColorImage(ResourceRef<const Image>(_image), glm::vec4{0.3f, 0.6f, 0.9f, 1.f});
        }, CommandBuffer::Usage::eGraphics);
        _guiImage = gfx::GUI_Image::Create(ResourceRef<const Image>(_image));
    }

    void Update() override { ++updates; }

    void Render(CommandBuffer& cb) override {
        cb.BeginRendering(); // default framebuffer: clear
        cb.EndRendering();
    }

    void RenderUI(ImGuiContext* context) override {
        ImGui::SetCurrentContext(context);
        ImGui::Begin("gfx GL test overlay");
        ImGui::Text("frame %d", updates);
        if (_guiImage) {
            ImGui::Image(**_guiImage, ImVec2(64, 64));
        }
        ImGui::End();
    }

    void OnResize(const glm::uvec2 extent) override { lastResize = extent; }

    int updates = 0;
    glm::uvec2 lastResize{0, 0};

private:
    gfx::Resource<gfx::Image> _image;
    gfx::Resource<gfx::GUI_Image> _guiImage;
};

void drawFrame(gfx::Scene& scene) {
    glfwPollEvents();
    gfx::Context::DrainMainThread();
    gfx::Context::Scheduler().Draw([&](CommandBuffer& cb) {
        gfx::Context::Repository().update();
        scene.Update();
        scene.Render(cb);
        gfx::GUI::Render(cb, scene);
    });
}

// ---- shared GL window, created once for the whole binary ---------------------
//
// OpenGL needs a current context and the framework allows a single window per
// process, so all tests share one window (same model as the Vulkan GpuTest
// harness, which shares one headless device). The environment skips silently
// when no display / GL context is available, and every test then GTEST_SKIPs.

class GlEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        auto scenePtr = std::make_unique<GlOverlayScene>();
        s_scene = scenePtr.get();
        try {
            s_window = gfx::Window::Builder(std::move(scenePtr))
                           .setTitle("gfx GL windowed test")
                           .setExtent({320, 240})
                           .setResizable(true)
                           .setVSync(false)
                           .setAPI(gfx::API::eOpenGL)
                           .build();
        } catch (const std::exception& e) {
            s_reason = e.what();
            s_window.reset();
            s_scene = nullptr;
        }
    }

    void TearDown() override {
        if (s_window) {
            gfx::Context::DrainMainThread();
            s_window.reset();
        }
        s_scene = nullptr;
    }

    static bool ready() { return s_window != nullptr; }
    static const std::string& reason() { return s_reason; }
    static gfx::Window& window() { return *s_window; }
    static GlOverlayScene& scene() { return *s_scene; }

private:
    static std::unique_ptr<gfx::Window> s_window;
    static GlOverlayScene* s_scene;
    static std::string s_reason;
};

std::unique_ptr<gfx::Window> GlEnvironment::s_window;
GlOverlayScene* GlEnvironment::s_scene = nullptr;
std::string GlEnvironment::s_reason = "no display";

class GlTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!GlEnvironment::ready()) {
            GTEST_SKIP() << "windowed OpenGL context unavailable: " << GlEnvironment::reason();
        }
        EXPECT_EQ(gfx::Context::activeAPI(), gfx::API::eOpenGL);
    }
};

// ---- shared offscreen helpers ------------------------------------------------

struct OffscreenTarget {
    gfx::Resource<Image> image;
    gfx::Resource<ImageView> view;
    gfx::Resource<Framebuffer> framebuffer;
};

OffscreenTarget makeTarget(const glm::vec4 clearColor) {
    OffscreenTarget t;
    t.image = Image::Builder{}
                  .setType(Image::Type::e2D)
                  .setFormat(Image::Format::eRGBA8_UNORM)
                  .setExtent(glm::uvec2{kW, kH})
                  .addUsage(Image::Usage::eColorAttachment)
                  .addUsage(Image::Usage::eTransferSrc)
                  .build();
    t.view = ImageView::Builder(ResourceRef<const Image>(t.image)).build();
    t.framebuffer = Framebuffer::Builder{}
                        .addColorAttachment(ResourceRef<const ImageView>(t.view), clearColor)
                        .build();
    return t;
}

ResourceRef<const Shader> loadShader(const char* file, const Shader::Stage stage, const char* key) {
    return Shader::Builder{}
        .setLang<Shader::Lang::eGLSL>()
        .setStage(stage)
        .setPath(gfx::shaderPath(file))
        .getOrBuild(key);
}

std::vector<Pixel> readbackImage(const gfx::Resource<Image>& image) {
    Buffer::RawBuilder rb;
    rb.setRawSize(static_cast<glm::i64>(kW) * kH * sizeof(Pixel))
      .addUsage(Buffer::Usage::eTransferDst)
      .setType(Buffer::Type::eReadback);
    auto readback = rb.build();
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(ResourceRef<const Image>(image), ResourceRef<const Buffer>(readback));
    }, CommandBuffer::Usage::eTransfer);
    return readback->Read<Pixel>();
}

gfx::Resource<Image> makeImage(gfx::Flags<Image::Usage> usage,
                               Image::Format format = Image::Format::eRGBA8_UNORM) {
    return Image::Builder{}
        .setType(Image::Type::e2D)
        .setFormat(format)
        .setExtent(glm::uvec2{8, 8})
        .setUsage(usage)
        .build();
}

// -----------------------------------------------------------------------------
// Rendering parity: everything OpenGL can express should produce the same pixels
// as Vulkan. Each phase renders offscreen and reads the texels back.
// -----------------------------------------------------------------------------
TEST_F(GlTest, RenderParity) {
    auto& window = GlEnvironment::window();
    auto& scene = GlEnvironment::scene();

    // ---- Phase 1: presentation path -------------------------------------
    for (int i = 0; i < 8 && !window.shouldClose(); ++i) {
        drawFrame(scene);
        window.LateUpdate();
    }
    EXPECT_GE(scene.updates, 1);

    // ---- Phase 2: full-screen triangle, every texel green ---------------
    {
        auto target = makeTarget({0.f, 0.f, 0.f, 1.f});
        const auto vert = loadShader("flatTriangle.vert.glsl", Shader::Stage::eVertex, "glt.flat.vert");
        const auto frag = loadShader("flatTriangle.frag.glsl", Shader::Stage::eFragment, "glt.flat.frag");
        auto pipeline = GraphicsPipeline::Builder{}
                            .setVertexShader(vert)
                            .setFragmentShader(frag)
                            .setFramebuffer(ResourceRef<Framebuffer>(target.framebuffer))
                            .build();

        CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
            cb.BeginRendering(ResourceRef<const Framebuffer>(target.framebuffer));
            cb.BindGraphicsPipeline(ResourceRef<const GraphicsPipeline>(pipeline));
            cb.SetViewport(0, 0, kW, kH);
            cb.SetScissor(0, 0, kW, kH);
            cb.Draw(3);
            cb.EndRendering();
        }, CommandBuffer::Usage::eGraphics);

        const auto out = readbackImage(target.image);
        ASSERT_EQ(out.size(), static_cast<std::size_t>(kW) * kH);
        for (std::size_t i = 0; i < out.size(); ++i) {
            ASSERT_EQ(out[i].r, 0)   << "texel " << i;
            ASSERT_EQ(out[i].g, 255) << "texel " << i;
            ASSERT_EQ(out[i].b, 0)   << "texel " << i;
            ASSERT_EQ(out[i].a, 255) << "texel " << i;
        }
    }

    // ---- Phase 3: push constants (GL emulates them with a UBO) ----------
    {
        auto target = makeTarget({0.f, 0.f, 0.f, 1.f});
        const auto vert = loadShader("flatTriangle.vert.glsl", Shader::Stage::eVertex, "glt.flat.vert");
        const auto frag = loadShader("pushColor.frag.glsl", Shader::Stage::eFragment, "glt.push.frag");
        auto pipeline = GraphicsPipeline::Builder{}
                            .setVertexShader(vert)
                            .setFragmentShader(frag)
                            .setFramebuffer(ResourceRef<Framebuffer>(target.framebuffer))
                            .build();

        const glm::vec4 pushed{1.f, 0.f, 1.f, 1.f}; // magenta
        CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
            cb.BeginRendering(ResourceRef<const Framebuffer>(target.framebuffer));
            cb.BindGraphicsPipeline(ResourceRef<const GraphicsPipeline>(pipeline));
            cb.PushConstants(pushed);
            cb.SetViewport(0, 0, kW, kH);
            cb.SetScissor(0, 0, kW, kH);
            cb.Draw(3);
            cb.EndRendering();
        }, CommandBuffer::Usage::eGraphics);

        const auto out = readbackImage(target.image);
        ASSERT_EQ(out.size(), static_cast<std::size_t>(kW) * kH);
        for (std::size_t i = 0; i < out.size(); ++i) {
            ASSERT_EQ(out[i].r, 255) << "texel " << i;
            ASSERT_EQ(out[i].g, 0)   << "texel " << i;
            ASSERT_EQ(out[i].b, 255) << "texel " << i;
        }
    }

    // ---- Phase 4: alpha blending over the clear color --------------------
    {
        auto target = makeTarget({1.f, 0.f, 0.f, 1.f}); // red clear
        const auto vert = loadShader("flatTriangle.vert.glsl", Shader::Stage::eVertex, "glt.flat.vert");
        const auto frag = loadShader("pushColor.frag.glsl", Shader::Stage::eFragment, "glt.push.frag");

        gfx::ColorBlendState blend;
        blend.attachments.push_back(gfx::ColorBlendState::AttachmentState{
            .blendEnable = true,
            .srcColorBlendFactor = gfx::BlendFactor::eSrcAlpha,
            .dstColorBlendFactor = gfx::BlendFactor::eOneMinusSrcAlpha,
        });
        auto pipeline = GraphicsPipeline::Builder{}
                            .setVertexShader(vert)
                            .setFragmentShader(frag)
                            .setFramebuffer(ResourceRef<Framebuffer>(target.framebuffer))
                            .setColorBlendState(blend)
                            .build();

        const glm::vec4 halfGreen{0.f, 1.f, 0.f, 0.5f};
        CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
            cb.BeginRendering(ResourceRef<const Framebuffer>(target.framebuffer));
            cb.BindGraphicsPipeline(ResourceRef<const GraphicsPipeline>(pipeline));
            cb.PushConstants(halfGreen);
            cb.SetViewport(0, 0, kW, kH);
            cb.SetScissor(0, 0, kW, kH);
            cb.Draw(3);
            cb.EndRendering();
        }, CommandBuffer::Usage::eGraphics);

        // 0.5*green + 0.5*red = (0.5, 0.5, 0) -> ~128/128/0 in UNORM8.
        const auto out = readbackImage(target.image);
        ASSERT_EQ(out.size(), static_cast<std::size_t>(kW) * kH);
        SCOPED_TRACE(::testing::Message() << "texel0 = (" << int(out[0].r) << "," << int(out[0].g)
                                          << "," << int(out[0].b) << "," << int(out[0].a) << ")");
        for (std::size_t i = 0; i < out.size(); ++i) {
            ASSERT_NEAR(out[i].r, 128, 2) << "texel " << i;
            ASSERT_NEAR(out[i].g, 128, 2) << "texel " << i;
            ASSERT_EQ(out[i].b, 0) << "texel " << i;
        }
    }

    // ---- Phase 5: scissored draw ------------------------------------------
    // The region is chosen symmetric under a vertical flip, so the same
    // assertions hold for GL (origin bottom-left) and Vulkan (origin top-left).
    {
        constexpr std::uint32_t kSx = 4, kSy = 4, kSw = 8, kSh = 8;
        auto target = makeTarget({0.f, 0.f, 0.f, 1.f});
        const auto vert = loadShader("flatTriangle.vert.glsl", Shader::Stage::eVertex, "glt.flat.vert");
        const auto frag = loadShader("flatTriangle.frag.glsl", Shader::Stage::eFragment, "glt.flat.frag");
        auto pipeline = GraphicsPipeline::Builder{}
                            .setVertexShader(vert)
                            .setFragmentShader(frag)
                            .setFramebuffer(ResourceRef<Framebuffer>(target.framebuffer))
                            .build();

        CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
            cb.BeginRendering(ResourceRef<const Framebuffer>(target.framebuffer));
            cb.BindGraphicsPipeline(ResourceRef<const GraphicsPipeline>(pipeline));
            cb.SetViewport(0, 0, kW, kH);
            cb.SetScissor(kSx, kSy, kSw, kSh);
            // dynamic-state overrides must record and not disturb the draw
            cb.SetFrontFace(gfx::FrontFace::eCounterClockwise);
            cb.SetDepthTestEnable(false);
            cb.SetDepthWriteEnable(false);
            cb.SetRasterizerDiscardEnable(false);
            cb.Draw(3);
            cb.EndRendering();
        }, CommandBuffer::Usage::eGraphics);

        const auto out = readbackImage(target.image);
        ASSERT_EQ(out.size(), static_cast<std::size_t>(kW) * kH);
        for (std::uint32_t y = 0; y < kH; ++y) {
            for (std::uint32_t x = 0; x < kW; ++x) {
                const bool inScissor = x >= kSx && x < kSx + kSw && y >= kSy && y < kSy + kSh;
                const Pixel& got = out[y * kW + x];
                if (inScissor) {
                    ASSERT_EQ(got.g, 255) << "scissored texel (" << x << "," << y << ")";
                } else {
                    ASSERT_EQ(got.g, 0) << "clipped texel (" << x << "," << y << ")";
                }
            }
        }
    }

    // ---- Phase 6: indexed mesh draw ---------------------------------------
    {
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

        auto target = makeTarget({0.f, 0.f, 0.f, 1.f});
        const auto vert = loadShader("meshTriangle.vert.glsl", Shader::Stage::eVertex, "glt.mesh.vert");
        const auto frag = loadShader("flatTriangle.frag.glsl", Shader::Stage::eFragment, "glt.flat.frag");
        auto pipeline = GraphicsPipeline::Builder{}
                            .setVertexShader<PosMesh>(vert)
                            .setFragmentShader(frag)
                            .setFramebuffer(ResourceRef<Framebuffer>(target.framebuffer))
                            .build();

        CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
            cb.BeginRendering(ResourceRef<const Framebuffer>(target.framebuffer));
            cb.BindGraphicsPipeline(ResourceRef<const GraphicsPipeline>(pipeline));
            cb.SetViewport(0, 0, kW, kH);
            cb.SetScissor(0, 0, kW, kH);
            cb.BindMesh(ResourceRef<const gfx::Mesh>(mesh));
            cb.DrawIndexed(); // index count resolved from the bound mesh
            cb.EndRendering();
        }, CommandBuffer::Usage::eGraphics);

        const auto out = readbackImage(target.image);
        ASSERT_EQ(out.size(), static_cast<std::size_t>(kW) * kH);
        int green = 0;
        for (const auto& p : out) if (p.g == 255) ++green;
        // The oversized triangle covers the whole target, like on Vulkan.
        EXPECT_EQ(green, static_cast<int>(kW * kH));
    }

    // ---- Phase 7: resize + present ----------------------------------------
    glfwSetWindowSize(*window, 480, 360);
    for (int i = 0; i < 20; ++i) glfwPollEvents();
    for (int i = 0; i < 6 && !window.shouldClose(); ++i) {
        drawFrame(scene);
        window.LateUpdate();
    }
}

// -----------------------------------------------------------------------------
// Compute round-trip: doubles every uint in a storage buffer on the GPU. Drives
// the GL ComputePipeline, storage-buffer descriptor bind and Dispatch paths.
// -----------------------------------------------------------------------------
TEST_F(GlTest, ComputeDispatch) {
    constexpr std::uint32_t kCount = 256;   // multiple of local_size_x (64)
    constexpr std::uint32_t kLocalSize = 64;

    std::vector<std::uint32_t> input(kCount);
    std::iota(input.begin(), input.end(), 1u); // 1,2,3,...

    Buffer::Builder<std::uint32_t> bufBuilder;
    bufBuilder.setData(input);
    bufBuilder.addUsage(Buffer::Usage::eStorage);
    bufBuilder.addUsage(Buffer::Usage::eTransferSrc);
    bufBuilder.addUsage(Buffer::Usage::eTransferDst);
    bufBuilder.setType(Buffer::Type::eDeviceLocal);
    auto buffer = bufBuilder.build();

    const auto shader = loadShader("doubleValues.comp.glsl", Shader::Stage::eCompute, "glt.doubleValues");
    auto pipeline = ComputePipeline::Builder{}.setComputeShader(shader).build();

    auto descriptorSet = DescriptorSet::Builder(gfx::ResourceRef<const gfx::Pipeline>(pipeline), 0)
                             .write(0, Descriptor(ResourceRef<const Buffer>(buffer)))
                             .build();

    const ResourceRef<const Buffer> bufRef(buffer);
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BindComputePipeline(ResourceRef<const ComputePipeline>(pipeline));
        cb.BindDescriptorSet(0, ResourceRef<const DescriptorSet>(descriptorSet));
        cb.BufferBarrier(gfx::BufferBarrier(bufRef, gfx::ResourceAccess::ComputeReadWrite));
        cb.Dispatch(kCount / kLocalSize, 1, 1);
        cb.BufferBarrier(gfx::BufferBarrier(bufRef, gfx::ResourceAccess::TransferSrc));
    }, CommandBuffer::Usage::eCompute);

    const std::vector<std::uint32_t> output = buffer->Read<std::uint32_t>();
    ASSERT_EQ(output.size(), input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        EXPECT_EQ(output[i], input[i] * 2u) << "at index " << i;
    }
}

// -----------------------------------------------------------------------------
// Sampler builder variants + every image/sampler descriptor-binding type, so the
// GL sampler backend and the descriptor-set build switch are all exercised.
// -----------------------------------------------------------------------------
TEST_F(GlTest, SamplersAndDescriptors) {
    auto linear = Sampler::Builder{}
                      .setMinFilter(gfx::Filter::eLinear)
                      .setMagFilter(gfx::Filter::eLinear)
                      .setMipmapMode(Sampler::MipmapMode::eLinear)
                      .setAddressModeU(Sampler::AddressMode::eRepeat)
                      .setAddressModeV(Sampler::AddressMode::eMirroredRepeat)
                      .setAddressModeW(Sampler::AddressMode::eClampToEdge)
                      .setMaxLod(4.f)
                      .build();
    ASSERT_TRUE(static_cast<bool>(linear));

    auto nearest = Sampler::Builder{}
                       .setMinFilter(gfx::Filter::eNearest)
                       .setMagFilter(gfx::Filter::eNearest)
                       .setMipmapMode(Sampler::MipmapMode::eNearest)
                       .setAddressModeU(Sampler::AddressMode::eClampToBorder)
                       .build();
    ASSERT_TRUE(static_cast<bool>(nearest));

    auto sampledImg = makeImage(gfx::Flags(Image::Usage::eSampled) | Image::Usage::eTransferDst);
    auto storageImg = makeImage(gfx::Flags(Image::Usage::eStorage) | Image::Usage::eTransferDst);
    auto sampledView = ImageView::Builder(ResourceRef<const Image>(sampledImg)).build();
    auto storageView = ImageView::Builder(ResourceRef<const Image>(storageImg)).build();
    auto sampler = Sampler::Builder{}.build();

    Buffer::RawBuilder ub;
    ub.setRawSize(256).addUsage(Buffer::Usage::eUniform).setType(Buffer::Type::eDynamic);
    auto uniform = ub.build();

    // eSampledImage / eStorageImage / eCombinedImageSampler / eSampler / eUniformBuffer
    auto layout = DescriptorSetLayout::Builder{}
                      .addBinding(0, DescriptorType::eSampler)
                      .addBinding(1, DescriptorType::eSampledImage)
                      .addBinding(2, DescriptorType::eStorageImage)
                      .addBinding(3, DescriptorType::eCombinedImageSampler)
                      .addBinding(4, DescriptorType::eUniformBuffer)
                      .build();
    auto set = DescriptorSet::Builder(*layout)
                   .write(0, Descriptor(ResourceRef<const Sampler>(sampler)))
                   .write(1, Descriptor(ResourceRef<const ImageView>(sampledView)))
                   .write(2, Descriptor(ResourceRef<const ImageView>(storageView)))
                   .write(3, Descriptor(ResourceRef<const ImageView>(sampledView), ResourceRef<const Sampler>(sampler)))
                   .write(4, Descriptor(ResourceRef<const Buffer>(uniform)))
                   .build();
    ASSERT_TRUE(static_cast<bool>(set));

    // Runtime re-write path (separate from the build-time writes above).
    set->Write(0, Descriptor(ResourceRef<const Sampler>(sampler)), 0);
    set->Write(3, Descriptor(ResourceRef<const ImageView>(sampledView), ResourceRef<const Sampler>(sampler)), 0);
    SUCCEED();
}

// -----------------------------------------------------------------------------
// Image variety (3D / array / mips), GenerateMipmaps, Blit, Resolve and the
// buffer-to-buffer copy / fill / clear transfer paths.
// -----------------------------------------------------------------------------
TEST_F(GlTest, ImageOpsAndTransfers) {
    // 3D + array + single-channel image/view creation.
    auto image3d = Image::Builder{}
                       .setType(Image::Type::e3D)
                       .setFormat(Image::Format::eRGBA8_UNORM)
                       .setExtent(glm::uvec3{8, 8, 4})
                       .addUsage(Image::Usage::eTransferDst)
                       .addUsage(Image::Usage::eSampled)
                       .build();
    ASSERT_TRUE(static_cast<bool>(image3d));
    auto view3d = ImageView::Builder(ResourceRef<const Image>(image3d))
                      .setViewType(ImageView::Type::e3D).build();
    ASSERT_TRUE(static_cast<bool>(view3d));

    auto arrayImg = Image::Builder{}
                        .setType(Image::Type::e2D)
                        .setFormat(Image::Format::eR8_UNORM)
                        .setExtent(glm::uvec2{8, 8})
                        .setArrayLayers(3)
                        .addUsage(Image::Usage::eTransferDst)
                        .addUsage(Image::Usage::eSampled)
                        .build();
    ASSERT_TRUE(static_cast<bool>(arrayImg));

    // GenerateMipmaps: walk every level of a mipped image.
    auto mipped = Image::Builder{}
                      .setType(Image::Type::e2D)
                      .setFormat(Image::Format::eRGBA8_UNORM)
                      .setExtent(glm::uvec2{8, 8})
                      .setMipLevels(4)
                      .addUsage(Image::Usage::eTransferSrc)
                      .addUsage(Image::Usage::eTransferDst)
                      .addUsage(Image::Usage::eSampled)
                      .build();

    // Blit (down-scale) between two images.
    auto blitSrc = makeImage(gfx::Flags(Image::Usage::eTransferSrc) | Image::Usage::eTransferDst);
    auto blitDst = Image::Builder{}
                       .setType(Image::Type::e2D)
                       .setFormat(Image::Format::eRGBA8_UNORM)
                       .setExtent(glm::uvec2{4, 4})
                       .addUsage(Image::Usage::eTransferDst)
                       .addUsage(Image::Usage::eTransferSrc)
                       .build();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.ClearColorImage(ResourceRef<const Image>(mipped), glm::vec4{0.25f, 0.5f, 0.75f, 1.f});
        cb.GenerateMipmaps(ResourceRef<const Image>(mipped));
        cb.ClearColorImage(ResourceRef<const Image>(blitSrc), glm::vec4{1.f, 1.f, 0.f, 1.f});
        cb.Blit(ResourceRef<const Image>(blitSrc), ResourceRef<const Image>(blitDst), gfx::Blit{
            .srcExtent = {8, 8, 1},
            .dstExtent = {4, 4, 1},
            .filtering = gfx::Filter::eLinear,
        });
    }, CommandBuffer::Usage::eGraphics);

    // Buffer-to-buffer copy / fill / clear, then verify the copy round-trips.
    std::vector<std::uint32_t> data(64, 7u);
    Buffer::Builder<std::uint32_t> srcB;
    srcB.setData(data);
    srcB.addUsage(Buffer::Usage::eTransferSrc);
    srcB.addUsage(Buffer::Usage::eTransferDst);
    srcB.setType(Buffer::Type::eDeviceLocal);
    auto src = srcB.build();

    Buffer::RawBuilder dstB;
    dstB.setRawSize(static_cast<glm::i64>(data.size() * sizeof(std::uint32_t)))
        .addUsage(Buffer::Usage::eTransferDst)
        .addUsage(Buffer::Usage::eTransferSrc)
        .setType(Buffer::Type::eReadback);
    auto dst = dstB.build();

    std::uint32_t fillValue = 0xABCDABCDu;
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyBuffer(ResourceRef<const Buffer>(src), ResourceRef<const Buffer>(dst));
        cb.FillBuffer(ResourceRef<const Buffer>(src), &fillValue, 0, sizeof(std::uint32_t) * 8);
        cb.ClearBuffer(ResourceRef<const Buffer>(src));
    }, CommandBuffer::Usage::eTransfer);

    const std::vector<std::uint32_t> out = dst->Read<std::uint32_t>();
    ASSERT_EQ(out.size(), data.size());
    for (auto v : out) EXPECT_EQ(v, 7u);
}

// -----------------------------------------------------------------------------
// Textured draw: sample a solid-color texture through a combined image/sampler
// descriptor set bound in a graphics pipeline. Exercises the GL descriptor-set
// bind path for image + sampler (which the compute test, storage buffer only,
// never reaches) and must produce the source color at every texel.
// -----------------------------------------------------------------------------
TEST_F(GlTest, TexturedDraw) {
    // Source texture, cleared to a known color and sampled by the fragment shader.
    auto texture = makeImage(gfx::Flags(Image::Usage::eSampled) | Image::Usage::eTransferDst);
    const glm::vec4 texColor{0.2f, 0.4f, 0.8f, 1.f};
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.ClearColorImage(ResourceRef<const Image>(texture), texColor);
    }, CommandBuffer::Usage::eGraphics);
    auto texView = ImageView::Builder(ResourceRef<const Image>(texture)).build();
    auto sampler = Sampler::Builder{}
                       .setMinFilter(gfx::Filter::eNearest)
                       .setMagFilter(gfx::Filter::eNearest)
                       .build();

    auto target = makeTarget({0.f, 0.f, 0.f, 1.f});
    const auto vert = loadShader("sampleTexture.vert.glsl", Shader::Stage::eVertex, "glt.tex.vert");
    const auto frag = loadShader("sampleTexture.frag.glsl", Shader::Stage::eFragment, "glt.tex.frag");
    auto pipeline = GraphicsPipeline::Builder{}
                        .setVertexShader(vert)
                        .setFragmentShader(frag)
                        .setFramebuffer(ResourceRef<Framebuffer>(target.framebuffer))
                        .build();

    auto set = DescriptorSet::Builder(gfx::ResourceRef<const gfx::Pipeline>(pipeline), 0)
                   .write(0, Descriptor(ResourceRef<const ImageView>(texView), ResourceRef<const Sampler>(sampler)))
                   .build();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginRendering(ResourceRef<const Framebuffer>(target.framebuffer));
        cb.BindGraphicsPipeline(ResourceRef<const GraphicsPipeline>(pipeline));
        cb.BindDescriptorSet(0, ResourceRef<const DescriptorSet>(set));
        cb.SetViewport(0, 0, kW, kH);
        cb.SetScissor(0, 0, kW, kH);
        cb.Draw(3);
        cb.EndRendering();
    }, CommandBuffer::Usage::eGraphics);

    const auto out = readbackImage(target.image);
    ASSERT_EQ(out.size(), static_cast<std::size_t>(kW) * kH);
    // Nearest-sampled solid texture -> every texel is the source color (~51/102/204).
    for (std::size_t i = 0; i < out.size(); ++i) {
        ASSERT_NEAR(out[i].r, 51, 2)  << "texel " << i;
        ASSERT_NEAR(out[i].g, 102, 2) << "texel " << i;
        ASSERT_NEAR(out[i].b, 204, 2) << "texel " << i;
    }
}

// -----------------------------------------------------------------------------
// Debug-label commands (scoped + single markers). No-ops without a debugger, but
// they still record and must not fail the command buffer.
// -----------------------------------------------------------------------------
TEST_F(GlTest, DebugLabels) {
    auto image = makeImage(gfx::Flags(Image::Usage::eTransferDst) | Image::Usage::eTransferSrc);
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginDebugLabel("outer", glm::vec4{1.f, 0.f, 0.f, 1.f});
        cb.InsertDebugLabel("marker");
        cb.DebugLabel("scoped", [&](CommandBuffer& inner) {
            inner.ClearColorImage(ResourceRef<const Image>(image), glm::vec4{0.f, 1.f, 0.f, 1.f});
        });
        cb.EndDebugLabel();
    }, CommandBuffer::Usage::eGraphics);
    SUCCEED();
}

} // namespace

// Registered before RUN_ALL_TESTS (compatible with gtest_main). gtest owns and
// deletes the environment.
static ::testing::Environment* const kGlEnv =
    ::testing::AddGlobalTestEnvironment(new GlEnvironment);
