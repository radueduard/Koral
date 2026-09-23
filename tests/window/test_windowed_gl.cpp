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

#include <array>
#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <numeric>
#include <span>
#include <vector>

#include <GLFW/glfw3.h>
#include <imgui.h>

#include "buffer.h"
#include "commandBuffer.h"
#include "computePipeline.h"
#include "context.h"
#include "descriptor.h"
#include "bufferView.h"
#include "descriptorSet.h"
#include "descriptorSetLayout.h"
#include "framebuffer.h"
#include "graphicsPipeline.h"
#include "gui.h"
#include "image.h"
#include "imageView.h"
#include "mesh.h"
#include <koralMesh.h>
#include "resource.h"
#include "sampler.h"
#include "scene.h"
#include "scheduler.h"
#include "shader.h"
#include "window.h"

#include "orientation_shared.h"

using kor::Buffer;
using kor::CommandBuffer;
using kor::ComputePipeline;
using kor::Descriptor;
using kor::DescriptorSet;
using kor::DescriptorSetLayout;
using kor::DescriptorType;
using kor::Framebuffer;
using kor::GraphicsPipeline;
using kor::Image;
using kor::ImageView;
using kor::ResourceRef;
using kor::Sampler;
using kor::Shader;

namespace {

using Pixel = glm::u8vec4;

constexpr std::uint32_t kW = 16;
constexpr std::uint32_t kH = 16;

// Minimal scene: clears the default framebuffer and draws an ImGui overlay with a
// GuiImage, driving the GL presentation + ImGui + GuiImage blit paths per frame.
class GlOverlayScene : public kor::Scene {
public:
    void Initialize() override {
        _image = Image::Builder{}
                     .setType(Image::Type::e2D)
                     .setFormat(Image::Format::eRGBA8_UNORM)
                     .setExtent(glm::uvec2{16, 16})
                     .setUsage(Image::Usage::eTransferSrc | Image::Usage::eTransferDst | Image::Usage::eSampled)
                     .build();
        CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
            cb.ClearColorImage(_image, glm::vec4{0.3f, 0.6f, 0.9f, 1.f});
        }, CommandBuffer::Usage::eGraphics);
        _guiImage = kor::GuiImage::Create(_image);
    }

    void Update() override { ++updates; }

    void Render(CommandBuffer& cb) override {
        cb.BeginRendering(); // default framebuffer: clear
        cb.EndRendering();
    }

    void RenderUI() override {
        ImGui::Begin("Koral GL test overlay");
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
    kor::Resource<kor::Image> _image;
    kor::Resource<kor::GuiImage> _guiImage;
};

void drawFrame(kor::Scene& scene) {
    glfwPollEvents();
    kor::Context::DrainMainThread();
    kor::Context::Scheduler().Draw([&](CommandBuffer& cb) {
        kor::Context::Repository().update();
        scene.Update();
        scene.Render(cb);
        kor::GUI::Render(cb, scene);
    });
    kor::GUI::RenderPlatformWindows();   // after the frame, as the runtime does
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
            s_window = kor::Window::Builder(std::move(scenePtr))
                           .setTitle("Koral GL windowed test")
                           .setExtent({320, 240})
                           .setResizable(true)
                           .setVSync(false)
                           .setAPI(kor::API::eOpenGL)
                           .build();
        } catch (const std::exception& e) {
            s_reason = e.what();
            s_window.reset();
            s_scene = nullptr;
        }
    }

    void TearDown() override {
        if (s_window) {
            kor::Context::DrainMainThread();
            s_window.reset();
        }
        s_scene = nullptr;
    }

    static bool ready() { return s_window != nullptr; }
    static const std::string& reason() { return s_reason; }
    static kor::Window& window() { return *s_window; }
    static GlOverlayScene& scene() { return *s_scene; }

private:
    static std::unique_ptr<kor::Window> s_window;
    static GlOverlayScene* s_scene;
    static std::string s_reason;
};

std::unique_ptr<kor::Window> GlEnvironment::s_window;
GlOverlayScene* GlEnvironment::s_scene = nullptr;
std::string GlEnvironment::s_reason = "no display";

class GlTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!GlEnvironment::ready()) {
            GTEST_SKIP() << "windowed OpenGL context unavailable: " << GlEnvironment::reason();
        }
        EXPECT_EQ(kor::Context::activeAPI(), kor::API::eOpenGL);
    }
};

