// Windowed Vulkan integration tests: boot a real window + surface + swap chain +
// scheduler + ImGui, render frames (with an ImGui overlay and a GUI_Image),
// resize, and — the parity part — verify that a rasterized triangle and the same
// pattern written by a compute imageStore both present the same way (the Vulkan
// side of the orientation parity checked identically on GL in test_windowed_gl).
//
// The window/context is created once for the whole binary by VkEnvironment and
// shared by every test through the VkWindowTest fixture (mirroring the OpenGL
// windowed suite and the headless GpuTest). A real display (X11/Wayland) and a
// WSI-capable Vulkan loader must be available; tests skip gracefully otherwise.
// One windowed Vulkan context per process — a second corrupts the heap (GLFW +
// the Vulkan/ImGui statics don't survive re-initialization) — so everything runs
// against the single shared window.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <iostream>
#include <memory>
#include <string_view>

#include <GLFW/glfw3.h>
#include <imgui.h>

#include "commandBuffer.h"
#include "computePipeline.h"
#include "descriptor.h"
#include "descriptorSet.h"
#include "semantics.h"
#include "shader.h"
#include "error.h"
#include "framebuffer.h"
#include "input.h"
#include "imageView.h"
#include "buffer.h"
#include "log.h"
#include "context.h"
#include "gui.h"
#include "image.h"
#include "resource.h"
#include "scene.h"
#include "scheduler.h"
#include "window.h"

#include "orientation_shared.h"

// The GUI extras module, drawn from a real scene's RenderUI. imgui.h is already included above, which
// ImGuizmo.h requires of whoever includes it.
#include <koralGuiExtras.h>

namespace {

// A scene that draws a cleared default framebuffer plus an ImGui overlay and a
// GUI_Image, so a single frame drives the scheduler, the swap chain and the whole
// ImGui-on-Vulkan path (GUI_Image blit helper, textured widget, font access).
class OverlayScene : public kor::Scene {
public:
    void Initialize() override {
        _image = kor::Image::Builder{}
                     .setType(kor::Image::Type::e2D)
                     .setFormat(kor::Image::Format::eRGBA8_UNORM)
                     .setExtent(glm::uvec2{16, 16})
                     .addUsage(kor::Image::Usage::eTransferSrc)
                     .addUsage(kor::Image::Usage::eTransferDst)
                     .addUsage(kor::Image::Usage::eSampled)
                     .build();
        kor::CommandBuffer::SingleTimeCommand([&](kor::CommandBuffer& cb) {
            cb.ClearColorImage(kor::ResourceRef<const kor::Image>(_image), glm::vec4{0.3f, 0.6f, 0.9f, 1.f});
        }, kor::CommandBuffer::Usage::eGraphics);
        _guiImage = kor::GUI_Image::Create(kor::ResourceRef<const kor::Image>(_image));

        viewportTarget = kor::Image::Builder{}
            .setType(kor::Image::Type::e2D)
            .setFormat(kor::Image::Format::eRGBA8_UNORM)
            .setExtent(glm::uvec2{64, 64})
            .setIsPerFrame(true)            // one copy per frame in flight, like a real render target
            .addUsage(kor::Image::Usage::eColorAttachment)
            .addUsage(kor::Image::Usage::eSampled)
            .build();
        sampledOnlyTarget = kor::Image::Builder{}
            .setType(kor::Image::Type::e2D)
            .setFormat(kor::Image::Format::eRGBA8_UNORM)
            .setExtent(glm::uvec2{32, 32})
            .setIsPerFrame(true)
            .addUsage(kor::Image::Usage::eSampled)
            .build();

        // Once, here — deliberately not every frame in RenderUI like the two below. Resizing a target
        // replaces the image, and the viewport is supposed to notice that by itself; a scene that
        // handed it back every frame would hide a viewport that could not. @see kgui::Viewport::setImage
        directViewport.setImage(kor::ResourceRef<const kor::Image>(viewportTarget));

        viewportTargetView = kor::ImageView::Builder(kor::ResourceRef<const kor::Image>(viewportTarget)).build();
        viewportFramebuffer = kor::Framebuffer::Builder{}
            .addColorAttachment(kor::ResourceRef<const kor::ImageView>(viewportTargetView),
                                glm::vec4{0.2f, 0.f, 0.4f, 1.f})
            .build();
    }

    void Update() override {
        ++updates;

        // A window being dragged asks for a new size every frame, so the target is replaced every
        // frame — and every replacement is a fresh image in an undefined layout that the interface
        // samples in the same frame. This is the shape of that.
        if (resizeTargetEveryFrame) {
            const glm::uvec2 next{ 64 + (updates % 17), 48 + (updates % 13) };
            viewportFramebuffer->Resize(next);
        }
    }

    void Render(kor::CommandBuffer& cb) override {
        // Into the viewport's own target first, so the interface has something to show — and so the
        // target is rendered into at whatever size it now is.
        cb.BeginRendering(kor::ResourceRef<kor::Framebuffer>(viewportFramebuffer));
        cb.EndRendering();

        cb.BeginRendering();   // default framebuffer: clears the swap-chain image
        cb.EndRendering();
    }

    void RenderUI() override {
        ImGui::Begin("Koral test overlay");
        ImGui::Text("frame %d", updates);
        ImGui::SliderFloat("slider", &_slider, 0.f, 1.f);
        if (_guiImage) {
            ImGui::Image(**_guiImage, ImVec2(64, 64));
        }
        ImGui::End();

        // The GUI extras, drawn in a real ImGui frame with a real backend behind it — which is the one
        // thing the headless tests cannot cover, since a texture handle is a backend object.
        ImGuizmo::BeginFrame();
        if (drawViewports) {
            if (floatViewportOutsideMainWindow) {
                // Far to the left of the main window, so ImGui has to give it its own OS window.
                ImGui::SetNextWindowPos(ImVec2(-600.f, 100.f), ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(320.f, 240.f), ImGuiCond_Always);
            }
            directViewport.Draw("Direct");   // image set once, at Initialize

            sampledOnlyViewport.setImage(kor::ResourceRef<const kor::Image>(sampledOnlyTarget));
            sampledOnlyViewport.Draw("SampledOnly");
        }

        viewport.setImage(kor::ResourceRef<const kor::Image>(_image));
        if (viewport.Draw("Scene")) {
            ImGui::Begin("Scene");
            gizmo.Manipulate(viewport, glm::mat4(1.f), glm::mat4(1.f), transform);
            ImGui::End();
        }
        if (drawLogPanel) log.Draw();
        if (drawStatsPanel) stats.Draw();
    }