// ---- shared offscreen helpers ------------------------------------------------

struct OffscreenTarget {
    kor::Resource<Image> image;
    kor::Resource<ImageView> view;
    kor::Resource<Framebuffer> framebuffer;
};

OffscreenTarget makeTarget(const glm::vec4 clearColor) {
    OffscreenTarget t;
    t.image = Image::Builder{}
                  .setType(Image::Type::e2D)
                  .setFormat(Image::Format::eRGBA8_UNORM)
                  .setExtent(glm::uvec2{kW, kH})
                  .setUsage(Image::Usage::eColorAttachment | Image::Usage::eTransferSrc)
                  .build();
    t.view = ImageView::Builder(t.image).build();
    t.framebuffer = Framebuffer::Builder{}
                        .addColor({ .view = t.view, .clear = clearColor })
                        .build();
    return t;
}

ResourceRef<const Shader> loadShader(const char* file, const Shader::Stage stage, const char* key) {
    return Shader::Builder{}
        .setLang<Shader::Lang::eGLSL>()
        .setStage(stage)
        .setPath(kor::shaderPath(file))
        .getOrBuild(key);
}

std::vector<Pixel> readbackImage(const kor::Resource<Image>& image) {
    Buffer::RawBuilder rb;
    rb.setRawSize(static_cast<glm::i64>(kW) * kH * sizeof(Pixel))
      .setUsage(Buffer::Usage::eTransferDst)
      .setType(Buffer::Type::eReadback);
    auto readback = rb.build();
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(image, readback);
    }, CommandBuffer::Usage::eTransfer);
    return readback->Read<Pixel>();
}