    void OnResize(glm::uvec2 extent) override { lastResize = extent; }

    int updates = 0;
    glm::uvec2 lastResize{0, 0};

    kgui::Viewport viewport;
    // A colour target as a scene would actually make one: sampled and rendered into, with no transfer
    // usage at all. It is the case that used to fail — the GUI's handle copied from every image, so an
    // image without eTransferSrc could not be shown, and a per-frame one was only ever valid for the
    // swap-chain image that happened to be current when the handle was made.
    kor::Resource<kor::Image> viewportTarget;
    kgui::Viewport directViewport;
    /// When set, the target is resized every frame, as it is while a window edge is being dragged.
    bool resizeTargetEveryFrame = false;
    /// When set, the viewport panel is placed outside the main window, which is what makes ImGui give
    /// it a platform window of its own — the same state as undocking it by hand.
    bool floatViewportOutsideMainWindow = false;
    /// Which of the GUI extras to draw, so their cost can be measured against a bare frame.
    bool drawLogPanel = true;
    bool drawStatsPanel = true;
    bool drawViewports = true;

    /// A per-frame image the interface samples and *nothing ever writes*. Its access is therefore the
    /// same on every frame, which is the case a shared (frame-blind) layout tracker gets wrong: the
    /// resolver sees "already shader-read" and skips the barrier, while this frame's copy has never
    /// been transitioned at all.
    kor::Resource<kor::Image> sampledOnlyTarget;
    kgui::Viewport sampledOnlyViewport;
    kor::Resource<kor::Framebuffer> viewportFramebuffer;
    kor::Resource<kor::ImageView> viewportTargetView;
    kgui::Gizmo gizmo;
    kgui::LogPanel log;
    kgui::StatsPanel stats;
    glm::mat4 transform{1.f};

private:
    float _slider = 0.5f;
    kor::Resource<kor::Image> _image;
    kor::Resource<kor::GUI_Image> _guiImage;
};

// Drives one engine-style frame through the scheduler.
void drawFrame(kor::Scene& scene) {
    glfwPollEvents();
    kor::Context::DrainMainThread();
    kor::Context::Scheduler().Draw([&](kor::CommandBuffer& cb) {
        kor::Context::Repository().update();
        scene.Update();
        scene.Render(cb);
        kor::GUI::Render(cb, scene);
    });
    // Exactly where the runtime calls it: after the frame is submitted, never inside the recording.
    // @see kor::GUI::RenderPlatformWindows
    kor::GUI::RenderPlatformWindows();
}

// ---- shared Vulkan window, created once for the whole binary -----------------
//
// One windowed Vulkan context per process (see the file header). All tests share
// it through VkWindowTest; the environment skips silently when no display / WSI
// loader is available and every test then GTEST_SKIPs.

class VkEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        auto scenePtr = std::make_unique<OverlayScene>();
        s_scene = scenePtr.get();
        try {
            s_window = kor::Window::Builder(std::move(scenePtr))
                           .setTitle("Koral windowed test")
                           .setExtent({320, 240})
                           .setResizable(true)
                           .setVSync(false)
                           .setAPI(kor::API::eVulkan)
                           // X11 deliberately: ImGui's multi-viewport needs to place a window at an
                           // absolute screen position, which Wayland denies, so viewports — and with
                           // them everything about *undocked* panels — are off there. Testing them at
                           // all means asking for the platform that has them.
                           .setPlatform(kor::WindowPlatform::eX11)
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
            s_window.reset(); // WaitIdle + full teardown of the presentation stack
        }
        s_scene = nullptr;
    }

    static bool ready() { return s_window != nullptr; }
    static const std::string& reason() { return s_reason; }
    static kor::Window& window() { return *s_window; }
    static OverlayScene& scene() { return *s_scene; }

private:
    static std::unique_ptr<kor::Window> s_window;
    static OverlayScene* s_scene;
    static std::string s_reason;
};

std::unique_ptr<kor::Window> VkEnvironment::s_window;
OverlayScene* VkEnvironment::s_scene = nullptr;
std::string VkEnvironment::s_reason = "no display";

class VkWindowTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!VkEnvironment::ready()) {
            GTEST_SKIP() << "windowed Vulkan context unavailable: " << VkEnvironment::reason();
        }
        EXPECT_EQ(kor::Context::activeAPI(), kor::API::eVulkan);
    }
};

// Render frames with an ImGui overlay (acquire/record/submit/present + the whole
// ImGui-on-Vulkan path), then resize and keep drawing so the next Acquire/Present
// sees an out-of-date swap chain and recreates it (SwapChain::Resize + the
// default-framebuffer Resize path).
TEST_F(VkWindowTest, RenderResizeAndPresent) {
    auto& window = VkEnvironment::window();
    auto& scene = VkEnvironment::scene();

    // Touch the font accessor (pure gui.cpp coverage, harmless if null).
    (void)kor::GUI::GetFont(kor::Font::Regular);

    // Phase 1: render enough frames to cycle every in-flight frame slot twice.
    for (int i = 0; i < 8 && !window.shouldClose(); ++i) {
        drawFrame(scene);
        window.LateUpdate();
    }
    EXPECT_GE(scene.updates, 1);

    // Phase 2: resize. Ask the compositor for a new size and pump events so the
    // surface actually changes, then keep drawing to hit the out-of-date/recreate
    // path in the scheduler and swap chain.
    glfwSetWindowSize(*window, 480, 360);
    for (int i = 0; i < 20; ++i) glfwPollEvents();
    for (int i = 0; i < 12 && !window.shouldClose(); ++i) {
        drawFrame(scene);
        window.LateUpdate();
    }
}