kor::Resource<Image> makeImage(kor::Flags<Image::Usage> usage,
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
                            .setFramebuffer(target.framebuffer)
                            .build();

        CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
            cb.BeginRendering(target.framebuffer);
            cb.BindGraphicsPipeline(pipeline);
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
                            .setFramebuffer(target.framebuffer)
                            .build();

        const glm::vec4 pushed{1.f, 0.f, 1.f, 1.f}; // magenta
        CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
            cb.BeginRendering(target.framebuffer);
            cb.BindGraphicsPipeline(pipeline);
            cb.PushConstantBlock(pushed);
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

        kor::ColorBlendState blend;
        blend.attachments.push_back(kor::ColorBlendState::AttachmentState{
            .blendEnable = true,
            .srcColorBlendFactor = kor::BlendFactor::eSrcAlpha,
            .dstColorBlendFactor = kor::BlendFactor::eOneMinusSrcAlpha,
        });
        auto pipeline = GraphicsPipeline::Builder{}
                            .setVertexShader(vert)
                            .setFragmentShader(frag)
                            .setFramebuffer(target.framebuffer)
                            .setColorBlendState(blend)
                            .build();

        const glm::vec4 halfGreen{0.f, 1.f, 0.f, 0.5f};
        CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
            cb.BeginRendering(target.framebuffer);
            cb.BindGraphicsPipeline(pipeline);
            cb.PushConstantBlock(halfGreen);
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
                            .setFramebuffer(target.framebuffer)
                            .build();

        CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
            cb.BeginRendering(target.framebuffer);
            cb.BindGraphicsPipeline(pipeline);
            cb.SetViewport(0, 0, kW, kH);
            cb.SetScissor(kSx, kSy, kSw, kSh);
            // dynamic-state overrides must record and not disturb the draw
            cb.SetFrontFace(kor::FrontFace::eCounterClockwise);
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

        auto target = makeTarget({0.f, 0.f, 0.f, 1.f});
        const auto vert = loadShader("meshTriangle.vert.glsl", Shader::Stage::eVertex, "glt.mesh.vert");
        const auto frag = loadShader("flatTriangle.frag.glsl", Shader::Stage::eFragment, "glt.flat.frag");
        auto pipeline = GraphicsPipeline::Builder{}
                            .setVertexShader(vert, PosMesh::Layout())
                            .setFragmentShader(frag)
                            .setFramebuffer(target.framebuffer)
                            .build();

        CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
            cb.BeginRendering(target.framebuffer);
            cb.BindGraphicsPipeline(pipeline);
            cb.SetViewport(0, 0, kW, kH);
            cb.SetScissor(0, 0, kW, kH);
            cb.BindMesh(mesh);
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
    bufBuilder.setUsage(Buffer::Usage::eStorage | Buffer::Usage::eTransferSrc | Buffer::Usage::eTransferDst);
    bufBuilder.setType(Buffer::Type::eDeviceLocal);
    auto buffer = bufBuilder.build();

    const auto shader = loadShader("doubleValues.comp.glsl", Shader::Stage::eCompute, "glt.doubleValues");
    auto pipeline = ComputePipeline::Builder{}.setComputeShader(shader).build();

    auto descriptorSet = DescriptorSet::Builder(pipeline, 0)
                             .write(0, buffer)
                             .build();

    const ResourceRef<const Buffer> bufRef(buffer);
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BindComputePipeline(pipeline);
        cb.BindDescriptorSet(0, descriptorSet);
        cb.BufferBarrier(kor::BufferBarrier(bufRef, kor::ResourceAccess::eComputeReadWrite));
        cb.Dispatch(kCount / kLocalSize, 1, 1);
        cb.BufferBarrier(kor::BufferBarrier(bufRef, kor::ResourceAccess::eTransferSrc));
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
                      .setMinFilter(kor::Filter::eLinear)
                      .setMagFilter(kor::Filter::eLinear)
                      .setMipmapMode(Sampler::MipmapMode::eLinear)
                      .setAddressModeU(Sampler::AddressMode::eRepeat)
                      .setAddressModeV(Sampler::AddressMode::eMirroredRepeat)
                      .setAddressModeW(Sampler::AddressMode::eClampToEdge)
                      .setMaxLod(4.f)
                      .build();
    ASSERT_TRUE(static_cast<bool>(linear));

    auto nearest = Sampler::Builder{}
                       .setMinFilter(kor::Filter::eNearest)
                       .setMagFilter(kor::Filter::eNearest)
                       .setMipmapMode(Sampler::MipmapMode::eNearest)
                       .setAddressModeU(Sampler::AddressMode::eClampToBorder)
                       .build();
    ASSERT_TRUE(static_cast<bool>(nearest));

    auto sampledImg = makeImage(kor::Flags(Image::Usage::eSampled) | Image::Usage::eTransferDst);
    auto storageImg = makeImage(kor::Flags(Image::Usage::eStorage) | Image::Usage::eTransferDst);
    auto sampledView = ImageView::Builder(sampledImg).build();
    auto storageView = ImageView::Builder(storageImg).build();
    auto sampler = Sampler::Builder{}.build();

    Buffer::RawBuilder ub;
    ub.setRawSize(256).setUsage(Buffer::Usage::eUniform).setType(Buffer::Type::eDynamic);
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
                   .write(0, sampler)
                   .write(1, sampledView)
                   .write(2, storageView)
                   .write(3, sampledView, sampler)
                   .write(4, uniform)
                   .build();
    ASSERT_TRUE(static_cast<bool>(set));

    // Runtime re-write path (separate from the build-time writes above).
    set->rebind(0, sampler, 0);
    set->rebind(3, sampledView, sampler, 0);
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
                       .setUsage(Image::Usage::eTransferDst | Image::Usage::eSampled)
                       .build();
    ASSERT_TRUE(static_cast<bool>(image3d));
    auto view3d = ImageView::Builder(image3d)
                      .setViewType(ImageView::Type::e3D).build();
    ASSERT_TRUE(static_cast<bool>(view3d));

    auto arrayImg = Image::Builder{}
                        .setType(Image::Type::e2D)
                        .setFormat(Image::Format::eR8_UNORM)
                        .setExtent(glm::uvec2{8, 8})
                        .setArrayLayers(3)
                        .setUsage(Image::Usage::eTransferDst | Image::Usage::eSampled)
                        .build();
    ASSERT_TRUE(static_cast<bool>(arrayImg));

    // GenerateMipmaps: walk every level of a mipped image.
    auto mipped = Image::Builder{}
                      .setType(Image::Type::e2D)
                      .setFormat(Image::Format::eRGBA8_UNORM)
                      .setExtent(glm::uvec2{8, 8})
                      .setMipLevels(4)
                      .setUsage(Image::Usage::eTransferSrc | Image::Usage::eTransferDst | Image::Usage::eSampled)
                      .build();

    // Blit (down-scale) between two images.
    auto blitSrc = makeImage(kor::Flags(Image::Usage::eTransferSrc) | Image::Usage::eTransferDst);
    auto blitDst = Image::Builder{}
                       .setType(Image::Type::e2D)
                       .setFormat(Image::Format::eRGBA8_UNORM)
                       .setExtent(glm::uvec2{4, 4})
                       .setUsage(Image::Usage::eTransferDst | Image::Usage::eTransferSrc)
                       .build();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.ClearColorImage(mipped, glm::vec4{0.25f, 0.5f, 0.75f, 1.f});
        cb.GenerateMipmaps(mipped);
        cb.ClearColorImage(blitSrc, glm::vec4{1.f, 1.f, 0.f, 1.f});
        cb.Blit(blitSrc, blitDst, kor::Blit{
            .srcExtent = {8, 8, 1},
            .dstExtent = {4, 4, 1},
            .filtering = kor::Filter::eLinear,
        });
    }, CommandBuffer::Usage::eGraphics);

    // Buffer-to-buffer copy / fill / clear, then verify the copy round-trips.
    std::vector<std::uint32_t> data(64, 7u);
    Buffer::Builder<std::uint32_t> srcB;
    srcB.setData(data);
    srcB.setUsage(Buffer::Usage::eTransferSrc | Buffer::Usage::eTransferDst);
    srcB.setType(Buffer::Type::eDeviceLocal);
    auto src = srcB.build();

    Buffer::RawBuilder dstB;
    dstB.setRawSize(static_cast<glm::i64>(data.size() * sizeof(std::uint32_t)))
        .setUsage(Buffer::Usage::eTransferDst | Buffer::Usage::eTransferSrc)
        .setType(Buffer::Type::eReadback);
    auto dst = dstB.build();

    // Eight of them: FillBuffer copies the bytes it is given rather than replicating a value, so
    // asking for 8 * sizeof(uint32_t) from a single uint32_t read past the end of it.
    const std::array<std::uint32_t, 8> fillValues { 0xABCDABCDu, 0xABCDABCDu, 0xABCDABCDu, 0xABCDABCDu,
                                                    0xABCDABCDu, 0xABCDABCDu, 0xABCDABCDu, 0xABCDABCDu };
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyBuffer(src, dst);
        cb.FillBuffer(src, fillValues);
        cb.ClearBuffer(src);
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
    auto texture = makeImage(kor::Flags(Image::Usage::eSampled) | Image::Usage::eTransferDst);
    const glm::vec4 texColor{0.2f, 0.4f, 0.8f, 1.f};
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.ClearColorImage(texture, texColor);
    }, CommandBuffer::Usage::eGraphics);
    auto texView = ImageView::Builder(texture).build();
    auto sampler = Sampler::Builder{}
                       .setMinFilter(kor::Filter::eNearest)
                       .setMagFilter(kor::Filter::eNearest)
                       .build();

    auto target = makeTarget({0.f, 0.f, 0.f, 1.f});
    const auto vert = loadShader("sampleTexture.vert.glsl", Shader::Stage::eVertex, "glt.tex.vert");
    const auto frag = loadShader("sampleTexture.frag.glsl", Shader::Stage::eFragment, "glt.tex.frag");
    auto pipeline = GraphicsPipeline::Builder{}
                        .setVertexShader(vert)
                        .setFragmentShader(frag)
                        .setFramebuffer(target.framebuffer)
                        .build();

    auto set = DescriptorSet::Builder(pipeline, 0)
                   .write(0, texView, sampler)
                   .build();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginRendering(target.framebuffer);
        cb.BindGraphicsPipeline(pipeline);
        cb.BindDescriptorSet(0, set);
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
// Push constants addressed by name, across two stages, through GL's emulation.
//
// GL has no push constants: the pipeline owns a std140 UBO and a write lands at the same byte
// offset Vulkan would have used. So the name lookup has to produce the same offsets here, and a
// constant only the vertex stage reads has to survive being written alongside one only the
// fragment stage reads.
// -----------------------------------------------------------------------------
TEST_F(GlTest, PushConstantsByNameAcrossStages) {
    auto target = makeTarget({0.f, 0.f, 0.f, 1.f});
    const auto vert = loadShader("pushMultiStage.vert.glsl", Shader::Stage::eVertex, "glt.pushmulti.vert");
    const auto frag = loadShader("pushMultiStage.frag.glsl", Shader::Stage::eFragment, "glt.pushmulti.frag");
    auto pipeline = GraphicsPipeline::Builder{}
                        .setVertexShader(vert)
                        .setFragmentShader(frag)
                        .setFramebuffer(target.framebuffer)
                        .build();
    ASSERT_TRUE(pipeline.valid()) << (pipeline.error() ? pipeline.error()->history() : "");

    const auto* offsetConstant = pipeline->findPushConstant("offset");
    const auto* colorConstant = pipeline->findPushConstant("color");
    ASSERT_NE(offsetConstant, nullptr);
    ASSERT_NE(colorConstant, nullptr);
    EXPECT_TRUE(offsetConstant->stages & Shader::Stage::eVertex);
    EXPECT_TRUE(colorConstant->stages & Shader::Stage::eFragment);

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginRendering(target.framebuffer);
        cb.BindGraphicsPipeline(pipeline);
        cb.SetViewport(0, 0, kW, kH);
        cb.SetScissor(0, 0, kW, kH);
        cb.PushConstant("offset", glm::vec2{0.f, 0.f});
        cb.PushConstant("color", glm::vec4{1.f, 0.f, 1.f, 1.f});   // magenta
        cb.Draw(3);
        cb.EndRendering();
    }, CommandBuffer::Usage::eGraphics);

    const auto out = readbackImage(target.image);
    ASSERT_EQ(out.size(), static_cast<std::size_t>(kW) * kH);
    for (std::size_t i = 0; i < out.size(); ++i) {
        ASSERT_EQ(out[i], Pixel(255, 0, 255, 255)) << "texel " << i;
    }

    // The vertex stage's half of the block, on its own: shifted clear off the target.
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginRendering(target.framebuffer);
        cb.BindGraphicsPipeline(pipeline);
        cb.SetViewport(0, 0, kW, kH);
        cb.SetScissor(0, 0, kW, kH);
        cb.PushConstant("offset", glm::vec2{2.f, 0.f});
        cb.PushConstant("color", glm::vec4{1.f, 0.f, 1.f, 1.f});
        cb.Draw(3);
        cb.EndRendering();
    }, CommandBuffer::Usage::eGraphics);

    EXPECT_EQ(readbackImage(target.image).front(), Pixel(0, 0, 0, 255))
        << "the vertex stage did not read its half of the block";
}