// The windowed suite writes kor:: out in full elsewhere; the block below is dense enough with types
// that naming them once reads better.
using kor::Buffer;
using kor::CommandBuffer;
using kor::ComputePipeline;
using kor::Descriptor;
using kor::DescriptorSet;
using kor::ResourceRef;
using kor::Shader;

// ---------------------------------------------------------------------------------------------
// Editing a shader so that a *semantic block gains a field*, while it is running, and having the
// new field arrive.
//
// The hardest point of the hot-reload story. An edit that only changes code is easy: the shader
// recompiles and the pipeline reloads in place. An edit that reshapes a uniform block is not,
// because the block's shape decides two things nobody else knows — how big the buffer behind that
// binding has to be, and which semantic fills each field. Both were resolved when the descriptor
// set was built, and neither is fixed by rebuilding the pipeline.
//
// The chain: file → Shader::OnReload → Pipeline::Reload → the layout adopts the new block
// description in place and says so (Resource::markChanged) → the descriptor set, which is
// recoverable and rebuilds when an input changes, is replayed → SemanticBuffers::acquire is asked
// for the *new* shape → a new buffer, filled by the same object, lands at the binding.
//
// Here rather than in the headless suite for one reason worth knowing: a semantic block's buffer is
// per-frame, and a per-frame buffer needs a scheduler. Semantics need a frame loop.
//
// Driven through the real FileWatcher rather than a back door, because the timing between its
// thread and the frame is part of what is being claimed.
// ---------------------------------------------------------------------------------------------

/**
 * @brief Something that can answer for semantics, standing in for a camera.
 *
 * Deliberately not the camera module: what is being tested is the engine's side of the contract, and
 * a test object makes the values it hands out obvious.
 */
class Probe final : public kor::SemanticSerializer
{
public:
    glm::vec4 alpha { 1.f, 2.f, 3.f, 4.f };
    glm::vec4 beta  { 5.f, 6.f, 7.f, 8.f };
    glm::vec4 gamma { 9.f, 10.f, 11.f, 12.f };

    [[nodiscard]] std::string_view semanticNamespace() const override { return "probe"; }

    bool serialize(const std::string_view semantic, kor::SemanticSlot& slot) const override
    {
        if (semantic == "ALPHA") return slot.set(alpha);
        if (semantic == "BETA")  return slot.set(beta);
        if (semantic == "GAMMA") return slot.set(gamma);
        return false;   // anything else: this object does not answer for it
    }

    kor::SemanticBuffers& semanticBuffers() override { return _buffers; }

private:
    kor::SemanticBuffers _buffers;
};

// Two fields, and the compute shader that copies them into a storage buffer so the test can read
// what actually arrived on the device.
constexpr const char* kTwoFields = R"(#version 450
layout(local_size_x = 1) in;

layout(set = 0, binding = 0, std140) uniform Probe {
    #pragma probe(ALPHA)
    vec4 alpha;
    #pragma probe(BETA)
    vec4 beta;
} probe;

layout(set = 0, binding = 1, std430) buffer Out { vec4 values[]; };

void main() {
    values[0] = probe.alpha;
    values[1] = probe.beta;
}
)";

// The edit: a third field, annotated with a semantic the same object already answers for.
constexpr const char* kThreeFields = R"(#version 450
layout(local_size_x = 1) in;

layout(set = 0, binding = 0, std140) uniform Probe {
    #pragma probe(ALPHA)
    vec4 alpha;
    #pragma probe(BETA)
    vec4 beta;
    #pragma probe(GAMMA)
    vec4 gamma;
} probe;

layout(set = 0, binding = 1, std430) buffer Out { vec4 values[]; };

void main() {
    values[0] = probe.alpha;
    values[1] = probe.beta;
    values[2] = probe.gamma;
}
)";

std::filesystem::path writeShader(const std::string& name, const char* body)
{
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream(path) << body;
    return path;
}

TEST_F(VkWindowTest, AddingAFieldToABlockDeliversItWithoutARestart) {
    const auto path = writeShader("koral_semantic_reload.comp.glsl", kTwoFields);

    Probe probe;

    // Somewhere for the shader to put what it was given, big enough for the field that does not
    // exist yet — the test reads it back to see whether it arrived.
    auto readback = Buffer::Builder<glm::vec4>()
        .setData(std::vector<glm::vec4>(4, glm::vec4(0.f)))
        .addUsage(Buffer::Usage::eStorage)
        .addUsage(Buffer::Usage::eTransferSrc)
        .addUsage(Buffer::Usage::eTransferDst)
        .setType(Buffer::Type::eDeviceLocal)
        .build();
    ASSERT_TRUE(readback.valid()) << readback.error()->history();

    const auto shader = Shader::Builder{}
        .setLang<Shader::Lang::eGLSL>()
        .setStage(Shader::Stage::eCompute)
        .setPath(path)
        .getOrBuild("test.semanticReload");
    ASSERT_TRUE(shader.valid()) << shader.error()->history();

    auto pipeline = ComputePipeline::Builder{}.setComputeShader(shader).build();
    ASSERT_TRUE(pipeline.valid()) << pipeline.error()->history();

    auto set = DescriptorSet::Builder(ResourceRef<const kor::Pipeline>(pipeline), 0)
        .writeSemantic(0, probe)                                    // the semantic block
        .write(1, Descriptor(ResourceRef<const Buffer>(readback)))  // where it lands
        .build();
    ASSERT_TRUE(set.valid()) << set.error()->history();

    // One shape asked for, one buffer made for it.
    EXPECT_EQ(probe.semanticBuffers().blockCount(), 1u);

    const auto dispatch = [&] {
        CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
            cb.BindComputePipeline(ResourceRef<const ComputePipeline>(pipeline));
            cb.BindDescriptorSet(0, ResourceRef<const DescriptorSet>(set));
            cb.Dispatch(1, 1, 1);
        }, CommandBuffer::Usage::eCompute);
        return readback->Read<glm::vec4>();
    };

    {
        const auto values = dispatch();
        ASSERT_GE(values.size(), 3u);
        EXPECT_EQ(values[0], probe.alpha);
        EXPECT_EQ(values[1], probe.beta);
        EXPECT_EQ(values[2], glm::vec4(0.f)) << "nothing has written a third field yet";
    }

    const auto setGeneration = set.generation();

    // The edit.
    std::ofstream(path) << kThreeFields;

    // The whole point: nothing below asks for a rebuild, names the new field, or re-writes the
    // binding. The file changed; everything else follows from that.
    // Real frames, because that is what drives the repository — and a budget in seconds, because
    // the FileWatcher polls twice a second on a thread of its own. The work then lands over the two
    // frames after it notices: the repository repairs *before* it updates, so the pipeline reloads
    // in one frame and the descriptor set is replayed in the next.
    auto& scene = VkEnvironment::scene();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (probe.semanticBuffers().blockCount() != 2u && std::chrono::steady_clock::now() < deadline) {
        drawFrame(scene);
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    ASSERT_EQ(probe.semanticBuffers().blockCount(), 2u)
        << "the reshaped block never reached the object that fills it";

    EXPECT_GT(set.generation(), setGeneration) << "the descriptor set was not rebuilt";
    ASSERT_TRUE(set.valid()) << set.error()->history();

    {
        const auto values = dispatch();
        ASSERT_GE(values.size(), 3u);
        EXPECT_EQ(values[0], probe.alpha);
        EXPECT_EQ(values[1], probe.beta);
        EXPECT_EQ(values[2], probe.gamma) << "the field added by the edit did not arrive";
    }

    // The old shape's buffer is still there — kept, not leaked: two shapes have been asked for over
    // this run, and a block is keyed by shape rather than by shader.
    EXPECT_EQ(probe.semanticBuffers().blockCount(), 2u);

    std::filesystem::remove(path);
}