// -----------------------------------------------------------------------------
// The padded shapes — a mat3, an array of vec3 — through GL's push-constant UBO.
//
// GL has no push constants: the block is re-emitted as a uniform buffer. If that changed any
// stride, a value laid out from the reflected (SPIR-V) strides would land somewhere else here than
// it does on Vulkan, and this is the test that would say so.
// -----------------------------------------------------------------------------
TEST_F(GlTest, PushConstantsAreLaidOutIntoTheShadersPadding) {
    auto target = makeTarget({0.f, 0.f, 0.f, 1.f});
    const auto vert = loadShader("flatTriangle.vert.glsl", Shader::Stage::eVertex, "glt.aligned.vert");
    const auto frag = loadShader("pushAligned.frag.glsl", Shader::Stage::eFragment, "glt.aligned.frag");
    auto pipeline = GraphicsPipeline::Builder{}
                        .setVertexShader(vert)
                        .setFragmentShader(frag)
                        .setFramebuffer(target.framebuffer)
                        .build();
    ASSERT_TRUE(pipeline.valid()) << (pipeline.error() ? pipeline.error()->history() : "");

    const auto* basis = pipeline->findPushConstant("basis");
    const auto* tints = pipeline->findPushConstant("tints");
    ASSERT_NE(basis, nullptr);
    ASSERT_NE(tints, nullptr);
    EXPECT_EQ(basis->matrixStride, 16u);
    EXPECT_EQ(tints->arrayStride, 16u);

    glm::mat3 basisValue(0.f);
    basisValue[0] = glm::vec3{0.2f, 0.f, 0.f};
    basisValue[1] = glm::vec3{0.f, 0.4f, 0.f};
    basisValue[2] = glm::vec3{0.f, 0.f, 0.6f};
    const std::array tintsValue{ glm::vec3{0.2f, 0.f, 0.f}, glm::vec3{0.f, 0.2f, 0.f}, glm::vec3{0.f, 0.f, 0.2f} };

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginRendering(target.framebuffer);
        cb.BindGraphicsPipeline(pipeline);
        cb.SetViewport(0, 0, kW, kH);
        cb.SetScissor(0, 0, kW, kH);
        cb.PushConstant("basis", basisValue);
        cb.PushConstant("tints", tintsValue);
        cb.Draw(3);
        cb.EndRendering();
    }, CommandBuffer::Usage::eGraphics);

    // The same colour Vulkan produces from the same two calls, which is the whole point.
    const auto out = readbackImage(target.image);
    ASSERT_EQ(out.size(), static_cast<std::size_t>(kW) * kH);
    EXPECT_EQ(out.front(), Pixel(102, 153, 204, 255));
}