// -----------------------------------------------------------------------------
// Y-orientation parity (see orientation_shared.h): a top-half quad drawn by the
// rasterizer and the same pattern written by a compute imageStore must land in
// the same place, and both are blit to the screen. This is the Vulkan reference
// for the identical check run on OpenGL in test_windowed_gl.cpp.
// -----------------------------------------------------------------------------
// The GUI extras with a real backend behind them: the viewport must actually get a texture handle for
// its image (the one thing a headless test cannot check), be laid out to a real size, and survive a
// gizmo drawn over it — all inside the frame the scene's RenderUI runs in.
TEST_F(VkWindowTest, GuiExtrasDrawInARealFrame) {
    auto& scene = VkEnvironment::scene();

    for (int i = 0; i < 4; ++i) drawFrame(scene);

    EXPECT_TRUE(scene.viewport.showing())
        << "the backend produced no texture handle for the viewport's image";
    // The case a scene actually hits: a per-frame, sampled-only colour target, shown with no copy and
    // no transfer usage. Several frames have gone by, so every swap-chain image has been used —
    // which is what the layout error was about.
    EXPECT_TRUE(scene.directViewport.showing())
        << "a sampled-only per-frame target could not be shown";
    EXPECT_GT(scene.viewport.size().x, 0u);
    EXPECT_GT(scene.viewport.size().y, 0u);
    EXPECT_FALSE(scene.gizmo.isUsing()) << "nothing was dragged";

    // The window has been drawn at some size, so the image's rectangle is inside it.
    EXPECT_GT(scene.viewport.rect().size.x, 0.f);
    EXPECT_GT(scene.viewport.rect().size.y, 0.f);
}

// Resizing a viewport's target every frame — what dragging a floating window's edge does — must not
// leave the interface sampling an image nothing has transitioned.
//
// It did: a resize *replaces* the image, and the backend reset only its own layout map while the barrier
// resolver reads the one in kor::Image. The resolver therefore compared a brand-new image against the
// old one's state, decided no barrier was needed, and ImGui sampled it in VK_IMAGE_LAYOUT_UNDEFINED —
// once per frame, with a different VkImage each time, and a grey window while the drag lasted.
//
// Asserted through the log, which is where the validation layer's complaints land.
//
// It also covers the viewport keeping up *without being told*: `directViewport` is given its image
// once, at Initialize, so the only thing that can rebuild its handle across these twelve resizes is
// the viewport noticing for itself. The obvious assertion for that does not work — `showing()` stays
// true with a *stale* handle, since the handle object still exists and merely names a destroyed
// VkImage — so the layer is the only witness. Disabling Viewport::refreshHandle turns this check into
// 40 errors of "Invalid VkDescriptorSet Object".
TEST_F(VkWindowTest, ResizingAViewportTargetEveryFrameIsClean) {
    auto& scene = VkEnvironment::scene();

    // Settle first: the frames before this may legitimately have transitioned things.
    for (int i = 0; i < 3; ++i) drawFrame(scene);
    kor::log::clearHistory();
    kor::log::resetRepeatCounts();

    scene.resizeTargetEveryFrame = true;
    for (int i = 0; i < 12; ++i) drawFrame(scene);
    scene.resizeTargetEveryFrame = false;

    // Wait for the frames to land, so anything the layer says about them has been said.
    kor::Context::Scheduler().WaitIdle();
    for (int i = 0; i < 3; ++i) drawFrame(scene);

    std::vector<std::string> complaints;
    for (const auto& record : kor::log::history()) {
        if (record.level == kor::log::Level::eError) complaints.push_back(record.message);
    }
    EXPECT_TRUE(complaints.empty())
        << complaints.size() << " error(s) while resizing, first: " << (complaints.empty() ? "" : complaints.front());
}