// -----------------------------------------------------------------------------
// Per-pass clear values, on the backend that can actually get them wrong.
//
// GL records a render pass as a lambda and replays it at Submit. A clear value
// read from the framebuffer at *replay* time would be the last one written — so
// two passes over one framebuffer would both come out green, while Vulkan, which
// bakes the value in as it records, would give red then green. RenderInfo resolves
// against the framebuffer while recording precisely so the two agree; this is the
// test that fails if that resolution is ever moved back into the backend.
// -----------------------------------------------------------------------------
TEST_F(GlTest, PerPassClearColorsSurviveDeferredReplay) {
    auto target = makeTarget({0.f, 0.f, 1.f, 1.f});   // built blue; neither pass asks for it

    Buffer::RawBuilder rb;
    rb.setRawSize(static_cast<glm::i64>(kW) * kH * sizeof(Pixel))
      .setUsage(Buffer::Usage::eTransferDst)
      .setType(Buffer::Type::eReadback);
    auto firstPass = rb.build();
    auto secondPass = rb.build();

    // Both passes in one command buffer, so both lambdas are queued before either runs — which is
    // the arrangement that catches a value read at replay time.
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginRendering(kor::RenderInfo(target.framebuffer)
                              .setClearColor(0, glm::vec4{1.f, 0.f, 0.f, 1.f}));
        cb.EndRendering();
        cb.CopyImageToBuffer(target.image, firstPass);

        cb.BeginRendering(kor::RenderInfo(target.framebuffer)
                              .setClearColor(0, glm::vec4{0.f, 1.f, 0.f, 1.f}));
        cb.EndRendering();
        cb.CopyImageToBuffer(target.image, secondPass);
    }, CommandBuffer::Usage::eGraphics);

    const auto first = firstPass->Read<Pixel>();
    const auto second = secondPass->Read<Pixel>();
    ASSERT_EQ(first.size(), static_cast<std::size_t>(kW) * kH);
    ASSERT_EQ(second.size(), static_cast<std::size_t>(kW) * kH);
    EXPECT_EQ(first.front(), Pixel(255, 0, 0, 255)) << "the first pass cleared to its own red";
    EXPECT_EQ(second.front(), Pixel(0, 255, 0, 255)) << "the second pass cleared to its own green";

    // And a pass that overrides nothing still gets the framebuffer's own value.
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginRendering(target.framebuffer);
        cb.EndRendering();
    }, CommandBuffer::Usage::eGraphics);
    EXPECT_EQ(readbackImage(target.image).front(), Pixel(0, 0, 255, 255));
}