// Does a per-frame buffer's *other* copies receive a value written once?
//
// This is what a camera relies on. A camera writes its uniform block only when something moved, and
// the block is per-frame — so the copies belonging to the other frames in flight get the new value by
// propagation (Buffer::automaticUpdate copies it across as each frame comes round), not by being
// written. If that propagation does not converge, a camera that moves and then holds still shows
// alternating old and new values: the view trembles at the frame-in-flight period.
TEST_F(VkWindowTest, APerFrameBufferPropagatesAWriteToEveryCopy) {
    auto& scene = VkEnvironment::scene();
    const auto copies = kor::Context::Scheduler().getImageCount();
    ASSERT_GE(copies, 2u) << "nothing to propagate to with a single copy";

    kor::Buffer::RawBuilder rb;
    rb.setRawSize(static_cast<glm::i64>(sizeof(glm::u32)))
      .addUsage(kor::Buffer::Usage::eUniform)
      .setIsPerFrame(true)
      .setType(kor::Buffer::Type::eDynamic);
    auto buffer = rb.build();
    ASSERT_TRUE(static_cast<bool>(buffer));

    // Written once, on whichever frame is current — exactly what a camera does when it stops moving.
    constexpr glm::u32 kValue = 0xC0FFEE;
    const std::array<glm::u32, 1> value{ kValue };
    buffer->Write(std::span<const glm::u32>(value), 0);

    // Then several frames with no further write. Every copy must come to hold it.
    std::vector<glm::u32> seen;
    for (glm::u32 i = 0; i < copies * 2 + 1; ++i) {
        drawFrame(scene);
        seen.push_back(buffer->Read<glm::u32>(1).front());
    }

    for (std::size_t i = 0; i < seen.size(); ++i) {
        EXPECT_EQ(seen[i], kValue)
            << "frame " << i << " of " << copies << " in flight read a stale copy";
    }
}

// The moving-camera case: a per-frame buffer written with a *different* value every frame must read
// back, on that frame, as the value written on that frame — never as a neighbour's.
//
// This is the shape of a camera being moved. If a frame ever reads another frame's value the view
// jumps back and forth by one frame's worth of movement, which is what "trembling" looks like.
TEST_F(VkWindowTest, APerFrameBufferWrittenEveryFrameReadsBackWhatItWasGiven) {
    auto& scene = VkEnvironment::scene();

    kor::Buffer::RawBuilder rb;
    rb.setRawSize(static_cast<glm::i64>(sizeof(glm::u32)))
      .addUsage(kor::Buffer::Usage::eUniform)
      .setIsPerFrame(true)
      .setType(kor::Buffer::Type::eDynamic);
    auto buffer = rb.build();
    ASSERT_TRUE(static_cast<bool>(buffer));

    for (glm::u32 i = 1; i <= 12; ++i) {
        // Written where a camera writes it: from the repository update at the top of the frame.
        const std::array<glm::u32, 1> value{ i };
        buffer->Write(std::span<const glm::u32>(value), 0);

        const auto readBack = buffer->Read<glm::u32>(1).front();
        EXPECT_EQ(readBack, i) << "frame " << i << " read a value from another frame";

        drawFrame(scene);
    }
}

// How much does writing a per-frame buffer cost per frame?
//
// Not a pass/fail assertion — a measurement, printed, because the answer decides whether the
// propagation path is worth rewriting. A camera writes its uniform block on every frame it moves, and
// each write schedules a propagation copy that vk::Buffer::automaticUpdate used to submit *and wait
// on* (runSingleTimeCommand defaults to wait=true → queue->waitIdle()), which is a full queue stall
// per written buffer per frame.
TEST_F(VkWindowTest, MeasureWhereTheFrameGoes) {
    auto& scene = VkEnvironment::scene();

    // A fixed, realistic log, filled *before* any timing: letting it grow between phases was what made
    // an earlier version of this measurement compare two different workloads and report nonsense.
    kor::log::clearHistory();
    kor::log::setRepeatLimit(0);          // no suppression: every one of these must land
    for (int i = 0; i < 2000; ++i) kor::log::info("a log line with some text in it, number {}", i);
    kor::log::setRepeatLimit(10);

    const auto time = [&](const int count) {
        for (int i = 0; i < 3; ++i) drawFrame(scene);
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < count; ++i) drawFrame(scene);
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / count;
    };

    // Alternated and reduced by minimum: a frame can only be *slowed* by something else on the machine,
    // so the fastest run of each is the one least polluted by it.
    double withPanel = 1e9, withoutPanel = 1e9;
    for (int repeat = 0; repeat < 5; ++repeat) {
        scene.drawLogPanel = true;
        withPanel = std::min(withPanel, time(40));
        scene.drawLogPanel = false;
        withoutPanel = std::min(withoutPanel, time(40));
    }
    scene.drawLogPanel = true;

    std::cout << "[ MEASURE  ] " << kor::log::history().size() << " records: "
              << withoutPanel << " ms/frame without the log panel, " << withPanel << " with it ("
              << (withPanel - withoutPanel) << " ms is the panel)" << std::endl;
    SUCCEED();
}

TEST_F(VkWindowTest, MeasurePerFrameBufferWriteCost) {
    auto& scene = VkEnvironment::scene();

    kor::Buffer::RawBuilder rb;
    rb.setRawSize(static_cast<glm::i64>(sizeof(glm::u32)))
      .addUsage(kor::Buffer::Usage::eUniform)
      .setIsPerFrame(true)
      .setType(kor::Buffer::Type::eDynamic);
    auto buffer = rb.build();

    const auto time = [&](const int count, const bool writing) {
        for (int i = 0; i < 3; ++i) drawFrame(scene);
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < count; ++i) {
            if (writing) {
                const std::array<glm::u32, 1> value{ static_cast<glm::u32>(i) };
                buffer->Write(std::span<const glm::u32>(value), 0);
            }
            drawFrame(scene);
        }
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / count;
    };

    // Alternated, and reduced by minimum for the reason given in MeasureWhereTheFrameGoes.
    double writing = 1e9, idle = 1e9;
    for (int repeat = 0; repeat < 5; ++repeat) {
        idle = std::min(idle, time(40, false));
        writing = std::min(writing, time(40, true));
    }

    std::cout << "[ MEASURE  ] " << idle << " ms/frame idle, " << writing
              << " while writing a per-frame buffer every frame (" << (writing - idle) << " ms is the write)"
              << std::endl;
    SUCCEED();
}