// -----------------------------------------------------------------------------
// Debug-label commands (scoped + single markers). No-ops without a debugger, but
// they still record and must not fail the command buffer.
// -----------------------------------------------------------------------------
TEST_F(GlTest, DebugLabels) {
    auto image = makeImage(kor::Flags(Image::Usage::eTransferDst) | Image::Usage::eTransferSrc);
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BeginDebugLabel("outer", glm::vec4{1.f, 0.f, 0.f, 1.f});
        cb.InsertDebugLabel("marker");
        cb.DebugLabel("scoped", [&](CommandBuffer& inner) {
            inner.ClearColorImage(image, glm::vec4{0.f, 1.f, 0.f, 1.f});
        });
        cb.EndDebugLabel();
    }, CommandBuffer::Usage::eGraphics);
    SUCCEED();
}

// -----------------------------------------------------------------------------
// eDeviceDynamic on OpenGL. GL cannot request a memory placement, so the type
// degrades to eDynamic — but it must still behave: host-writable, visible to a
// dispatch, and readable back (slowly is fine, broken is not; a placement hint
// must never become a correctness difference between backends).
// -----------------------------------------------------------------------------
TEST_F(GlTest, DeviceDynamicBufferRoundTrips) {
    constexpr std::uint32_t kCount = 256;

    std::vector<std::uint32_t> source(kCount);
    std::iota(source.begin(), source.end(), 1u);

    Buffer::Builder<std::uint32_t> builder;
    builder.setData(source);
    builder.setUsage(Buffer::Usage::eStorage | Buffer::Usage::eTransferSrc);
    builder.setType(Buffer::Type::eDeviceDynamic);
    auto buffer = builder.build();
    ASSERT_TRUE(buffer.valid());
    EXPECT_TRUE(buffer->isHostVisible());

    const auto readBack = buffer->Read<std::uint32_t>();
    ASSERT_EQ(readBack.size(), source.size());
    EXPECT_TRUE(std::ranges::equal(readBack, source))
        << "an eDeviceDynamic buffer did not read back what was written to it";
}

// -----------------------------------------------------------------------------
// GPU timers, the GL half. Different machinery entirely from Vulkan's query pool:
// two glQueryCounter objects per scope, polled for GL_QUERY_RESULT_AVAILABLE so
// the recording thread never blocks on the GPU. Driven through the frame path,
// which is where the results are collected.
// -----------------------------------------------------------------------------
TEST_F(GlTest, GpuTimersMeasureFrameWork) {
    auto image = makeImage(kor::Flags(Image::Usage::eTransferDst) | Image::Usage::eTransferSrc);

    const int budget = static_cast<int>(kor::Context::Scheduler().imageCount()) + 8;
    bool found = false;
    double milliseconds = 0.0;

    for (int frame = 0; frame < budget && !found; ++frame) {
        kor::Context::Scheduler().Draw([&](CommandBuffer& cb) {
            cb.Timer("gl.clears", [&](CommandBuffer& inner) {
                for (int i = 0; i < 8; ++i)
                    inner.ClearColorImage(image, glm::vec4{0.f, 1.f, 0.f, 1.f});
            });
        });

        for (const auto& f : kor::Context::Scheduler().frames()) {
            for (const auto& timing : f.get().commandBuffer().timings()) {
                if (timing.label != "gl.clears") continue;
                found = true;
                milliseconds = timing.milliseconds;
            }
        }
    }

    ASSERT_TRUE(found) << "no frame reported its timer within " << budget << " frames";
    EXPECT_GT(milliseconds, 0.0);
    EXPECT_LT(milliseconds, 1000.0);
}

// -----------------------------------------------------------------------------
// Block-compressed upload and readback, the GL half of what the Vulkan suite
// checks in CompressedImageUploadRoundTrips. GL spells these formats after the
// extensions they came in and needs its own entry points for them
// (glCompressedTextureSubImage2D / glGetCompressedTextureSubImage), so the path
// is genuinely different code even though the engine API is the same. BC7 is
// GL_ARB_texture_compression_bptc — core since 4.2.
// -----------------------------------------------------------------------------
TEST_F(GlTest, CompressedImageUploadRoundTrips) {
    constexpr std::uint32_t kSize = 16;   // 4x4 blocks of BC7
    const auto format = Image::Format::eBC7_UNORM;
    const auto byteCount = Image::sizeOfRegion(format, { kSize, kSize, 1 });
    ASSERT_EQ(byteCount, 256u);

    std::vector<std::uint8_t> blocks(byteCount);
    for (std::size_t i = 0; i < blocks.size(); ++i)
        blocks[i] = static_cast<std::uint8_t>((i / Image::blockSize(format)) + 1);

    auto image = Image::Builder{}
        .setType(Image::Type::e2D)
        .setFormat(format)
        .setExtent(glm::uvec2{ kSize, kSize })
        .setUsage(Image::Usage::eTransferDst | Image::Usage::eTransferSrc | Image::Usage::eSampled)
        .build();
    ASSERT_TRUE(static_cast<bool>(image)) << (image.error() ? image.error()->message : "");

    const auto staging = Buffer::Builder<std::uint8_t>()
        .setDataView(std::span<const std::uint8_t>(blocks))
        .setUsage(Buffer::Usage::eTransferSrc)
        .setType(Buffer::Type::eStaging)
        .build();

    Buffer::RawBuilder rb;
    rb.setRawSize(static_cast<glm::i64>(byteCount))
      .setUsage(Buffer::Usage::eTransferDst)
      .setType(Buffer::Type::eReadback);
    auto readback = rb.build();

    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyBufferToImage(staging, image, kor::Copy{
            .imageOffset = { 0, 0, 0 },
            .imageExtent = { kSize, kSize, 1 },
        });
        cb.CopyImageToBuffer(image, readback, kor::Copy{
            .imageOffset = { 0, 0, 0 },
            .imageExtent = { kSize, kSize, 1 },
        });
    }, CommandBuffer::Usage::eTransfer);

    const auto out = readback->Read<std::uint8_t>();
    ASSERT_EQ(out.size(), blocks.size());
    EXPECT_EQ(out, blocks);
}