// Undocking a viewport — giving it an OS window of its own — must not crash, and the panel must keep
// showing its image there.
//
// Forced rather than dragged: a window placed outside the main viewport's rectangle is exactly what
// makes ImGui promote it to a platform window, which is the same state undocking produces.
TEST_F(VkWindowTest, AViewportSurvivesBeingGivenItsOwnWindow) {
    if (!(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)) {
        GTEST_SKIP() << "multi-viewport is off on this platform (Wayland); nothing can undock";
    }

    auto& scene = VkEnvironment::scene();
    for (int i = 0; i < 3; ++i) drawFrame(scene);

    scene.floatViewportOutsideMainWindow = true;
    for (int i = 0; i < 8; ++i) drawFrame(scene);      // create the platform window and live with it

    EXPECT_TRUE(scene.directViewport.showing()) << "the floating panel lost its image";

    scene.floatViewportOutsideMainWindow = false;
    for (int i = 0; i < 8; ++i) drawFrame(scene);      // and back again, destroying it
    EXPECT_TRUE(scene.directViewport.showing());
}

// The cursor mode is a mode of *every* window input is read from, and a change to it must not arrive
// as movement.
//
// Capturing warps the pointer and releasing it puts it back; reporting either as a delta would fling a
// camera the instant aiming began, which is the classic version of this bug.
TEST_F(VkWindowTest, CapturingTheCursorReportsNoMovementForIt) {
    auto& scene = VkEnvironment::scene();
    ASSERT_EQ(kor::Input::getCursorMode(), kor::Input::CursorMode::eNormal);

    for (int i = 0; i < 3; ++i) drawFrame(scene);

    kor::Input::setCursorMode(kor::Input::CursorMode::eCaptured);
    EXPECT_EQ(kor::Input::getCursorMode(), kor::Input::CursorMode::eCaptured);
    EXPECT_EQ(glfwGetInputMode(*kor::Context::Window(), GLFW_CURSOR), GLFW_CURSOR_DISABLED);

    drawFrame(scene);
    EXPECT_EQ(kor::Input::getMousePositionDelta(), glm::vec2(0.f, 0.f))
        << "the warp that capturing performs was reported as movement";

    kor::Input::setCursorMode(kor::Input::CursorMode::eNormal);
    EXPECT_EQ(glfwGetInputMode(*kor::Context::Window(), GLFW_CURSOR), GLFW_CURSOR_NORMAL);

    drawFrame(scene);
    EXPECT_EQ(kor::Input::getMousePositionDelta(), glm::vec2(0.f, 0.f))
        << "releasing the cursor was reported as movement";

    // Hidden is the middle setting: invisible, but still free to move.
    kor::Input::setCursorMode(kor::Input::CursorMode::eHidden);
    EXPECT_EQ(glfwGetInputMode(*kor::Context::Window(), GLFW_CURSOR), GLFW_CURSOR_HIDDEN);
    kor::Input::setCursorMode(kor::Input::CursorMode::eNormal);
}