// -----------------------------------------------------------------------------
// Y-orientation parity (see orientation_shared.h): a top-half quad drawn by the
// rasterizer and the same pattern written by a compute imageStore must land in
// the same place, and both are blit to the screen. The compute case is the one
// that fails on GL without the imageStore Y-flip in the transpiler.
// -----------------------------------------------------------------------------
TEST_F(GlTest, RasterTriangleOrientationToScreen) {
    auto r = orient::rasterTopHalf();
    orient::expectHalfSplit(r.pixels);
    orient::blitToScreen(r.image);
}

TEST_F(GlTest, ComputeTriangleOrientationToScreen) {
    auto r = orient::computeTopHalf();
    orient::expectHalfSplit(r.pixels);
    orient::blitToScreen(r.image);
}


// -----------------------------------------------------------------------------
// Texel buffers on GL, which has no buffer-view object of its own: a
// kor::BufferView is a GL_TEXTURE_BUFFER texture whose storage is the buffer,
// and binding it is binding that texture to a unit. This is the whole of the GL
// half of BufferView, and the only thing that exercises it.
// -----------------------------------------------------------------------------
TEST_F(GlTest, TexelBufferFetch) {
    constexpr std::uint32_t kTexels = 64;

    std::vector<float> source(kTexels * 4, 0.f);
    for (std::uint32_t i = 0; i < kTexels; ++i) source[i * 4] = static_cast<float>(i);

    Buffer::Builder<float> sourceBuilder;
    sourceBuilder.setData(source)
                 .setUsage(Buffer::Usage::eTexel | Buffer::Usage::eTransferDst);
    auto sourceBuffer = sourceBuilder.build();
    ASSERT_TRUE(sourceBuffer.valid()) << (sourceBuffer.error() ? sourceBuffer.error()->history() : "");

    auto view = kor::BufferView::Builder(sourceBuffer)
        .setFormat(kor::Image::Format::eRGBA32_SFLOAT)
        .build();
    ASSERT_TRUE(view.valid()) << (view.error() ? view.error()->history() : "");

    Buffer::Builder<float> destBuilder;
    destBuilder.setData(std::vector<float>(kTexels, -1.f))
               .setUsage(Buffer::Usage::eStorage | Buffer::Usage::eTransferSrc | Buffer::Usage::eTransferDst);
    auto destination = destBuilder.build();
    ASSERT_TRUE(destination.valid());

    const auto shader = loadShader("texelBuffer.comp.glsl", Shader::Stage::eCompute, "glt.texelBuffer");
    auto pipeline = ComputePipeline::Builder{}.setComputeShader(shader).build();
    ASSERT_TRUE(pipeline.valid()) << (pipeline.error() ? pipeline.error()->history() : "");

    auto set = DescriptorSet::Builder(pipeline, 0)
        .write("source", view)
        .write("destination", destination)
        .build();
    ASSERT_TRUE(set.valid()) << (set.error() ? set.error()->history() : "");

    const ResourceRef<const Buffer> destRef(destination);
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.BindComputePipeline(pipeline);
        cb.BindDescriptorSet(0, set);
        cb.BufferBarrier(kor::BufferBarrier(destRef, kor::ResourceAccess::eComputeReadWrite));
        cb.Dispatch(kTexels / 64, 1, 1);
        cb.BufferBarrier(kor::BufferBarrier(destRef, kor::ResourceAccess::eTransferSrc));
    }, CommandBuffer::Usage::eCompute);

    const std::vector<float> output = destination->Read<float>();
    ASSERT_EQ(output.size(), static_cast<std::size_t>(kTexels));
    for (std::uint32_t i = 0; i < kTexels; ++i) {
        EXPECT_FLOAT_EQ(output[i], static_cast<float>(i)) << "at texel " << i;
    }
}
} // namespace

// Registered before RUN_ALL_TESTS (compatible with gtest_main). gtest owns and
// deletes the environment.
static ::testing::Environment* const kGlEnv =
    ::testing::AddGlobalTestEnvironment(new GlEnvironment);