// A window attached while the cursor is captured has to arrive in the same mode, or the cursor
// reappears the moment the pointer crosses into an undocked panel.
TEST_F(VkWindowTest, AWindowAttachedWhileCapturedArrivesCaptured) {
    kor::Input::setCursorMode(kor::Input::CursorMode::eCaptured);

    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* second = glfwCreateWindow(64, 64, "second", nullptr, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(glfwGetInputMode(second, GLFW_CURSOR), GLFW_CURSOR_NORMAL) << "not attached yet";

    kor::Input::attachTo(second);
    EXPECT_EQ(glfwGetInputMode(second, GLFW_CURSOR), GLFW_CURSOR_DISABLED);

    kor::Input::setCursorMode(kor::Input::CursorMode::eNormal);
    EXPECT_EQ(glfwGetInputMode(second, GLFW_CURSOR), GLFW_CURSOR_NORMAL) << "and follows a change";

    kor::Input::detachFrom(second);
    glfwDestroyWindow(second);
}

// Input must be readable from more than the main window, because an undocked interface panel is a
// window of its own and GLFW delivers events to the window they happen over.
//
// A real undocked panel cannot be produced from a test — it takes dragging — so this attaches a second
// window directly, which is the same path the GUI puts ImGui's windows through.
TEST_F(VkWindowTest, InputCanBeReadFromMoreThanTheMainWindow) {
    // Counted per window rather than compared against a total, because the total is not this test's
    // to predict: drawing a frame lets GUI::RenderPlatformWindows attach ImGui's own platform
    // windows, and a panel that does not fit the main viewport (the Log panel, against this
    // fixture's small window) is promoted to one. That is the mechanism working, not a leak, so
    // asserting on the list's *size* made this test fail for a reason that has nothing to do with
    // what it covers.
    const auto timesAttached = [](GLFWwindow* window) {
        const auto attached = kor::Input::attachedWindows();
        return std::ranges::count(attached, window);
    };

    const auto before = kor::Input::attachedWindows();
    ASSERT_FALSE(before.empty()) << "the main window should be attached";
    EXPECT_EQ(before.front(), *kor::Context::Window());

    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* second = glfwCreateWindow(64, 64, "second", nullptr, nullptr);
    ASSERT_NE(second, nullptr);

    kor::Input::attachTo(second);
    EXPECT_EQ(timesAttached(second), 1);
    // Attaching twice is not an error and does not double up — the GUI calls it every frame.
    kor::Input::attachTo(second);
    EXPECT_EQ(timesAttached(second), 1);

    // A frame with the extra window attached must be no different from one without.
    auto& scene = VkEnvironment::scene();
    for (int i = 0; i < 3; ++i) drawFrame(scene);
    EXPECT_EQ(timesAttached(second), 1) << "a frame must not disturb a window attached by hand";

    kor::Input::detachFrom(second);
    EXPECT_EQ(timesAttached(second), 0);
    EXPECT_EQ(timesAttached(*kor::Context::Window()), 1) << "and must leave the main window attached";
    glfwDestroyWindow(second);

    for (int i = 0; i < 2; ++i) drawFrame(scene);
}

// A per-frame image that is only ever *sampled* — never written — shown through a viewport across
// enough frames to come round to every copy in flight.
//
// It passes both with and without the frame-index fix to Image::trackingKey, and the reason is worth
// recording: a viewport's refresh uses an explicit ImageBarrier, and an explicit barrier is emitted
// unconditionally (Record::transitions), so every copy is transitioned whatever the tracker believes.
// The tracker's frame-blindness can therefore only bite an *implicit* barrier — one inferred from a
// declared use — which nothing here exercises. Kept as coverage of the sampled-only per-frame case,
// not as proof of that fix.
TEST_F(VkWindowTest, APerFrameImageThatIsOnlySampledIsShownCleanly) {
    auto& scene = VkEnvironment::scene();

    // Enough frames to come round to every copy at least twice.
    for (int i = 0; i < 3; ++i) drawFrame(scene);
    kor::log::clearHistory();
    kor::log::resetRepeatCounts();

    for (int i = 0; i < 10; ++i) drawFrame(scene);
    kor::Context::Scheduler().WaitIdle();
    for (int i = 0; i < 2; ++i) drawFrame(scene);

    // One validation error is expected here and is not ours: Dear ImGui's Vulkan backend reuses the
    // per-frame semaphore of each *platform window's* swapchain, and this test floats a panel, so
    // ImGui creates one. Confirmed by handle: the VkSwapchainKHR the message names is neither of the
    // ones kor::vk::SwapChain created. The engine's own instance of this VUID is fixed —
    // SwapChain::ClaimAcquiredImage waits out the frame that still owns the acquired image — and a
    // regression there would name our swapchain and still fail this, because only this exact
    // message is dropped.
    constexpr std::string_view imguiViewportSemaphoreReuse = "may still be in use by VkSwapchainKHR";

    std::vector<std::string> complaints;
    for (const auto& record : kor::log::history()) {
        if (record.level != kor::log::Level::eError) continue;
        if (record.message.find(imguiViewportSemaphoreReuse) != std::string::npos) continue;
        complaints.push_back(record.message);
    }
    EXPECT_TRUE(complaints.empty())
        << complaints.size() << " error(s), first: " << (complaints.empty() ? "" : complaints.front());
    EXPECT_TRUE(scene.sampledOnlyViewport.showing());
}

// ...and the picture is actually *there* after a resize settles: the target is rendered into at its new
// size, and the handle showing it was rebuilt for that size.
//
// This is the "grey while resizing" half. Grey during a continuous drag is the documented frame of lag
// (see koralViewport.h), but a resize that *stops* must leave a real picture rather than an empty image
// — which is what a handle still bound to the replaced image would give.
TEST_F(VkWindowTest, AResizedViewportTargetIsShownAtItsNewSize) {
    auto& scene = VkEnvironment::scene();

    scene.resizeTargetEveryFrame = true;
    for (int i = 0; i < 6; ++i) drawFrame(scene);
    scene.resizeTargetEveryFrame = false;

    // Settle: one frame to resize, one to render into it and show it.
    scene.viewportFramebuffer->Resize(glm::uvec2{ 96, 72 });
    for (int i = 0; i < 3; ++i) drawFrame(scene);

    EXPECT_EQ(scene.viewportTarget->getExtent(), glm::uvec3(96, 72, 1));
    EXPECT_TRUE(scene.directViewport.showing()) << "the handle did not survive the resize";

    // The target holds what the pass cleared it to, at the new size — so it was rendered into after
    // being replaced, not left undefined.
    kor::Buffer::RawBuilder rb;
    rb.setRawSize(static_cast<glm::i64>(96) * 72 * 4)
      .addUsage(kor::Buffer::Usage::eTransferDst)
      .setType(kor::Buffer::Type::eReadback);
    auto readback = rb.build();

    kor::CommandBuffer::SingleTimeCommand([&](kor::CommandBuffer& cb) {
        cb.CopyImageToBuffer(kor::ResourceRef<const kor::Image>(scene.viewportTarget),
                             kor::ResourceRef<const kor::Buffer>(readback));
    }, kor::CommandBuffer::Usage::eTransfer);

    const auto texels = readback->Read<glm::u8vec4>();
    ASSERT_EQ(texels.size(), static_cast<std::size_t>(96) * 72);
    // The framebuffer's own clear colour, {0.2, 0, 0.4, 1}.
    EXPECT_NEAR(texels.front().r, 51, 3);
    EXPECT_NEAR(texels.front().b, 102, 3);
}

// A frame that never touches the window's framebuffer must still come out as the colour that
// framebuffer was given: a scene that renders into its own targets and shows them through the
// interface would otherwise present last frame's picture, or uninitialised memory.
TEST_F(VkWindowTest, AnUntouchedScreenIsClearedByTheRuntime) {
    // What the runtime does each frame, in the order it does it. The scene deliberately records
    // nothing against the screen — as a viewport-only scene does not.
    auto& scene = VkEnvironment::scene();
    bool cleared = false;

    kor::Context::Scheduler().Draw([&](kor::CommandBuffer& cb) {
        const auto framebuffer = kor::Context::DefaultFramebuffer();
        ASSERT_TRUE(framebuffer.valid());
        ASSERT_FALSE(framebuffer->getColorAttachments().empty());
        const auto screen = framebuffer->getColorAttachments()[0].get().getImage();

        // Nothing has been recorded, so nothing can have touched it.
        EXPECT_FALSE(cb.hasTouched(screen));

        cb.BeginRendering();
        cb.EndRendering();
        cleared = true;

        // ...and now it has, which is what stops the runtime clearing it a second time.
        EXPECT_TRUE(cb.hasTouched(screen));

        kor::GUI::Render(cb, scene);
    });
    kor::GUI::RenderPlatformWindows();

    EXPECT_TRUE(cleared);
}

// The other half: a frame that *did* touch the screen is left alone.
TEST_F(VkWindowTest, ATouchedScreenIsNotClearedAgain) {
    auto& scene = VkEnvironment::scene();

    kor::Context::Scheduler().Draw([&](kor::CommandBuffer& cb) {
        const auto framebuffer = kor::Context::DefaultFramebuffer();
        const auto screen = framebuffer->getColorAttachments()[0].get().getImage();

        cb.ClearColorImage(screen, glm::vec4{0.1f, 0.2f, 0.3f, 1.f});
        EXPECT_TRUE(cb.hasTouched(screen)) << "a clear is an interaction with the framebuffer";

        kor::GUI::Render(cb, scene);
    });
    kor::GUI::RenderPlatformWindows();
}

// GPU timers over the real frame path, which is the one thing the headless timer tests cannot
// reach: the frame's command buffer is reset and re-recorded every frame, and its results are
// collected when its frame in flight comes round again. Run with validation on, this is also what
// proves the query pool is reset legally — outside a render pass, and never while in use.
TEST_F(VkWindowTest, FrameTimersReportTheFramesOwnWork) {
    auto& scene = VkEnvironment::scene();

    // Long enough for a frame that recorded a timer to complete and be recorded into again, which
    // takes a full cycle of the frames in flight.
    const int budget = static_cast<int>(kor::Context::Scheduler().getImageCount()) + 4;
    bool found = false;
    double milliseconds = 0.0;

    for (int frame = 0; frame < budget && !found; ++frame) {
        kor::Context::Scheduler().Draw([&](kor::CommandBuffer& cb) {
            // Fetched per frame: the default framebuffer's colour attachment is the swap-chain
            // image this frame presents, so a reference taken once outside the loop goes stale
            // the moment the chain rotates.
            const auto framebuffer = kor::Context::DefaultFramebuffer();
            ASSERT_TRUE(framebuffer.valid());
            const auto screen = framebuffer->getColorAttachments()[0].get().getImage();

            cb.Timer("frame.clear", [&](kor::CommandBuffer& inner) {
                inner.ClearColorImage(screen, glm::vec4{0.1f, 0.2f, 0.3f, 1.f});
            });
            kor::GUI::Render(cb, scene);
        });
        kor::GUI::RenderPlatformWindows();

        for (const auto& f : kor::Context::Scheduler().getFrames()) {
            for (const auto& timing : f.get().getCommandBuffer().getTimings()) {
                if (timing.label != "frame.clear") continue;
                found = true;
                milliseconds = timing.milliseconds;
            }
        }
    }

    ASSERT_TRUE(found) << "no frame reported its timer within " << budget << " frames";
    EXPECT_GT(milliseconds, 0.0);
    EXPECT_LT(milliseconds, 1000.0);
}

// A mip level or an array layer of an ordinary 2D image is a *view*, not a copy — so showing one asks
// nothing of the image beyond eSampled. This is the case a viewport hits, and it used to demand
// eTransferSrc and blit the whole image every frame.
TEST_F(VkWindowTest, GuiImageShowsAMipOrLayerWithoutCopying) {
    auto mipped = kor::Image::Builder{}
        .setType(kor::Image::Type::e2D)
        .setFormat(kor::Image::Format::eRGBA8_UNORM)
        .setExtent(glm::uvec2{32, 32})
        .setMipLevels(3)
        .addUsage(kor::Image::Usage::eSampled)   // sampled only: no transfer usage at all
        .build();
    ASSERT_TRUE(static_cast<bool>(mipped));

    auto level0 = kor::GUI_Image::Create(kor::ResourceRef<const kor::Image>(mipped));
    EXPECT_TRUE(static_cast<bool>(level0));
    auto level2 = kor::GUI_Image::Create(kor::ResourceRef<const kor::Image>(mipped), 0, 2);
    EXPECT_TRUE(static_cast<bool>(level2));

    auto layered = kor::Image::Builder{}
        .setType(kor::Image::Type::e2D)
        .setFormat(kor::Image::Format::eRGBA8_UNORM)
        .setExtent(glm::uvec2{32, 32})
        .setArrayLayers(6)
        .addUsage(kor::Image::Usage::eSampled)
        .build();
    auto face = kor::GUI_Image::Create(kor::ResourceRef<const kor::Image>(layered), 4, 0);
    EXPECT_TRUE(static_cast<bool>(face));
}

// What genuinely *does* need a copy — a 3D image's slice — and so needs eTransferSrc. The requirement
// was undocumented and its only symptom was a wall of validation messages naming a usage flag; now it
// is one error that says which flag and why.
TEST_F(VkWindowTest, GuiImageSaysWhyItCannotCopyFromAnImage) {
    auto volume = kor::Image::Builder{}
        .setType(kor::Image::Type::e3D)
        .setFormat(kor::Image::Format::eRGBA8_UNORM)
        .setExtent(glm::uvec3{16, 16, 4})
        .addUsage(kor::Image::Usage::eSampled)   // sampled, but not readable by a copy
        .build();
    ASSERT_TRUE(static_cast<bool>(volume));

    try {
        auto handle = kor::GUI_Image::Create(kor::ResourceRef<const kor::Image>(volume), 1, 0);
        FAIL() << "a slice of a 3D image without eTransferSrc cannot be shown, and should say so";
    } catch (const kor::BackendException& e) {
        EXPECT_NE(e.error.message.find("eTransferSrc"), std::string::npos) << e.error.message;
    }
}

TEST_F(VkWindowTest, RasterTriangleOrientationToScreen) {
    auto r = orient::rasterTopHalf();
    orient::expectHalfSplit(r.pixels);
    orient::blitToScreen(r.image);
}

TEST_F(VkWindowTest, ComputeTriangleOrientationToScreen) {
    auto r = orient::computeTopHalf();
    orient::expectHalfSplit(r.pixels);
    orient::blitToScreen(r.image);
}

} // namespace

// Registered before RUN_ALL_TESTS (compatible with gtest_main). gtest owns and
// deletes the environment.
static ::testing::Environment* const kVkEnv =
    ::testing::AddGlobalTestEnvironment(new VkEnvironment);
