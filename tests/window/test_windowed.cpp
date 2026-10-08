// Windowed Vulkan integration tests: boot a real window + surface + swap chain +
// scheduler, render frames (with a koral-ui interface over them, showing images),
// resize, and — the parity part — verify that a rasterized triangle and the same
// pattern written by a compute imageStore both present the same way.
//
// The window/context is created once for the whole binary by VkEnvironment and
// shared by every test through the VkWindowTest fixture (mirroring the headless
// GpuTest). A real display (X11/Wayland) and a
// WSI-capable Vulkan loader must be available; tests skip gracefully otherwise.
// One windowed Vulkan context per process — a second corrupts the heap (GLFW and
// the Vulkan statics don't survive re-initialization) — so everything runs
// against the single shared window.

#include <gtest/gtest.h>

#include <dlfcn.h>

#include <algorithm>
#include <array>
#include <map>
#include <tuple>
#include <chrono>
#include <filesystem>
#include <functional>
#include <fstream>
#include <thread>
#include <iostream>
#include <map>
#include <memory>
#include <string_view>

#include <GLFW/glfw3.h>

#include "commandBuffer.h"
#include "computePipeline.h"
#include "descriptor.h"
#include "descriptorSet.h"
#include "semantics.h"
#include "shader.h"
#include "error.h"
#include "framebuffer.h"
#include "frameGraph.h"
#include "input.h"
#include "imageView.h"
#include "buffer.h"
#include "log.h"
#include "app.h"
#include "context.h"
#include "image.h"
#include "resource.h"
#include "scene.h"
#include "scheduler.h"
#include "window.h"

#include "orientation_shared.h"
#include "scheduler_seam_shared.h"

// The GUI extras module, in a real scene's koral-ui interface.
#include <koralGuiExtras.h>
#include <kui/render.h>

namespace {

// A scene that draws a cleared default framebuffer with a koral-ui interface over it — a panel of text,
// the GUI extras' panels and viewports showing images — so a single frame drives the scheduler, the swap
// chain and an interface sampling images the scene renders.
class OverlayScene : public kor::Scene {
public:
    void Initialize() override {
        _image = kor::Image::Builder{}
                     .SetType(kor::Image::Type::e2D)
                     .SetFormat(kor::Image::Format::eRGBA8_UNORM)
                     .SetExtent(kor::UVec2{16, 16})
                     .SetUsage(kor::Image::Usage::eTransferSrc | kor::Image::Usage::eTransferDst | kor::Image::Usage::eSampled)
                     .Build();
        kor::CommandBuffer::SingleTimeCommand([&](kor::CommandBuffer& cb) {
            cb.ClearColorImage(_image, kor::Vec4{0.3f, 0.6f, 0.9f, 1.f});
        }, kor::CommandBuffer::Usage::eGraphics).Wait();
        viewport->SetImage(_image);

        viewportTarget = kor::Image::Builder{}
            .SetType(kor::Image::Type::e2D)
            .SetFormat(kor::Image::Format::eRGBA8_UNORM)
            .SetExtent(kor::UVec2{64, 64})
            .SetIsPerFrame(true)            // one copy per frame in flight, like a real render target
            // eTransferSrc because AResizedViewportTargetIsShownAtItsNewSize copies it back out;
            // setUsage names the whole set, so it has to be listed with the other two.
            .SetUsage(kor::Image::Usage::eColorAttachment | kor::Image::Usage::eSampled
                    | kor::Image::Usage::eTransferSrc)
            .Build();
        sampledOnlyTarget = kor::Image::Builder{}
            .SetType(kor::Image::Type::e2D)
            .SetFormat(kor::Image::Format::eRGBA8_UNORM)
            .SetExtent(kor::UVec2{32, 32})
            .SetIsPerFrame(true)
            .SetUsage(kor::Image::Usage::eSampled)
            .Build();

        // Once, here. Resizing a target replaces the image under the same handle, and the interface is
        // supposed to notice that by itself; a scene that handed it back every frame would hide one that
        // could not.
        directViewport->SetImage(viewportTarget);
        sampledOnlyViewport->SetImage(sampledOnlyTarget);

        viewportTargetView = kor::ImageView::Builder(viewportTarget).Build();
        viewportFramebuffer = kor::Framebuffer::Builder{}
            .AddColor({ .view = viewportTargetView, .clear = kor::Vec4{0.2f, 0.f, 0.4f, 1.f} })
            .Build();
        ui.SetRoot(Interface());
    }

    void Update() override {
        ++updates;
        if (onUpdate) onUpdate();

        // A window being dragged asks for a new size every frame, so the target is replaced every
        // frame — and every replacement is a fresh image in an undefined layout that the interface
        // samples in the same frame. This is the shape of that.
        if (resizeTargetEveryFrame) {
            const kor::UVec2 next{ 64 + (updates % 17), 48 + (updates % 13) };
            viewportFramebuffer->Resize(next);
        }

        // What the interface shows: the scene's own panels, or a test's, built again when that changes.
        const auto shape = std::tuple{ drawLogPanel, drawStatsPanel, drawViewports, testInterface.Get() };
        if (shape != _shape) {
            _shape = shape;
            ui.SetRoot(testInterface ? testInterface : Interface());
        }
        ui.Update();
    }

    void Render(kor::CommandBuffer& cb) override {
        if (drawDefault) {
            // Into the viewport's own target first, so the interface has something to show — and so
            // the target is rendered into at whatever size it now is.
            cb.BeginRendering(viewportFramebuffer);
            cb.EndRendering();

            cb.BeginRendering();   // default framebuffer: clears the swap-chain image
            cb.EndRendering();

            // And the interface over it, as the last thing in the frame.
            const auto screen = SceneWindow().DefaultFramebuffer();
            ui.GetRenderer().Prepare(screen);
            ui.GetRenderer().Record(cb, screen);
        }
        // A test's own graph, run as a scene's own is: in the frame, with the scene current.
        if (extraGraph) extraGraph->Execute();
        if (onRender) onRender(cb);
    }

    void OnResize(kor::UVec2 extent) override { lastResize = extent; }

    /** The scene's own interface: text and a slider, the image, the viewports, the log and the statistics. */
    kui::Widget Interface() {
        std::vector<kui::Widget> panels {
            kui::Text("Koral test overlay"),
            kui::Slider(_slider, [this](const float v) { _slider = v; }),
            kui::SizedBox(64.f, 64.f, kgui::Viewport(viewport)),
        };
        if (drawViewports) {
            panels.push_back(kui::SizedBox(80.f, 60.f, kgui::Viewport(directViewport)));
            panels.push_back(kui::SizedBox(40.f, 30.f, kgui::Viewport(sampledOnlyViewport)));
        }
        if (drawLogPanel) panels.push_back(kui::SizedBox(300.f, 120.f, kgui::LogPanel()));
        if (drawStatsPanel) panels.push_back(kui::SizedBox(300.f, 160.f, kgui::StatsPanel()));
        return kui::ScrollView(kui::Column(std::move(panels), kui::FlexOptions {}.SetGap(4.f)));
    }

    int updates = 0;
    kor::UVec2 lastResize{0, 0};
    /// Runs inside the frame, where a scene's own Update would write its per-frame data.
    std::function<void()> onUpdate;
    /// Records a test's own work into the frame, after (or, with drawDefault off, instead of) the scene's.
    std::function<void(kor::CommandBuffer&)> onRender;
    /// A test's own interface, shown instead of the scene's while it is set.
    kui::Widget testInterface;
    /// A graph of a test's own, executed in the frame as the scene's own graph is.
    kor::FrameGraph* extraGraph = nullptr;
    /// Whether Render draws the viewport target, the screen and the interface.
    bool drawDefault = true;

    kui::Ui ui;
    std::shared_ptr<kgui::ViewportState> viewport = std::make_shared<kgui::ViewportState>();
    // A colour target as a scene would actually make one: sampled and rendered into, with no transfer
    // usage needed to show it.
    kor::Resource<kor::Image> viewportTarget;
    std::shared_ptr<kgui::ViewportState> directViewport = std::make_shared<kgui::ViewportState>();
    /// When set, the target is resized every frame, as it is while a window edge is being dragged.
    bool resizeTargetEveryFrame = false;
    /// Which of the GUI extras to draw, so their cost can be measured against a bare frame.
    bool drawLogPanel = true;
    bool drawStatsPanel = true;
    bool drawViewports = true;

    /// A per-frame image the interface samples and *nothing ever writes*. Its access is therefore the
    /// same on every frame, which is the case a shared (frame-blind) layout tracker gets wrong: the
    /// resolver sees "already shader-read" and skips the barrier, while this frame's copy has never
    /// been transitioned at all.
    kor::Resource<kor::Image> sampledOnlyTarget;
    std::shared_ptr<kgui::ViewportState> sampledOnlyViewport = std::make_shared<kgui::ViewportState>();
    kor::Resource<kor::Framebuffer> viewportFramebuffer;
    kor::Resource<kor::ImageView> viewportTargetView;

private:
    float _slider = 0.5f;
    kor::Resource<kor::Image> _image;
    std::tuple<bool, bool, bool, const kui::WidgetBase*> _shape { true, true, true, nullptr };
};

// Drives one frame of the application: every scene, exactly as the runtime runs it.
void drawFrame(kor::Scene&) {
    kor::App::Current().Frame();
}

// One frame in which the scene records @p record — instead of its own drawing unless @p drawDefault.
void drawCustomFrame(OverlayScene& scene, std::function<void(kor::CommandBuffer&)> record, const bool drawDefault = false) {
    scene.onRender = std::move(record);
    scene.drawDefault = drawDefault;
    kor::App::Current().Frame();
    scene.onRender = nullptr;
    scene.drawDefault = true;
}

// ---- shared Vulkan window, created once for the whole binary -----------------
//
// One windowed Vulkan context per process (see the file header). All tests share
// it through VkWindowTest; the environment skips silently when no display / WSI
// loader is available and every test then GTEST_SKIPs.

class VkEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        try {
            // X11 deliberately: a window placed at an absolute screen position — what an undocked panel
            // is — is something Wayland denies, and some of these tests attach windows of their own.
            s_app = std::make_unique<kor::App>(kor::AppSettings{.api = kor::API::eVulkan, .platform = kor::WindowPlatform::eX11});
            s_scene = static_cast<OverlayScene*>(s_app->Open("Overlay", std::make_unique<OverlayScene>(), {
                .title = "Koral windowed test", .extent = {320, 240}, .resizable = true, .vsync = false}));
            if (!s_scene) {
                s_reason = "the test scene's window could not be opened";
                s_app.reset();
            }
        } catch (const std::exception& e) {
            s_reason = e.what();
            s_app.reset();
            s_scene = nullptr;
        }
    }

    void TearDown() override {
        if (s_app) kor::Context::DrainMainThread();
        s_scene = nullptr;
        s_app.reset();   // every scene shut down, then the device
    }

    static bool ready() { return s_scene != nullptr; }
    static const std::string& reason() { return s_reason; }
    static kor::Window& window() { return s_scene->SceneWindow(); }
    static OverlayScene& scene() { return *s_scene; }
    static kor::App& app() { return *s_app; }

private:
    static std::unique_ptr<kor::App> s_app;
    static OverlayScene* s_scene;
    static std::string s_reason;
};

std::unique_ptr<kor::App> VkEnvironment::s_app;
OverlayScene* VkEnvironment::s_scene = nullptr;
std::string VkEnvironment::s_reason = "no display";

class VkWindowTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!VkEnvironment::ready()) {
            GTEST_SKIP() << "windowed Vulkan context unavailable: " << VkEnvironment::reason();
        }
        EXPECT_EQ(kor::Context::ActiveAPI(), kor::API::eVulkan);
        // A test body runs as the scene's code would: with the scene current.
        _scope = std::make_unique<kor::detail::SceneScope>(&VkEnvironment::scene());
    }
    void TearDown() override { _scope.reset(); }

private:
    std::unique_ptr<kor::detail::SceneScope> _scope;
};

// Render frames with an interface over them (acquire/record/submit/present, with
// images sampled by the interface), then resize and keep drawing so the next Acquire/Present
// sees an out-of-date swap chain and recreates it (SwapChain::Resize + the
// default-framebuffer Resize path).
TEST_F(VkWindowTest, RenderResizeAndPresent) {
    auto& window = VkEnvironment::window();
    auto& scene = VkEnvironment::scene();

    // Phase 1: render enough frames to cycle every in-flight frame slot twice.
    for (int i = 0; i < 8 && !window.ShouldClose(); ++i) {
        drawFrame(scene);
    }
    EXPECT_GE(scene.updates, 1);

    // Phase 2: resize. Ask the compositor for a new size and pump events so the
    // surface actually changes, then keep drawing to hit the out-of-date/recreate
    // path in the scheduler and swap chain.
    glfwSetWindowSize(*window, 480, 360);
    for (int i = 0; i < 20; ++i) glfwPollEvents();
    for (int i = 0; i < 12 && !window.ShouldClose(); ++i) {
        drawFrame(scene);
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
// description in place and says so (Resource::MarkChanged) → the descriptor set, which is
// recoverable and rebuilds when an input changes, is replayed → SemanticBuffers::Acquire is asked
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
    kor::Vec4 alpha { 1.f, 2.f, 3.f, 4.f };
    kor::Vec4 beta  { 5.f, 6.f, 7.f, 8.f };
    kor::Vec4 gamma { 9.f, 10.f, 11.f, 12.f };

    [[nodiscard]] std::string_view SemanticNamespace() const override { return "probe"; }

    bool Serialize(const std::string_view semantic, kor::SemanticSlot& slot) const override
    {
        // The return says whether this object *answers for* the semantic, not whether the write
        // landed: a shape mismatch is recorded on the slot and reported with the rest. Returning
        // the write's result here would report a type error as "no such semantic".
        if (semantic == "ALPHA") { slot.Set(alpha); return true; }
        if (semantic == "BETA")  { slot.Set(beta);  return true; }
        if (semantic == "GAMMA") { slot.Set(gamma); return true; }
        return false;   // anything else: this object does not answer for it
    }

    kor::SemanticBuffers& SemanticStorage() override { return _buffers; }

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
    auto readback = Buffer::Builder<kor::Vec4>()
        .SetData(std::vector<kor::Vec4>(4, kor::Vec4(0.f)))
        .SetUsage(Buffer::Usage::eStorage | Buffer::Usage::eTransferSrc | Buffer::Usage::eTransferDst)
        .SetType(Buffer::Type::eDeviceLocal)
        .Build();
    ASSERT_TRUE(readback.Valid()) << readback.Failure()->History();

    const auto shader = Shader::Builder{}
        .SetLang<Shader::Lang::eGLSL>()
        .SetStage(Shader::Stage::eCompute)
        .SetPath(path)
        .GetOrBuild("test.semanticReload");
    ASSERT_TRUE(shader.Valid()) << shader.Failure()->History();

    auto pipeline = ComputePipeline::Builder{}.SetComputeShader(shader).Build();
    ASSERT_TRUE(pipeline.Valid()) << pipeline.Failure()->History();

    auto set = DescriptorSet::Builder(pipeline, 0)
        .WriteSemantic(0, probe)                                    // the semantic block
        .Write(1, readback)  // where it lands
        .Build();
    ASSERT_TRUE(set.Valid()) << set.Failure()->History();

    // One shape asked for, one buffer made for it.
    EXPECT_EQ(probe.SemanticStorage().BlockCount(), 1u);

    const auto dispatch = [&] {
        CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
            cb.BindComputePipeline(pipeline);
            cb.BindDescriptorSet(0, set);
            cb.Dispatch(1, 1, 1);
        }, CommandBuffer::Usage::eCompute).Wait();
        return readback->Read<kor::Vec4>();
    };

    {
        const auto values = dispatch();
        ASSERT_GE(values.size(), 3u);
        EXPECT_EQ(values[0], probe.alpha);
        EXPECT_EQ(values[1], probe.beta);
        EXPECT_EQ(values[2], kor::Vec4(0.f)) << "nothing has written a third field yet";
    }

    const auto setGeneration = set.Generation();

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
    while (probe.SemanticStorage().BlockCount() != 2u && std::chrono::steady_clock::now() < deadline) {
        drawFrame(scene);
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    ASSERT_EQ(probe.SemanticStorage().BlockCount(), 2u)
        << "the reshaped block never reached the object that fills it";

    EXPECT_GT(set.Generation(), setGeneration) << "the descriptor set was not rebuilt";
    ASSERT_TRUE(set.Valid()) << set.Failure()->History();

    {
        const auto values = dispatch();
        ASSERT_GE(values.size(), 3u);
        EXPECT_EQ(values[0], probe.alpha);
        EXPECT_EQ(values[1], probe.beta);
        EXPECT_EQ(values[2], probe.gamma) << "the field added by the edit did not arrive";
    }

    // The old shape's buffer is still there — kept, not leaked: two shapes have been asked for over
    // this run, and a block is keyed by shape rather than by shader.
    EXPECT_EQ(probe.SemanticStorage().BlockCount(), 2u);

    std::filesystem::remove(path);
}


// -----------------------------------------------------------------------------
// Y-orientation parity (see orientation_shared.h): a top-half quad drawn by the
// rasterizer and the same pattern written by a compute imageStore must land in
// the same place, and both are blit to the screen.
// -----------------------------------------------------------------------------
// The GUI extras with a real device behind them: each viewport laid out to a real size, its image sampled
// by the interface — the per-frame, sampled-only one included — all in a frame the scene draws.
TEST_F(VkWindowTest, GuiExtrasDrawInARealFrame) {
    auto& scene = VkEnvironment::scene();
    for (int i = 0; i < 4; ++i) drawFrame(scene);

    // In pixels: twice the layout's size on a Retina display.
    const float ratio = scene.SceneWindow().PixelRatio();
    EXPECT_EQ(scene.viewport->Size(), kor::UVec2(kor::Vec2(64, 64) * ratio)) << "laid out at the size it was given";
    EXPECT_GT(scene.directViewport->Size().x, 0u);
    EXPECT_GT(scene.sampledOnlyViewport->Size().x, 0u);
    EXPECT_FALSE(scene.viewport->Dragging()) << "nothing was pressed";
    const auto texts = kui::debug::Texts(scene.ui);
    EXPECT_TRUE(std::ranges::any_of(texts, [](const std::string& t) { return t.find("resources tracked") != std::string::npos; }))
        << "the statistics panel is there";
}

// Resizing a viewport's target every frame — what dragging a floating window's edge does — must not
// leave the interface sampling an image nothing has transitioned.
//
// It did: a resize *replaces* the image, and the backend reset only its own layout map while the barrier
// resolver reads the one in kor::Image. The resolver therefore compared a brand-new image against the
// old one's state, decided no barrier was needed, and the interface sampled it in VK_IMAGE_LAYOUT_UNDEFINED
// — once per frame, with a different VkImage each time, and a grey window while the drag lasted.
//
// Asserted through the log, which is where the validation layer's complaints land.
//
// It also covers the interface keeping up *without being told*: `directViewport` is given its image once,
// at Initialize, so the only thing that can bind the new image across these twelve resizes is the
// interface's renderer noticing its generation change. The layer is the only witness: a descriptor still
// naming the replaced image is an "Invalid VkDescriptorSet Object".
TEST_F(VkWindowTest, ResizingAViewportTargetEveryFrameIsClean) {
    auto& scene = VkEnvironment::scene();

    // Settle first: the frames before this may legitimately have transitioned things.
    for (int i = 0; i < 3; ++i) drawFrame(scene);
    kor::log::ClearHistory();
    kor::log::ResetRepeatCounts();

    scene.resizeTargetEveryFrame = true;
    for (int i = 0; i < 12; ++i) drawFrame(scene);
    scene.resizeTargetEveryFrame = false;

    // Wait for the frames to land, so anything the layer says about them has been said.
    kor::Context::Scheduler().WaitIdle();
    for (int i = 0; i < 3; ++i) drawFrame(scene);

    std::vector<std::string> complaints;
    for (const auto& record : kor::log::History())
        if (record.level == kor::log::Level::eError) complaints.push_back(record.message);
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
    const auto copies = kor::Context::Scheduler().ImageCount();
    ASSERT_GE(copies, 2u) << "nothing to propagate to with a single copy";

    kor::Buffer::RawBuilder rb;
    rb.SetRawSize(static_cast<kor::i64>(sizeof(kor::u32)))
      .SetUsage(kor::Buffer::Usage::eUniform)
      .SetIsPerFrame(true)
      .SetType(kor::Buffer::Type::eDynamic);
    auto buffer = rb.Build();
    ASSERT_TRUE(static_cast<bool>(buffer));

    // Written once, on whichever frame is current — exactly what a camera does when it stops moving.
    constexpr kor::u32 kValue = 0xC0FFEE;
    const std::array<kor::u32, 1> value{ kValue };
    buffer->Write(std::span<const kor::u32>(value), 0);

    // Then several frames with no further write. Every copy must come to hold it.
    std::vector<kor::u32> seen;
    for (kor::u32 i = 0; i < copies * 2 + 1; ++i) {
        drawFrame(scene);
        seen.push_back(buffer->Read<kor::u32>(1).front());
    }

    for (std::size_t i = 0; i < seen.size(); ++i) {
        EXPECT_EQ(seen[i], kValue)
            << "frame " << i << " of " << copies << " in flight read a stale copy";
    }
}

// ---- frame graph: previous-frame resources ---------------------------------------------------
//
// A pass that declares ReadPrevious sees the resource as it stood at the end of the last frame — not
// as a pass earlier in *this* frame left it, and not a stale or undefined copy. These drive a graph of
// their own through the scheduler, the way the runtime does after Scene::Render.

namespace {
    // Clears "stamp" to this frame's number: a value that says which frame wrote it.
    class StampPass final : public kor::RenderPass {
    public:
        StampPass(kor::Image::Format format, float& value) : RenderPass("Stamp"), _format(format), _value(value) {}
        void Setup(kor::PassBuilder& b) override {
            const bool depth = kor::IsDepthStencilFormat(_format);
            b.Create("stamp", { .format = _format,
                                .usage = kor::Image::Usage::eTransferDst | kor::Image::Usage::eSampled
                                       | (depth ? kor::Image::Usage::eDepthStencilAttachment : kor::Image::Usage::eTransferDst) });
        }
        void Initialize(const kor::PassResources& r) override {
            _stamp = r.ImageNamed("stamp");
            if (kor::IsDepthStencilFormat(_format))
                _framebuffer = kor::Framebuffer::Builder{}.SetDepth({ .view = _stamp }).Build();
        }
        void Prepare() override { _frameValue = _value; }
        void Record(kor::CommandBuffer& cb) const override {
            if (_framebuffer) cb.BeginRendering(kor::RenderInfo(_framebuffer).SetClearDepth(_frameValue)).EndRendering();
            else cb.ClearColorImage(_stamp, kor::Vec4(_frameValue));
        }
    private:
        kor::Image::Format _format;
        float& _value;
        float _frameValue = 0.f;
        kor::ResourceRef<const kor::Image> _stamp;
        kor::Resource<kor::Framebuffer> _framebuffer;
    };

    // Runs after the stamp in the same frame, and copies out the *previous* frame's stamp.
    class ProbePass final : public kor::RenderPass {
    public:
        ProbePass() : RenderPass("Probe") {}
        void Setup(kor::PassBuilder& b) override { b.Read("stamp").ReadPrevious("stamp").SideEffect(); }
        void Initialize(const kor::PassResources& r) override {
            _previous = r.PreviousImageNamed("stamp");
            readback = kor::Buffer::RawBuilder{}
                .SetRawSize(static_cast<kor::i64>(sizeof(float)))
                .SetUsage(kor::Buffer::Usage::eTransferDst)
                .SetType(kor::Buffer::Type::eReadback)
                .Build();
        }
        void Prepare() override { hadPrevious = HasPrevious("stamp"); }
        void Record(kor::CommandBuffer& cb) const override {
            cb.CopyImageToBuffer(_previous, readback, kor::Copy{ .imageExtent = kor::IVec3(1, 1, 1) });
        }
        bool hadPrevious = false;
        kor::Resource<kor::Buffer> readback;
    private:
        kor::ResourceRef<const kor::Image> _previous;
    };

    // A frame the way the runtime draws one: the graph runs after Scene::Render.
    void drawGraphFrame(kor::Scene& scene, kor::FrameGraph& graph) {
        auto& overlay = static_cast<OverlayScene&>(scene);
        overlay.extraGraph = &graph;
        kor::App::Current().Frame();
        overlay.extraGraph = nullptr;
        kor::Context::Scheduler().WaitIdle();
    }

    void expectEachFrameSeesThePreviousOne(kor::Scene& scene, const kor::Image::Format format, const float cleared,
                                           const std::function<float(int)>& stampOf) {
        kor::FrameGraph graph;
        float value = 0.f;
        graph.Add<StampPass>(format, value);
        auto& probe = graph.Add<ProbePass>();

        for (int frame = 0; frame < 6; ++frame) {
            value = stampOf(frame);
            drawGraphFrame(scene, graph);
            const float seen = probe.readback->Read<float>(1).front();
            if (frame == 0) {
                EXPECT_FALSE(probe.hadPrevious) << "no frame before the first";
                EXPECT_EQ(seen, cleared) << "the history starts cleared, not undefined";
            } else {
                EXPECT_TRUE(probe.hadPrevious) << "frame " << frame;
                EXPECT_EQ(seen, stampOf(frame - 1)) << "frame " << frame << " did not see the frame before it";
            }
        }

        // A rebuild that leaves the resource as it was keeps its history: last frame is still last frame.
        graph.Invalidate();
        value = stampOf(6);
        drawGraphFrame(scene, graph);
        EXPECT_TRUE(probe.hadPrevious);
        EXPECT_EQ(probe.readback->Read<float>(1).front(), stampOf(5));

        // One that drops it — nothing reads the previous frame for a while — starts it over.
        probe.SetEnabled(false);
        value = stampOf(7);
        drawGraphFrame(scene, graph);
        probe.SetEnabled(true);
        value = stampOf(8);
        drawGraphFrame(scene, graph);
        EXPECT_FALSE(probe.hadPrevious);
        EXPECT_EQ(probe.readback->Read<float>(1).front(), cleared);
        value = stampOf(9);
        drawGraphFrame(scene, graph);
        EXPECT_TRUE(probe.hadPrevious);
        EXPECT_EQ(probe.readback->Read<float>(1).front(), stampOf(8));
    }
}

TEST_F(VkWindowTest, AFrameGraphPassReadsThePreviousFramesImage) {
    expectEachFrameSeesThePreviousOne(VkEnvironment::scene(), kor::Image::Format::eR32_SFLOAT, 0.f,
                                      [](const int frame) { return static_cast<float>(frame + 1); });
}

TEST_F(VkWindowTest, AFrameGraphPassReadsThePreviousFramesDepth) {
    // Depth must stay in [0, 1]; the history of a depth image starts at the far plane.
    expectEachFrameSeesThePreviousOne(VkEnvironment::scene(), kor::Image::Format::eD32_SFLOAT, 1.f,
                                      [](const int frame) { return 0.1f * static_cast<float>(frame + 1) / 2.f; });
}

// The graph times every pass: GPU time from a timer scope around what it recorded (arriving once the
// frame is done, through CommandBuffer::OnTimings), CPU time for its recording and its Prepare.
TEST_F(VkWindowTest, AFrameGraphTimesEachPass) {
    auto& scene = VkEnvironment::scene();
    kor::FrameGraph graph;
    float value = 1.f;
    graph.Add<StampPass>(kor::Image::Format::eR32_SFLOAT, value);
    graph.Add<ProbePass>();
    for (int frame = 0; frame < 8; ++frame) drawGraphFrame(scene, graph);

    const auto timings = graph.PassTimings();
    ASSERT_EQ(timings.size(), 2u);
    EXPECT_EQ(timings[0].name, "Stamp");
    EXPECT_EQ(timings[1].name, "Probe");
    for (const auto& timing : timings) {
        EXPECT_TRUE(timing.gpuMeasured) << timing.name << ": no GPU time arrived";
        EXPECT_GT(timing.gpuMs, 0.0) << timing.name;
        EXPECT_GT(timing.recordMs, 0.0) << timing.name;
    }
    EXPECT_NEAR(graph.Timing().gpuMs, timings[0].gpuMs + timings[1].gpuMs, 1e-9);
}

// ---- frame graph: memory, usage, rebuilds, CPU passes -------------------------------------

namespace {
    // A pass assembled from lambdas, counting how often it is initialized.
    class LambdaPass final : public kor::RenderPass {
    public:
        explicit LambdaPass(std::string name) : RenderPass(std::move(name)) {}
        void Setup(kor::PassBuilder& b) override { if (setup) setup(b); }
        void Initialize(const kor::PassResources& r) override { ++initializations; if (initialize) initialize(r); }
        void Record(kor::CommandBuffer& cb) const override { if (record) record(cb); }
        using RenderPass::RequestInitialize;

        std::function<void(kor::PassBuilder&)> setup;
        std::function<void(const kor::PassResources&)> initialize;
        std::function<void(kor::CommandBuffer&)> record;
        int initializations = 0;
    };

    kor::Resource<kor::Buffer> makeReadback() {
        return kor::Buffer::RawBuilder{}
            .SetRawSize(static_cast<kor::i64>(sizeof(float)))
            .SetUsage(kor::Buffer::Usage::eTransferDst)
            .SetType(kor::Buffer::Type::eReadback)
            .Build();
    }

    // Clears a new image to `value`, and a later pass copies one texel of it out.
    struct FillAndRead {
        LambdaPass* fill;
        LambdaPass* read;
        kor::Resource<kor::Buffer> readback;
    };
    FillAndRead addFillAndRead(kor::FrameGraph& graph, const std::string& image, const float value) {
        FillAndRead out{.readback = makeReadback()};
        auto target = std::make_shared<kor::ResourceRef<const kor::Image>>();
        out.fill = &graph.Add<LambdaPass>("Fill " + image);
        out.fill->setup = [=](kor::PassBuilder& b) { b.Create(image, {.format = kor::Image::Format::eR32_SFLOAT,
                                                                      .usage = kor::Image::Usage::eTransferDst}); };
        out.fill->initialize = [=](const kor::PassResources& r) { *target = r.ImageNamed(image); };
        out.fill->record = [=](kor::CommandBuffer& cb) { cb.ClearColorImage(*target, kor::Vec4(value)); };
        out.read = &graph.Add<LambdaPass>("Read " + image);
        out.read->setup = [=](kor::PassBuilder& b) { b.Read(image, kor::Image::Usage::eTransferSrc).SideEffect(); };
        out.read->initialize = [=](const kor::PassResources& r) { *target = r.ImageNamed(image); };
        out.read->record = [=, readback = kor::ResourceRef<const kor::Buffer>(out.readback)](kor::CommandBuffer& cb) {
            cb.CopyImageToBuffer(*target, readback, kor::Copy{ .imageExtent = kor::IVec3(1, 1, 1) });
        };
        return out;
    }
}

// Two images of one shape, never needed at the same time, share one allocation — and each still
// holds what its own passes put there.
TEST_F(VkWindowTest, AFrameGraphSharesMemoryBetweenImagesNeverAliveTogether) {
    auto& scene = VkEnvironment::scene();
    kor::FrameGraph graph;
    auto a = addFillAndRead(graph, "a", 1.f);
    auto b = addFillAndRead(graph, "b", 2.f);

    drawGraphFrame(scene, graph);
    EXPECT_EQ(graph.Memory().resources, 2u);
    EXPECT_EQ(graph.Memory().allocations, 1u) << "'a' is done with before 'b' is made";
    EXPECT_GT(graph.Memory().unsharedBytes, graph.Memory().bytes);
    EXPECT_EQ(a.readback->Read<float>(1).front(), 1.f);
    EXPECT_EQ(b.readback->Read<float>(1).front(), 2.f);

    graph.SetAliasing(false);
    drawGraphFrame(scene, graph);
    EXPECT_EQ(graph.Memory().allocations, 2u);
    EXPECT_EQ(graph.Memory().unsharedBytes, graph.Memory().bytes);
    EXPECT_EQ(a.readback->Read<float>(1).front(), 1.f);
    EXPECT_EQ(b.readback->Read<float>(1).front(), 2.f);
}

// An image asked for by name gets memory of its own, so what the frame left in it is still there.
TEST_F(VkWindowTest, AFrameGraphImageAskedForByNameIsNotShared) {
    auto& scene = VkEnvironment::scene();
    kor::FrameGraph graph;
    const auto a = addFillAndRead(graph, "a", 1.f);   // kept: their passes copy into these
    const auto b = addFillAndRead(graph, "b", 2.f);
    drawGraphFrame(scene, graph);
    ASSERT_EQ(graph.Memory().allocations, 1u);

    EXPECT_TRUE(graph.ImageNamed("a").Alive());
    drawGraphFrame(scene, graph);
    EXPECT_EQ(graph.Memory().allocations, 2u);
    EXPECT_NE(graph.ImageNamed("a").Get(), graph.ImageNamed("b").Get());
}

// The image is made with the usage of every pass that touches it, not only its creator's.
TEST_F(VkWindowTest, AFrameGraphImageHasTheUsageOfEveryPassUsingIt) {
    auto& scene = VkEnvironment::scene();
    kor::FrameGraph graph;
    auto& make = graph.Add<LambdaPass>("Make");
    make.setup = [](kor::PassBuilder& b) { b.Create("x", {.format = kor::Image::Format::eRGBA8_UNORM}); };
    auto& write = graph.Add<LambdaPass>("Write");
    write.setup = [](kor::PassBuilder& b) { b.Write("x", kor::Image::Usage::eTransferDst); };
    auto& read = graph.Add<LambdaPass>("Read");
    read.setup = [](kor::PassBuilder& b) { b.Read("x", kor::Image::Usage::eSampled | kor::Image::Usage::eTransferSrc).SideEffect(); };

    drawGraphFrame(scene, graph);
    const auto x = graph.ImageNamed("x");
    ASSERT_TRUE(x.Alive());
    const auto usage = x->UsageFlags();
    EXPECT_TRUE(usage & kor::Image::Usage::eTransferDst);
    EXPECT_TRUE(usage & kor::Image::Usage::eSampled);
    EXPECT_TRUE(usage & kor::Image::Usage::eTransferSrc);
}

TEST_F(VkWindowTest, AFrameGraphRefusesAnImageNoPassSaysHowItUses) {
    auto& scene = VkEnvironment::scene();
    kor::FrameGraph graph;
    auto& make = graph.Add<LambdaPass>("Make");
    make.setup = [](kor::PassBuilder& b) { b.Create("x", {.format = kor::Image::Format::eRGBA8_UNORM}); };
    auto& read = graph.Add<LambdaPass>("Read");
    read.setup = [](kor::PassBuilder& b) { b.Read("x").SideEffect(); };
    drawGraphFrame(scene, graph);
    EXPECT_TRUE(graph.Schedule().empty());
    EXPECT_EQ(make.initializations, 0);
}

// A rebuild initializes again only the passes whose resources it changed.
TEST_F(VkWindowTest, AFrameGraphInitializesAgainOnlyThePassesWhoseResourcesChanged) {
    auto& scene = VkEnvironment::scene();
    kor::FrameGraph graph;
    auto pair = addFillAndRead(graph, "a", 1.f);
    auto& user = graph.Add<LambdaPass>("Uses the import");
    user.setup = [](kor::PassBuilder& b) { b.Read("imported").SideEffect(); };
    auto first = makeReadback();
    graph.Import("imported", kor::ResourceRef<const kor::Buffer>(first));

    drawGraphFrame(scene, graph);
    EXPECT_EQ(pair.fill->initializations, 1);
    EXPECT_EQ(pair.read->initializations, 1);
    EXPECT_EQ(user.initializations, 1);

    // Nothing changed: nothing is initialized again, and the images are the ones there were.
    graph.Invalidate();
    drawGraphFrame(scene, graph);
    EXPECT_EQ(pair.fill->initializations, 1);
    EXPECT_EQ(user.initializations, 1);

    // The import now means another buffer: only the pass using it.
    auto second = makeReadback();
    graph.Import("imported", kor::ResourceRef<const kor::Buffer>(second));
    drawGraphFrame(scene, graph);
    EXPECT_EQ(pair.fill->initializations, 1);
    EXPECT_EQ(pair.read->initializations, 1);
    EXPECT_EQ(user.initializations, 2);

    // Asked for explicitly.
    pair.fill->RequestInitialize();
    drawGraphFrame(scene, graph);
    EXPECT_EQ(pair.fill->initializations, 2);
    EXPECT_EQ(pair.read->initializations, 1);
    EXPECT_EQ(pair.readback->Read<float>(1).front(), 1.f);
}

namespace {
    // Writes this frame's number into a buffer the CPU writes and the GPU reads.
    class CountFrames final : public kor::CpuPass {
    public:
        CountFrames() : CpuPass("Count frames") {}
        void Setup(kor::PassBuilder& b) override {
            b.Create("count", kor::BufferDesc{.size = sizeof(float), .usage = kor::Buffer::Usage::eTransferSrc,
                                              .type = kor::Buffer::Type::eDynamic});
        }
        void Initialize(const kor::PassResources& r) override { _count = r.WritableBufferNamed("count"); }
        void Prepare() override { ++_frame; }
        void Run() override {
            ranOn = std::this_thread::get_id();
            _count->Write(std::array<float, 1>{ static_cast<float>(_frame) }, 0);
            value = _frame;
        }
        std::atomic<int> value = 0;
        std::thread::id ranOn;
    private:
        int _frame = 0;
        kor::ResourceRef<kor::Buffer> _count;
    };

    // Copies it out on the GPU, and notes what the CPU pass had got to when it recorded.
    class CopyCount final : public kor::RenderPass {
    public:
        explicit CopyCount(const CountFrames& counter) : RenderPass("Copy count"), _counter(counter) {}
        void Setup(kor::PassBuilder& b) override { b.Read("count", kor::Buffer::Usage::eTransferSrc).SideEffect(); }
        void Initialize(const kor::PassResources& r) override {
            _count = r.BufferNamed("count");
            readback = makeReadback();
        }
        void Record(kor::CommandBuffer& cb) const override {
            seenWhenRecording = _counter.value.load();
            cb.CopyBuffer(_count, readback, sizeof(float), 0, 0);
        }
        kor::Resource<kor::Buffer> readback;
        mutable std::atomic<int> seenWhenRecording = -1;
    private:
        const CountFrames& _counter;
        kor::ResourceRef<const kor::Buffer> _count;
    };
}

// A CPU pass runs off the main thread, before the GPU passes that use what it wrote record.
TEST_F(VkWindowTest, AFrameGraphCpuPassRunsBeforeTheGpuPassesThatUseIt) {
    auto& scene = VkEnvironment::scene();
    kor::FrameGraph graph;
    auto& counter = graph.Add<CountFrames>();
    auto& copy = graph.Add<CopyCount>(counter);

    for (int frame = 1; frame <= 5; ++frame) {
        drawGraphFrame(scene, graph);
        EXPECT_EQ(copy.seenWhenRecording, frame) << "the GPU pass recorded before the CPU pass had run";
        EXPECT_EQ(copy.readback->Read<float>(1).front(), static_cast<float>(frame));
    }
    EXPECT_NE(counter.ranOn, std::this_thread::get_id()) << "a CPU pass runs on the background pool";
    ASSERT_EQ(graph.PassTimings().size(), 2u);
    EXPECT_FALSE(graph.PassTimings()[0].gpuMeasured) << "a CPU pass has no GPU time";
}

// ---- async compute ----------------------------------------------------------------------------

namespace {
    // fill (graphics) -> copy (async) -> read (graphics), with a graphics pass beside the copy that
    // samples the same source: both only read it as a transfer source, so they may overlap.
    struct AsyncChain {
        LambdaPass* fill;
        LambdaPass* copy;
        LambdaPass* beside;
        LambdaPass* read;
        kor::Resource<kor::Buffer> readback;
        kor::Resource<kor::Buffer> besideReadback;
    };
    AsyncChain addAsyncChain(kor::FrameGraph& graph, const float value) {
        AsyncChain out{.readback = makeReadback(), .besideReadback = makeReadback()};
        auto a = std::make_shared<kor::ResourceRef<const kor::Image>>();
        auto b = std::make_shared<kor::ResourceRef<const kor::Image>>();
        const kor::ImageDesc desc{.format = kor::Image::Format::eR32_SFLOAT, .usage = kor::Image::Usage::eTransferDst};
        out.fill = &graph.Add<LambdaPass>("Fill");
        out.fill->setup = [=](kor::PassBuilder& b) { b.Create("a", desc); };
        out.fill->initialize = [=](const kor::PassResources& r) { *a = r.ImageNamed("a"); };
        out.fill->record = [=](kor::CommandBuffer& cb) { cb.ClearColorImage(*a, kor::Vec4(value)); };
        out.copy = &graph.Add<LambdaPass>("Copy");
        out.copy->setup = [=](kor::PassBuilder& p) { p.Read("a", kor::Image::Usage::eTransferSrc).Create("b", desc).AsyncCompute(); };
        out.copy->initialize = [=](const kor::PassResources& r) { *a = r.ImageNamed("a"); *b = r.ImageNamed("b"); };
        out.copy->record = [=](kor::CommandBuffer& cb) { cb.CopyImage(*a, *b); };
        out.beside = &graph.Add<LambdaPass>("Beside");
        out.beside->setup = [=](kor::PassBuilder& p) { p.Read("a", kor::Image::Usage::eTransferSrc).SideEffect(); };
        out.beside->initialize = [=](const kor::PassResources& r) { *a = r.ImageNamed("a"); };
        out.beside->record = [=, readback = kor::ResourceRef<const kor::Buffer>(out.besideReadback)](kor::CommandBuffer& cb) {
            cb.CopyImageToBuffer(*a, readback, kor::Copy{ .imageExtent = kor::IVec3(1, 1, 1) });
        };
        out.read = &graph.Add<LambdaPass>("Read");
        out.read->setup = [=](kor::PassBuilder& p) { p.Read("b", kor::Image::Usage::eTransferSrc).SideEffect(); };
        out.read->initialize = [=](const kor::PassResources& r) { *b = r.ImageNamed("b"); };
        out.read->record = [=, readback = kor::ResourceRef<const kor::Buffer>(out.readback)](kor::CommandBuffer& cb) {
            cb.CopyImageToBuffer(*b, readback, kor::Copy{ .imageExtent = kor::IVec3(1, 1, 1) });
        };
        return out;
    }
}

// A pass on the async compute queue gets what the graphics queue made before it, and the graphics
// pass after it gets what it made — frame after frame, with frames in flight overlapping.
TEST_F(VkWindowTest, AFrameGraphRunsAnAsyncComputePassBetweenGraphicsPasses) {
    auto& scene = VkEnvironment::scene();
    kor::FrameGraph graph;
    auto chain = addAsyncChain(graph, 4.f);

    drawGraphFrame(scene, graph);
    ASSERT_EQ(graph.Schedule().size(), 4u);
    for (const auto& scheduled : graph.Schedule())
        EXPECT_EQ(scheduled.async, scheduled.name == "Copy") << scheduled.name;
    EXPECT_EQ(chain.readback->Read<float>(1).front(), 4.f);
    EXPECT_EQ(chain.besideReadback->Read<float>(1).front(), 4.f);
    if (!kor::Context::SupportsAsyncCompute()) GTEST_LOG_(INFO) << "no async compute queue here: ran in order";
    // KORAL_ASYNC_COMPUTE=separate-family runs this on a compute queue family of its own, as on AMD.
    if (const char* forced = std::getenv("KORAL_ASYNC_COMPUTE"); forced && std::string(forced) == "separate-family")
        EXPECT_TRUE(kor::Context::AsyncComputeIsSeparateFamily());

    // Without waiting in between: the copy of one frame must not run under the last one's reads.
    auto& overlay = static_cast<OverlayScene&>(scene);
    overlay.extraGraph = &graph;
    for (int frame = 0; frame < 6; ++frame) kor::App::Current().Frame();
    overlay.extraGraph = nullptr;
    kor::Context::Scheduler().WaitIdle();
    EXPECT_EQ(chain.readback->Read<float>(1).front(), 4.f);
}

// What an async pass uses never shares memory: the order says nothing about when it is in use
// relative to the graphics queue.
TEST_F(VkWindowTest, AFrameGraphDoesNotShareWhatAnAsyncPassUses) {
    auto& scene = VkEnvironment::scene();
    kor::FrameGraph graph;
    auto chain = addAsyncChain(graph, 2.f);
    auto after = addFillAndRead(graph, "later", 5.f);   // same shape, made after 'a' and 'b' are done with
    after.fill->setup = [fill = after.fill->setup](kor::PassBuilder& b) { fill(b); b.Read("b", kor::Image::Usage::eTransferSrc); };

    drawGraphFrame(scene, graph);
    EXPECT_EQ(graph.Memory().resources, 3u);
    EXPECT_EQ(graph.Memory().allocations, 3u) << "'a' and 'b' are used by the async pass";
    EXPECT_EQ(chain.readback->Read<float>(1).front(), 2.f);
    EXPECT_EQ(after.readback->Read<float>(1).front(), 5.f);
}

// A pass taken out of the graph is gone: not run, not scheduled, and what was left runs as before.
TEST_F(VkWindowTest, APassCanBeTakenOutOfTheGraph) {
    auto& scene = VkEnvironment::scene();
    kor::FrameGraph graph;
    auto first = addFillAndRead(graph, "first", 3.f);
    auto second = addFillAndRead(graph, "second", 8.f);
    drawGraphFrame(scene, graph);
    ASSERT_EQ(graph.Schedule().size(), 4u);

    EXPECT_TRUE(graph.Remove(*second.read));
    EXPECT_TRUE(graph.Remove(*second.fill));
    kor::FrameGraph other;
    EXPECT_FALSE(other.Remove(*first.fill)) << "not its pass";
    drawGraphFrame(scene, graph);
    ASSERT_EQ(graph.Schedule().size(), 2u);
    EXPECT_EQ(graph.Passes().size(), 2u);
    EXPECT_EQ(first.readback->Read<float>(1).front(), 3.f);
}

// A pass whose resources come from a set that changes as the graph runs — the images each entity holds. The set's
// name orders the passes like any resource's; adding and taking out members rebuilds nothing, and every member is
// synchronised by the commands that use it.
TEST_F(VkWindowTest, AResourceSetOrdersPassesAndChangesWithoutRebuildingTheGraph) {
    auto& scene = VkEnvironment::scene();
    const auto since = kor::log::LastSequence();
    kor::FrameGraph graph;
    auto set = std::make_shared<kor::ResourceSet>();
    graph.ImportSet("entity images", set);

    // An entity: a one-texel image, and where what it holds is read back to. Changed only between frames.
    struct Entities {
        std::vector<kor::Resource<kor::Image>> images;
        std::map<const kor::Image*, kor::Resource<kor::Buffer>> readbacks;
    };
    auto entities = std::make_shared<Entities>();
    const auto add = [&] {
        auto image = kor::Image::Builder{}.SetFormat(kor::Image::Format::eR32_SFLOAT).SetExtent(kor::UVec2(1, 1))
            .SetUsage(kor::Image::Usage::eTransferDst | kor::Image::Usage::eTransferSrc).Build();
        entities->readbacks.emplace(image.Get(), makeReadback());
        set->Add(kor::ResourceRef<const kor::Image>(image));
        entities->images.push_back(std::move(image));
    };
    const auto remove = [&](const std::size_t index) {
        set->Remove(entities->images[index].Get());
        entities->readbacks.erase(entities->images[index].Get());
        entities->images.erase(entities->images.begin() + static_cast<std::ptrdiff_t>(index));
    };

    // Added reader first: nothing but the set's name puts the writer ahead of it.
    auto& read = graph.Add<LambdaPass>("Read entities");
    auto& write = graph.Add<LambdaPass>("Write entities");
    auto members = std::make_shared<const kor::ResourceSet*>(nullptr);
    write.setup = [](kor::PassBuilder& b) { b.Write("entity images"); };
    write.initialize = [members](const kor::PassResources& r) { *members = r.SetNamed("entity images"); };
    write.record = [members, set](kor::CommandBuffer& cb) {
        const auto& images = (*members)->Images();
        for (std::size_t i = 0; i < images.size(); ++i) cb.ClearColorImage(images[i], kor::Vec4(static_cast<float>(i + 1)));
        set->Clear();   // refused: the set may not change while passes record with it
    };
    read.setup = [](kor::PassBuilder& b) { b.Read("entity images").SideEffect(); };
    read.initialize = [members](const kor::PassResources& r) { *members = r.SetNamed("entity images"); };
    read.record = [members, entities](kor::CommandBuffer& cb) {
        for (const auto& image : (*members)->Images())
            cb.CopyImageToBuffer(image, kor::ResourceRef<const kor::Buffer>(entities->readbacks.at(image.Get())),
                                 kor::Copy{ .imageExtent = kor::IVec3(1, 1, 1) });
    };

    const auto expectEachHoldsItsPlace = [&](const char* when) {
        for (std::size_t i = 0; i < entities->images.size(); ++i)
            EXPECT_EQ(entities->readbacks.at(entities->images[i].Get())->Read<float>(1).front(), static_cast<float>(i + 1))
                << "entity " << i << ", " << when;
    };

    for (int i = 0; i < 3; ++i) add();
    drawGraphFrame(scene, graph);
    ASSERT_EQ(graph.Schedule().size(), 2u);
    EXPECT_EQ(graph.Schedule()[0].name, "Write entities");
    EXPECT_EQ(graph.Schedule()[1].name, "Read entities");
    EXPECT_EQ(set->Size(), 3u) << "a change while the passes recorded went through";
    expectEachHoldsItsPlace("at first");

    // Entities come and go between frames: the passes see the new members, and nothing is set up again.
    const auto generation = set->Generation();
    remove(0);
    add();
    add();
    EXPECT_NE(set->Generation(), generation);
    drawGraphFrame(scene, graph);
    drawGraphFrame(scene, graph);
    EXPECT_EQ(set->Size(), 4u);
    expectEachHoldsItsPlace("after some came and went");
    EXPECT_EQ(write.initializations, 1);
    EXPECT_EQ(read.initializations, 1);

    for (const auto& record : kor::log::HistorySince(since)) {
        EXPECT_EQ(record.message.find("VUID"), std::string::npos) << record.message;
        EXPECT_EQ(record.message.find("SYNC-HAZARD"), std::string::npos) << record.message;
    }
}

// A command buffer handed to Execute on the async queue: work after it waits for the token it
// returned, and the token is its own — ready once the GPU has run it.
TEST_F(VkWindowTest, AnAsyncComputeCommandBufferIsWaitedForByTheTokenItReturns) {
    auto& scheduler = kor::Context::Scheduler();
    // Used on both queues: shared, for a device whose async queue is a family of its own.
    auto source = kor::Buffer::RawBuilder{}.SetRawSize(sizeof(float))
        .SetUsage(kor::Buffer::Usage::eTransferDst | kor::Buffer::Usage::eTransferSrc).SetSharedAcrossQueues(true).Build();
    auto readback = makeReadback();

    auto fill = kor::CommandBuffer::Create(kor::CommandBuffer::Usage::eAsyncCompute);
    fill->Begin();
    fill->FillBuffer(source, std::array{9.f});
    const kor::Token filled = scheduler.Execute(std::move(fill));

    auto copy = kor::CommandBuffer::Create(kor::CommandBuffer::Usage::eGraphics);
    copy->Begin();
    copy->CopyBuffer(source, readback);
    const kor::Token copied = scheduler.Execute(std::move(copy), {.after = {filled}});

    drawFrame(VkEnvironment::scene());
    scheduler.WaitIdle();
    EXPECT_TRUE(filled.Ready());
    EXPECT_TRUE(copied.Ready());
    EXPECT_EQ(readback->Read<float>(1).front(), 9.f);
}

TEST_F(VkWindowTest, AFrameGraphRefusesACpuPassFedByTheGpu) {
    auto& scene = VkEnvironment::scene();
    kor::FrameGraph graph;
    auto& gpu = graph.Add<LambdaPass>("GPU");
    gpu.setup = [](kor::PassBuilder& b) {
        b.Create("made", kor::BufferDesc{.size = 4, .usage = kor::Buffer::Usage::eStorage, .type = kor::Buffer::Type::eDynamic});
    };
    class Reader final : public kor::CpuPass {
    public:
        Reader() : CpuPass("CPU") {}
        void Setup(kor::PassBuilder& b) override { b.Read("made").SideEffect(); }
        void Run() override {}
    };
    graph.Add<Reader>();
    drawGraphFrame(scene, graph);
    EXPECT_TRUE(graph.Schedule().empty());
}

// Record cannot change the graph: the change is refused rather than racing the other passes.
TEST_F(VkWindowTest, AFrameGraphRefusesChangesWhilePassesRecord) {
    auto& scene = VkEnvironment::scene();
    kor::FrameGraph graph;
    auto pair = addFillAndRead(graph, "a", 1.f);
    pair.read->record = [&, copy = pair.read->record](kor::CommandBuffer& cb) {
        copy(cb);
        graph.Invalidate();
        pair.fill->SetEnabled(false);
    };
    drawGraphFrame(scene, graph);
    drawGraphFrame(scene, graph);
    EXPECT_TRUE(pair.fill->Enabled());
    EXPECT_EQ(pair.fill->initializations, 1) << "the graph was rebuilt from inside Record";
    EXPECT_EQ(pair.readback->Read<float>(1).front(), 1.f);
}

// ---- more than one scene -----------------------------------------------------------------------
//
// An application runs any number of scenes, each in its own window with its own input, clock and
// frame graph; Navigator changes what a window shows.

namespace {
    // What scenes did, kept outside them: most of these outlive the scene they count.
    std::map<std::string, int>& events() {
        static std::map<std::string, int> counts;
        return counts;
    }

    // Paints its own window one shade of red through its own frame graph, and copies a texel back.
    class PaintScene final : public kor::Scene {
    public:
        explicit PaintScene(const float red = 1.f) : red(red) {}
        explicit PaintScene(const kor::SceneArgs& arguments) : red(static_cast<float>(arguments.Number("red", 1.0))) {}

        void Initialize() override {
            ++events()["initialize " + Name()];
            readback = kor::Buffer::RawBuilder{}.SetRawSize(4).SetUsage(kor::Buffer::Usage::eTransferDst)
                .SetType(kor::Buffer::Type::eReadback).Build();
            auto target = std::make_shared<kor::ResourceRef<const kor::Image>>();
            auto& paint = Graph().Add<LambdaPass>("Paint");
            paint.setup = [](kor::PassBuilder& b) { b.Write(kor::FrameGraph::Screen, kor::Image::Usage::eTransferDst); };
            paint.initialize = [target](const kor::PassResources& r) { *target = r.ImageNamed(kor::FrameGraph::Screen); };
            paint.record = [target, red = red](kor::CommandBuffer& cb) { cb.ClearColorImage(*target, kor::Vec4(red, 0.f, 0.f, 1.f)); };
            auto& read = Graph().Add<LambdaPass>("Read");
            read.setup = [](kor::PassBuilder& b) { b.Read(kor::FrameGraph::Screen, kor::Image::Usage::eTransferSrc).SideEffect(); };
            read.initialize = [target](const kor::PassResources& r) { *target = r.ImageNamed(kor::FrameGraph::Screen); };
            read.record = [target, out = kor::ResourceRef<const kor::Buffer>(readback)](kor::CommandBuffer& cb) {
                cb.CopyImageToBuffer(*target, out, kor::Copy{ .imageExtent = kor::IVec3(1, 1, 1) });
            };
        }
        void Update() override {
            ++updates;
            currentInUpdate = kor::Scene::Current();
            extentInUpdate = Window::Extent();
        }
        void OnSuspend() override { ++events()["suspend " + Name()]; }
        void OnResume() override { ++events()["resume " + Name()]; }
        void Shutdown() override { ++events()["shutdown " + Name()]; }

        [[nodiscard]] kor::u8 Red() const {
            const auto texel = readback->Read<kor::u8>(4);
            return SceneWindow().DefaultFramebuffer()->ColorImage(0)->IsBgrOrder() ? texel[2] : texel[0];
        }

        float red;
        int updates = 0;
        kor::Scene* currentInUpdate = nullptr;
        kor::UVec2 extentInUpdate {0};
        kor::Resource<kor::Buffer> readback;
    };

    const kor::WindowSettings kSmall { .title = "Koral second scene", .extent = {160, 120}, .vsync = false };

    void settle() {
        kor::App::Current().Frame();
        kor::Context::Scheduler().WaitIdle();
    }
}

TEST_F(VkWindowTest, TwoScenesEachDrawTheirOwnWindow) {
    auto& app = VkEnvironment::app();
    auto* second = app.Open<PaintScene>(kSmall, 1.f);
    ASSERT_NE(second, nullptr);
    ASSERT_EQ(app.Scenes().size(), 2u);
    EXPECT_EQ(app.Scenes()[1], second);
    EXPECT_NE(&second->SceneWindow(), &VkEnvironment::scene().SceneWindow());
    EXPECT_NE(&second->SceneInput(), &VkEnvironment::scene().SceneInput());

    for (int frame = 0; frame < 3; ++frame) settle();
    if (second->SceneWindow().IsPaused()) GTEST_SKIP() << "the compositor never sized the second window";
    EXPECT_EQ(second->Red(), 255) << "the scene drew into a window, and it was not its own";
    EXPECT_GE(second->updates, 1);

    events().clear();
    app.Close(*second);
    settle();
    EXPECT_EQ(app.Scenes().size(), 1u);
    EXPECT_EQ(events()["shutdown Koral second scene"], 1);
}

// Inside a scene's hooks the scene is current, and Window:: is its own window.
TEST_F(VkWindowTest, EachSceneIsCurrentInItsOwnHooks) {
    auto& app = VkEnvironment::app();
    auto* second = app.Open<PaintScene>(kSmall, 0.5f);
    ASSERT_NE(second, nullptr);
    for (int frame = 0; frame < 2; ++frame) settle();
    if (second->SceneWindow().IsPaused()) GTEST_SKIP() << "the compositor never sized the second window";
    EXPECT_EQ(second->currentInUpdate, second);
    EXPECT_EQ(second->extentInUpdate, second->SceneWindow().Extent());
    EXPECT_NE(second->extentInUpdate, VkEnvironment::scene().SceneWindow().Extent()) << "the windows differ in size";
    app.Close(*second);
    settle();
}

namespace {
    kor::Task<void> notingTheSceneOnTheBackground(kor::Scene** seen, std::thread::id* thread) {
        co_await kor::Context::SwitchToBackgroundThread();
        *thread = std::this_thread::get_id();
        *seen = kor::Scene::Current();
    }
}

// A coroutine started in a scene is still in it after moving thread: `Window::` in a loader means the
// window of the scene that started it.
TEST_F(VkWindowTest, ACoroutineStartedInASceneResumesInIt) {
    kor::Scene* seen = nullptr;
    std::thread::id thread;
    auto task = notingTheSceneOnTheBackground(&seen, &thread);   // started with the fixture's scene current
    task.Wait();
    EXPECT_NE(thread, std::this_thread::get_id());
    EXPECT_EQ(seen, &VkEnvironment::scene());
}

// Keys go to the scene whose window they happen in, and to no other.
TEST_F(VkWindowTest, KeysGoToTheSceneWhoseWindowTheyHappenIn) {
    auto& app = VkEnvironment::app();
    auto* second = app.Open<PaintScene>(kSmall, 1.f);
    ASSERT_NE(second, nullptr);
    settle();

    kor::Input::Callbacks::KeyCallback(*second->SceneWindow(), GLFW_KEY_J, 0, GLFW_PRESS, 0);
    EXPECT_TRUE(second->SceneInput().IsKeyPressed(kor::Key::eJ));
    EXPECT_FALSE(VkEnvironment::scene().SceneInput().IsKeyPressed(kor::Key::eJ));
    {
        kor::detail::SceneScope scope(second);
        EXPECT_TRUE(kor::Scene::Input::IsKeyPressed(kor::Key::eJ)) << "Input:: is the current scene's";
    }
    settle();
    EXPECT_TRUE(second->SceneInput().IsKeyHeld(kor::Key::eJ)) << "a frame later the press is a hold";
    kor::Input::Callbacks::KeyCallback(*second->SceneWindow(), GLFW_KEY_J, 0, GLFW_RELEASE, 0);
    app.Close(*second);
    settle();
}

// What a window shows changes after the frame: replaced, pushed over, popped back.
TEST_F(VkWindowTest, NavigationReplacesPushesAndPops) {
    auto& app = VkEnvironment::app();
    app.Register<PaintScene>("First");
    app.Register<PaintScene>("Second");
    events().clear();

    auto* first = app.Open("First", kSmall, {{"red", "1"}});
    ASSERT_NE(first, nullptr);
    GLFWwindow* window = *first->SceneWindow();
    EXPECT_EQ(static_cast<PaintScene*>(first)->red, 1.f) << "the arguments reach the scene";

    {   // From inside the scene, as a scene would.
        kor::detail::SceneScope scope(first);
        kor::Navigator::Push("Second", {{"red", "0.5"}});
    }
    EXPECT_EQ(app.Scenes().back(), first) << "nothing changes until the frame is over";
    settle();
    auto* second = app.Scenes().back();
    EXPECT_NE(second, first);
    EXPECT_EQ(second->Name(), "Second");
    EXPECT_EQ(*second->SceneWindow(), window) << "pushed over it in the same window";
    EXPECT_EQ(events()["suspend First"], 1);
    const int firstUpdates = static_cast<PaintScene*>(first)->updates;
    settle();
    EXPECT_EQ(static_cast<PaintScene*>(first)->updates, firstUpdates) << "a covered scene is not updated";

    app.Pop(*second);
    settle();
    EXPECT_EQ(app.Scenes().back(), first);
    EXPECT_EQ(events()["shutdown Second"], 1);
    EXPECT_EQ(events()["resume First"], 1);

    app.Replace(*first, "Second");
    settle();
    EXPECT_EQ(events()["shutdown First"], 1);
    EXPECT_EQ(app.Scenes().back()->Name(), "Second");
    EXPECT_EQ(*app.Scenes().back()->SceneWindow(), window) << "replaced in the same window";

    app.Close(*app.Scenes().back());
    settle();
    EXPECT_EQ(app.Scenes().size(), 1u);
    EXPECT_EQ(events()["shutdown Second"], 2);
}

// A minimized window's scene is not updated or drawn — it has nowhere to draw.
TEST_F(VkWindowTest, AMinimizedWindowsSceneIsNotUpdated) {
    auto& app = VkEnvironment::app();
    auto* second = app.Open<PaintScene>(kSmall, 1.f);
    ASSERT_NE(second, nullptr);
    settle();
    second->SceneWindow().Pause();
    const int updates = second->updates;
    settle();
    EXPECT_EQ(second->updates, updates);
    EXPECT_FALSE(second->SceneWindow().IsShownThisFrame());
    second->SceneWindow().Unpause();
    settle();
    if (!second->SceneWindow().IsPaused()) EXPECT_EQ(second->updates, updates + 1);
    app.Close(*second);
    settle();
}

namespace {
    class SteppedScene final : public kor::Scene {
    public:
        void FixedUpdate() override {
            ++steps;
            ++stepsThisFrame;
            stepSawTheStep = stepSawTheStep && Time::FrameTime() == Time::FixedDeltaTime() && Time::Get().InFixedStep();
        }
        void Update() override {
            mostStepsInAFrame = std::max(mostStepsInAFrame, stepsThisFrame);
            stepsThisFrame = 0;
            updateSawTheFrame = updateSawTheFrame && !Time::Get().InFixedStep();
        }
        int steps = 0, stepsThisFrame = 0, mostStepsInAFrame = 0;
        bool stepSawTheStep = true, updateSawTheFrame = true;
    };
}

// FixedUpdate runs once per fixed step of the scene's own time: never with its time stopped, at most
// Time::MaxFixedSteps a frame however far behind, and with FrameTime() being the step inside it.
TEST_F(VkWindowTest, FixedStepsFollowTheScenesOwnTime) {
    auto& app = VkEnvironment::app();
    auto* scene = app.Open<SteppedScene>(kSmall);
    ASSERT_NE(scene, nullptr);
    settle();
    if (scene->SceneWindow().IsPaused()) GTEST_SKIP() << "the compositor never sized the window";

    // A step far longer than the test: none is due.
    scene->SceneTime().SetFixedDeltaTime(1000.f);
    for (int frame = 0; frame < 3; ++frame) settle();
    EXPECT_EQ(scene->steps, 0);

    // A step far shorter than a frame: every frame is behind, and catches up only so far.
    scene->SceneTime().SetFixedDeltaTime(1e-4f);
    for (int frame = 0; frame < 4; ++frame) settle();
    EXPECT_GT(scene->steps, 0);
    EXPECT_LE(scene->mostStepsInAFrame, static_cast<int>(kor::Time::MaxFixedSteps));
    EXPECT_TRUE(scene->stepSawTheStep) << "inside FixedUpdate, FrameTime() is the step";
    EXPECT_TRUE(scene->updateSawTheFrame);
    EXPECT_GE(scene->SceneTime().FixedStepFraction(), 0.f);
    EXPECT_LE(scene->SceneTime().FixedStepFraction(), 1.f);

    // Its time stopped: no step, whatever the rate — and no other scene's clock is touched.
    scene->SceneTime().SetTimeScale(0.f);
    const int steps = scene->steps;
    for (int frame = 0; frame < 3; ++frame) settle();
    EXPECT_EQ(scene->steps, steps);
    EXPECT_EQ(VkEnvironment::scene().SceneTime().TimeScale(), 1.f);

    app.Close(*scene);
    settle();
}

// A window presents in the first of the formats it asks for that the display offers, and its image
// says so: the image format of the same size and encoding, with the channel order beside it.
TEST_F(VkWindowTest, AWindowPresentsInTheFirstFormatItAsksForThatTheDisplayOffers) {
    auto& app = VkEnvironment::app();
    const auto& main = VkEnvironment::scene().SceneWindow();
    EXPECT_EQ(main.PixelFormat(), kor::Window::Format::eBGRA8_UNORM) << "the default, and what every desktop display offers";
    const auto mainImage = main.DefaultFramebuffer()->ColorImage(0);
    EXPECT_EQ(mainImage->PixelFormat(), kor::Image::Format::eRGBA8_UNORM);
    EXPECT_TRUE(mainImage->IsBgrOrder());

    kor::WindowSettings settings = kSmall;
    settings.formats = { kor::Window::Format::eRGBA8_SRGB, kor::Window::Format::eBGRA8_SRGB };
    auto* scene = app.Open<PaintScene>(settings, 1.f);
    ASSERT_NE(scene, nullptr);
    for (int frame = 0; frame < 2; ++frame) settle();
    const auto format = scene->SceneWindow().PixelFormat();
    EXPECT_TRUE(format == kor::Window::Format::eRGBA8_SRGB || format == kor::Window::Format::eBGRA8_SRGB)
        << "one of those asked for, and every display offers the second";
    const auto image = scene->SceneWindow().DefaultFramebuffer()->ColorImage(0);
    EXPECT_EQ(image->PixelFormat(), kor::Image::Format::eRGBA8_SRGB);
    EXPECT_EQ(image->IsBgrOrder(), format == kor::Window::Format::eBGRA8_SRGB);
    if (!scene->SceneWindow().IsPaused()) EXPECT_EQ(scene->Red(), 255) << "drawn into, and read back red first";
    app.Close(*scene);
    settle();
}

// A copy moves bytes, so one between a BGRA window image and an RGBA image would swap red and blue:
// refused, pointing at a blit, which converts.
TEST_F(VkWindowTest, CopyingAWindowsBgraImageToAnRgbaOneIsRefused) {
    const auto screen = VkEnvironment::scene().SceneWindow().DefaultFramebuffer()->ColorImage(0);
    ASSERT_TRUE(screen->IsBgrOrder());
    auto copy = kor::Image::Builder().SetExtent(screen->Extent()).SetFormat(screen->PixelFormat())
        .SetUsage(kor::Image::Usage::eTransferDst).Build();
    auto cb = kor::CommandBuffer::Create(kor::CommandBuffer::Usage::eGraphics);
    cb->Begin();
    cb->CopyImage(screen, copy);
    EXPECT_FALSE(cb->Errors().empty());
    cb->Reset();
}

// ---- offscreen scenes ---------------------------------------------------------------------------

namespace {
    // Remembers what its input said in each Update, for the tests that feed it.
    class ListeningScene final : public kor::Scene {
    public:
        void Update() override {
            spacePressed = Input::IsKeyPressed(kor::Key::eSpace);
            spaceHeld = Input::IsKeyHeld(kor::Key::eSpace);
            spaceReleased = Input::IsKeyReleased(kor::Key::eSpace);
            leftPressed = Input::IsMouseButtonPressed(kor::MouseButton::eLeft);
            mouse = Input::MousePosition();
            delta = Input::MousePositionDelta();
            ++updates;
        }
        void OnResize(const kor::UVec2 extent) override { resizedTo = extent; }
        bool spacePressed = false, spaceHeld = false, spaceReleased = false, leftPressed = false;
        kor::Vec2 mouse{0.f}, delta{0.f};
        kor::UVec2 resizedTo{0, 0};
        int updates = 0;
    };
}

// An offscreen scene draws exactly as one in a window does — its passes write FrameGraph::Screen —
// and what it drew is its window's image, for anything else to show or read.
TEST_F(VkWindowTest, AnOffscreenSceneDrawsIntoItsOwnImage) {
    auto& app = VkEnvironment::app();
    auto* scene = app.OpenOffscreen<PaintScene>({.title = "Offscreen", .extent = {64, 48}}, 0.5f);
    ASSERT_NE(scene, nullptr);
    for (int frame = 0; frame < 2; ++frame) settle();

    const auto& window = scene->SceneWindow();
    EXPECT_TRUE(window.IsOffscreen());
    EXPECT_EQ(*window, nullptr) << "no OS window behind it";
    EXPECT_EQ(window.Extent(), kor::UVec2(64, 48));
    EXPECT_TRUE(window.IsShownThisFrame());
    ASSERT_TRUE(window.Image().Alive());
    EXPECT_EQ(window.Image()->Extent(), kor::UVec3(64, 48, 1));
    EXPECT_FALSE(window.Image()->IsBgrOrder());
    EXPECT_EQ(scene->currentInUpdate, scene);
    EXPECT_EQ(scene->extentInUpdate, kor::UVec2(64, 48)) << "Window:: is its own window";
    EXPECT_NEAR(scene->Red(), 128, 1) << "what it painted is in its image";
    EXPECT_EQ(std::ranges::count(app.Scenes(), scene), 1);

    app.Close(*scene);
    settle();
}

// Resized when asked — by the program, or by a view showing it — at the start of the next frame, and
// the scene gets OnResize as it would from a window dragged by hand.
TEST_F(VkWindowTest, AnOffscreenWindowIsResizedWhenAskedAndItsSceneHearsOfIt) {
    auto& app = VkEnvironment::app();
    auto* scene = app.OpenOffscreen<ListeningScene>({.extent = {64, 64}});
    ASSERT_NE(scene, nullptr);
    settle();
    const auto generation = scene->SceneWindow().Image()->Generation();

    scene->SceneWindow().Resize({100, 30});
    EXPECT_EQ(scene->SceneWindow().Extent(), kor::UVec2(64, 64)) << "not until the next frame";
    settle();
    EXPECT_EQ(scene->SceneWindow().Extent(), kor::UVec2(100, 30));
    EXPECT_EQ(scene->resizedTo, kor::UVec2(100, 30));
    EXPECT_EQ(scene->SceneWindow().Image()->Extent(), kor::UVec3(100, 30, 1));
    EXPECT_NE(scene->SceneWindow().Image()->Generation(), generation) << "a view holding it notices";

    app.Close(*scene);
    settle();
}

// Input fed to a scene arrives the way an OS window's does: pressed on the next frame, then held.
TEST_F(VkWindowTest, InputFedToAnOffscreenSceneArrivesAsAWindowsWould) {
    auto& app = VkEnvironment::app();
    auto* scene = app.OpenOffscreen<ListeningScene>({.extent = {64, 64}});
    ASSERT_NE(scene, nullptr);
    settle();

    auto& input = scene->SceneInput();
    input.FeedKey(kor::Key::eSpace, true);
    input.FeedMouseButton(kor::MouseButton::eLeft, true);
    input.FeedMousePosition({10.f, 20.f});
    input.FeedMouseDelta({3.f, -4.f});
    settle();
    EXPECT_TRUE(scene->spacePressed);
    EXPECT_TRUE(scene->leftPressed);
    EXPECT_EQ(scene->mouse, kor::Vec2(10.f, 20.f));
    EXPECT_EQ(scene->delta, kor::Vec2(3.f, -4.f));

    settle();
    EXPECT_FALSE(scene->spacePressed);
    EXPECT_TRUE(scene->spaceHeld) << "and then held, until it is fed up";
    EXPECT_EQ(scene->delta, kor::Vec2(0.f)) << "movement is per frame";

    input.ReleaseAll();
    settle();
    EXPECT_FALSE(scene->spaceHeld);
    EXPECT_TRUE(scene->spaceReleased) << "released, the frame after it was fed up";

    app.Close(*scene);
    settle();
}

// Actions and axes follow whatever they are bound to — keys, buttons, a gamepad — with a stick's dead
// zone taken off and a diagonal on the keyboard no faster than straight on.
TEST_F(VkWindowTest, ActionsAndAxesFollowTheKeysAndGamepadsTheyAreBoundTo) {
    class Player final : public kor::Scene {
    public:
        void Initialize() override {
            Input::Get().BindAction("Jump", {kor::Key::eSpace, kor::GamepadButton::eA});
            Input::Get().BindAxis("MoveX", {{kor::Key::eD, 1.f}, {kor::Key::eA, -1.f}, kor::GamepadAxis::eLeftX});
            Input::Get().BindAxis("MoveY", {{kor::Key::eW, 1.f}, {kor::Key::eS, -1.f}});
        }
        void Update() override {
            jump = Input::Get().ActionState("Jump");
            moveX = Input::Get().Axis("MoveX");
            move = Input::Get().Axis2D("MoveX", "MoveY");
            aHeld = Input::Get().IsGamepadButtonHeld(kor::GamepadButton::eA);
        }
        kor::KeyState jump = kor::KeyState::eNotPressed;
        float moveX = 0.f;
        kor::Vec2 move{0.f};
        bool aHeld = false;
    };
    auto& app = VkEnvironment::app();
    auto* player = app.OpenOffscreen<Player>({.extent = {16, 16}});
    ASSERT_NE(player, nullptr);
    settle();
    auto& input = player->SceneInput();

    input.FeedKey(kor::Key::eSpace, true);
    settle();
    EXPECT_EQ(player->jump, kor::KeyState::ePressed);
    input.FeedGamepadButton(kor::GamepadButton::eA, true);   // a second source, while the first is down
    settle();
    EXPECT_EQ(player->jump, kor::KeyState::eHeld) << "still one press";
    input.FeedKey(kor::Key::eSpace, false);
    settle();
    EXPECT_EQ(player->jump, kor::KeyState::eHeld) << "A is still down";
    EXPECT_TRUE(player->aHeld);
    input.FeedGamepadButton(kor::GamepadButton::eA, false);
    settle();
    EXPECT_EQ(player->jump, kor::KeyState::eReleased);

    input.FeedKey(kor::Key::eD, true);
    input.FeedKey(kor::Key::eW, true);
    settle();
    EXPECT_EQ(player->moveX, 1.f);
    EXPECT_NEAR(kor::Length(player->move), 1.f, 1e-5f) << "a diagonal is no faster";
    input.FeedKey(kor::Key::eD, false);
    input.FeedKey(kor::Key::eW, false);

    input.FeedGamepadAxis(kor::GamepadAxis::eLeftX, 0.1f);
    settle();
    EXPECT_EQ(player->moveX, 0.f) << "inside the dead zone";
    input.FeedGamepadAxis(kor::GamepadAxis::eLeftX, -0.575f);
    settle();
    EXPECT_NEAR(player->moveX, -0.5f, 1e-4f) << "past it, stretched back over the rest of the travel";
    EXPECT_TRUE(input.IsGamepadConnected(0)) << "a fed gamepad counts as connected";

    app.Close(*player);
    settle();
}

// An editor's game view: a panel in one scene's interface showing another, offscreen, sized to the
// panel and — while the pointer is over it — given the editor's input.
TEST_F(VkWindowTest, ASceneViewShowsAnOffscreenSceneSizedToThePanel) {
    auto& app = VkEnvironment::app();
    auto& editor = VkEnvironment::scene();
    auto* game = app.OpenOffscreen<ListeningScene>({.extent = {16, 16}});
    ASSERT_NE(game, nullptr);

    auto state = std::make_shared<kgui::ViewportState>();
    editor.testInterface = kui::Align(kui::Alignment::TopLeft(), kui::SizedBox(200.f, 150.f, kgui::SceneView(*game, state)));
    for (int frame = 0; frame < 4; ++frame) settle();
    editor.testInterface = {};

    // In pixels: twice the panel's layout size on a Retina display.
    EXPECT_EQ(state->Size(), kor::UVec2(kor::Vec2(200, 150) * editor.SceneWindow().PixelRatio())) << "the panel's size";
    EXPECT_EQ(game->SceneWindow().Extent(), state->Size()) << "sized to the panel";
    EXPECT_EQ(game->resizedTo, state->Size());
    EXPECT_EQ(state->Image().Get(), game->SceneWindow().Image().Get()) << "showing the scene's picture";

    app.Close(*game);
    settle();
}

// ---- views ----------------------------------------------------------------------------------------

namespace {
    // A scene drawn twice: two views, each clearing its own screen to its own colour and reading one
    // texel back, and noting what `Window::` was while it recorded.
    class TwoViews final : public kor::Scene {
    public:
        struct Seen { const kor::Window* window = nullptr; kor::UVec2 extent{0, 0}; };

        void Initialize() override {
            for (const auto& [name, red, extent] : {std::tuple{"Left", 0.25f, kor::UVec2{40, 30}},
                                                    std::tuple{"Right", 0.75f, kor::UVec2{20, 10}}}) {
                auto& view = AddView(name, {.extent = extent});
                auto readback = std::make_shared<kor::Resource<kor::Buffer>>(kor::Buffer::RawBuilder{}.SetRawSize(4)
                    .SetUsage(kor::Buffer::Usage::eTransferDst).SetType(kor::Buffer::Type::eReadback).Build());
                auto seen = std::make_shared<Seen>();
                readbacks[name] = readback;
                seens[name] = seen;
                auto target = std::make_shared<kor::ResourceRef<const kor::Image>>();
                auto& paint = view.Graph().Add<LambdaPass>("Paint");
                paint.setup = [](kor::PassBuilder& b) {
                    b.Write(kor::FrameGraph::Screen, kor::Image::Usage::eTransferDst | kor::Image::Usage::eTransferSrc).SideEffect();
                };
                paint.initialize = [target](const kor::PassResources& r) { *target = r.ImageNamed(kor::FrameGraph::Screen); };
                paint.record = [target, readback, seen, red](kor::CommandBuffer& cb) {
                    seen->window = &Window::Get();
                    seen->extent = Window::Extent();
                    cb.ClearColorImage(*target, kor::Vec4(red, 0.f, 0.f, 1.f));
                    cb.CopyImageToBuffer(*target, kor::ResourceRef<const kor::Buffer>(*readback), kor::Copy{ .imageExtent = kor::IVec3(1, 1, 1) });
                };
            }
        }
        [[nodiscard]] kor::u8 Red(const std::string& view) const { return (*readbacks.at(view))->Read<kor::u8>(4)[0]; }

        std::map<std::string, std::shared_ptr<kor::Resource<kor::Buffer>>> readbacks;
        std::map<std::string, std::shared_ptr<Seen>> seens;
    };
}

// One scene, two views: each its own graph and its own image, and inside a view's passes `Window::`
// is the view's target.
TEST_F(VkWindowTest, ASceneDrawsEachOfItsViewsIntoItsOwnImage) {
    auto& app = VkEnvironment::app();
    auto* scene = app.OpenOffscreen<TwoViews>({.extent = {16, 16}});
    ASSERT_NE(scene, nullptr);
    for (int frame = 0; frame < 2; ++frame) settle();

    ASSERT_EQ(scene->Views().size(), 2u);
    auto* left = scene->FindView("Left");
    auto* right = scene->FindView("Right");
    ASSERT_NE(left, nullptr);
    ASSERT_NE(right, nullptr);
    EXPECT_EQ(left->Image()->Extent(), kor::UVec3(40, 30, 1));
    EXPECT_EQ(right->Image()->Extent(), kor::UVec3(20, 10, 1));
    EXPECT_NEAR(scene->Red("Left"), 64, 1);
    EXPECT_NEAR(scene->Red("Right"), 191, 1);
    EXPECT_EQ(scene->seens["Left"]->window, &left->Target()) << "Window:: in a view's pass is the view's target";
    EXPECT_EQ(scene->seens["Left"]->extent, kor::UVec2(40, 30));
    EXPECT_EQ(scene->seens["Right"]->extent, kor::UVec2(20, 10));

    // Resized, its passes see the new size; switched off, it is not drawn.
    left->Resize({50, 20});
    right->SetEnabled(false);
    const auto rightSeen = scene->seens["Right"]->extent;
    settle();
    EXPECT_EQ(scene->seens["Left"]->extent, kor::UVec2(50, 20));
    EXPECT_EQ(left->Image()->Extent(), kor::UVec3(50, 20, 1));
    scene->seens["Right"]->extent = {};
    settle();
    EXPECT_EQ(scene->seens["Right"]->extent, kor::UVec2(0, 0)) << "a view switched off is not drawn";
    (void)rightSeen;

    scene->RemoveView("Right");
    EXPECT_EQ(scene->FindView("Right"), nullptr);
    settle();

    app.Close(*scene);
    settle();
}

// A view shown in the scene's own interface, sized to the panel.
TEST_F(VkWindowTest, ASceneViewShowsOneOfTheScenesOwnViews) {
    auto& editor = VkEnvironment::scene();
    auto& view = editor.AddView("Preview", {.extent = {8, 8}});
    auto state = std::make_shared<kgui::ViewportState>();
    editor.testInterface = kui::Align(kui::Alignment::TopLeft(), kui::SizedBox(120.f, 90.f, kgui::SceneView(view, state)));
    for (int frame = 0; frame < 4; ++frame) settle();
    editor.testInterface = {};
    EXPECT_EQ(state->Size(), kor::UVec2(kor::Vec2(120, 90) * editor.SceneWindow().PixelRatio()));
    EXPECT_EQ(view.Target().Extent(), state->Size());
    EXPECT_EQ(state->Image().Get(), view.Image().Get());
    editor.RemoveView("Preview");
    settle();
}

// ---- debug lines ------------------------------------------------------------------------------------

// A line drawn in Update shows in the frame it was drawn in — through a DebugDrawPass, over what the
// scene drew — and is gone the next, unless it was given time to stay.
TEST_F(VkWindowTest, DebugLinesAreDrawnForTheirFrameOrTheirDuration) {
    class Lines final : public kor::Scene {
    public:
        void Initialize() override {
            readback = kor::Buffer::RawBuilder{}.SetRawSize(32 * 32 * 4).SetUsage(kor::Buffer::Usage::eTransferDst)
                .SetType(kor::Buffer::Type::eReadback).Build();
            auto screen = std::make_shared<kor::ResourceRef<const kor::Image>>();
            auto& clear = Graph().Add<LambdaPass>("Clear");
            clear.setup = [](kor::PassBuilder& b) { b.Write(kor::FrameGraph::Screen, kor::Image::Usage::eTransferDst); };
            clear.initialize = [screen](const kor::PassResources& r) { *screen = r.ImageNamed(kor::FrameGraph::Screen); };
            clear.record = [screen](kor::CommandBuffer& cb) { cb.ClearColorImage(*screen, kor::Vec4(0.f, 0.f, 0.f, 1.f)); };
            // Clip space is world space here: a line along y = 0 crosses the middle row.
            Graph().Add<kor::DebugDrawPass>(SceneDebug(), [] { return kor::Mat4(1.f); });
            auto& read = Graph().Add<LambdaPass>("Read");
            read.setup = [](kor::PassBuilder& b) { b.Read(kor::FrameGraph::Screen, kor::Image::Usage::eTransferSrc).SideEffect(); };
            read.initialize = [screen](const kor::PassResources& r) { *screen = r.ImageNamed(kor::FrameGraph::Screen); };
            read.record = [screen, out = kor::ResourceRef<const kor::Buffer>(readback)](kor::CommandBuffer& cb) {
                cb.CopyImageToBuffer(*screen, out);
            };
        }
        void Update() override {
            if (drawLine) Debug::Line({-1.f, 0.f, 0.5f}, {1.f, 0.f, 0.5f}, {.color = {1.f, 0.f, 0.f, 1.f}, .duration = duration});
            drawLine = false;
        }
        [[nodiscard]] kor::u8 RedAt(const int x, const int y) const { return readback->Read<kor::u8>(32 * 32 * 4)[(y * 32 + x) * 4]; }
        // y = 0 falls between the two middle rows; the rasterizer's edge rule picks one of them.
        [[nodiscard]] kor::u8 RedInTheMiddle() const { return std::max(RedAt(16, 15), RedAt(16, 16)); }
        kor::Resource<kor::Buffer> readback;
        bool drawLine = false;
        float duration = 0.f;
    };
    auto& app = VkEnvironment::app();
    auto* scene = app.OpenOffscreen<Lines>({.extent = {32, 32}});
    ASSERT_NE(scene, nullptr);
    settle();

    scene->drawLine = true;
    settle();
    EXPECT_EQ(scene->RedInTheMiddle(), 255) << "the line crosses the middle";
    EXPECT_EQ(scene->RedAt(16, 4), 0) << "and nothing else";
    EXPECT_EQ(scene->SceneDebug().LineCount(), 0u) << "for its frame only";
    settle();
    EXPECT_EQ(scene->RedInTheMiddle(), 0) << "gone the next frame";

    scene->drawLine = true;
    scene->duration = 60.f;
    settle();
    settle();
    EXPECT_EQ(scene->SceneDebug().LineCount(), 1u) << "given time, it stays";
    EXPECT_EQ(scene->RedInTheMiddle(), 255);

    app.Close(*scene);
    settle();
}

// Filled shapes are triangles over what the scene drew: a solid fill paints its colour, one you can see
// through blends with what is under it, and the outline is drawn over the fill.
TEST_F(VkWindowTest, DebugFillsAreSolidOrSeeThrough) {
    class Fills final : public kor::Scene {
    public:
        void Initialize() override {
            readback = kor::Buffer::RawBuilder{}.SetRawSize(32 * 32 * 4).SetUsage(kor::Buffer::Usage::eTransferDst)
                .SetType(kor::Buffer::Type::eReadback).Build();
            auto screen = std::make_shared<kor::ResourceRef<const kor::Image>>();
            auto& clear = Graph().Add<LambdaPass>("Clear");
            clear.setup = [](kor::PassBuilder& b) { b.Write(kor::FrameGraph::Screen, kor::Image::Usage::eTransferDst); };
            clear.initialize = [screen](const kor::PassResources& r) { *screen = r.ImageNamed(kor::FrameGraph::Screen); };
            clear.record = [screen](kor::CommandBuffer& cb) { cb.ClearColorImage(*screen, kor::Vec4(0.f, 0.f, 0.f, 1.f)); };
            Graph().Add<kor::DebugDrawPass>(SceneDebug(), [] { return kor::Mat4(1.f); });
            auto& read = Graph().Add<LambdaPass>("Read");
            read.setup = [](kor::PassBuilder& b) { b.Read(kor::FrameGraph::Screen, kor::Image::Usage::eTransferSrc).SideEffect(); };
            read.initialize = [screen](const kor::PassResources& r) { *screen = r.ImageNamed(kor::FrameGraph::Screen); };
            read.record = [screen, out = kor::ResourceRef<const kor::Buffer>(readback)](kor::CommandBuffer& cb) {
                cb.CopyImageToBuffer(*screen, out);
            };
        }
        void Update() override {
            // Clip space is world space: the left half solid red, the right half half-see-through green.
            Debug::Quad({-1.f, -1.f, 0.5f}, {0.f, -1.f, 0.5f}, {0.f, 1.f, 0.5f}, {-1.f, 1.f, 0.5f},
                        {.fill = {1.f, 0.f, 0.f, 1.f}, .outline = false});
            Debug::Quad({0.f, -1.f, 0.5f}, {1.f, -1.f, 0.5f}, {1.f, 1.f, 0.5f}, {0.f, 1.f, 0.5f},
                        {.fill = {0.f, 1.f, 0.f, 0.5f}, .outline = false});
            // And a blue outline across the middle, over both.
            Debug::Line({-1.f, 0.f, 0.5f}, {1.f, 0.f, 0.5f}, {.color = {0.f, 0.f, 1.f, 1.f}});
            // A white line 5 pixels wide, a quarter of the way down: rows 6 to 10.
            Debug::Line({-1.f, -0.5f, 0.5f}, {1.f, -0.5f, 0.5f}, {.color = {1.f, 1.f, 1.f, 1.f}, .lineWidth = 5.f});
        }
        [[nodiscard]] kor::U8Vec4 At(const int x, const int y) const {
            const auto pixels = readback->Read<kor::u8>(32 * 32 * 4);
            const int i = (y * 32 + x) * 4;
            return {pixels[i], pixels[i + 1], pixels[i + 2], pixels[i + 3]};
        }
        kor::Resource<kor::Buffer> readback;
    };
    auto& app = VkEnvironment::app();
    auto* scene = app.OpenOffscreen<Fills>({.extent = {32, 32}});
    ASSERT_NE(scene, nullptr);
    settle();
    settle();

    const auto solid = scene->At(8, 4), seeThrough = scene->At(24, 4);
    EXPECT_EQ(solid.x, 255) << "a solid fill is its colour";
    EXPECT_EQ(solid.y, 0);
    EXPECT_NEAR(seeThrough.y, 128, 2) << "half see-through over black is half its colour";
    EXPECT_EQ(seeThrough.x, 0);
    const auto line = std::max(scene->At(8, 15).z, scene->At(8, 16).z);
    EXPECT_EQ(line, 255) << "the line is drawn over the solid fill";
    for (const int row : {7, 8, 9})
        EXPECT_EQ(scene->At(8, row), kor::U8Vec4(255, 255, 255, 255)) << "a wide line covers row " << row;
    EXPECT_EQ(scene->At(8, 4).x, 255) << "and not what is beyond its width";
    EXPECT_EQ(scene->At(8, 4).y, 0);

    app.Close(*scene);
    settle();
}

// ---- state scenes share ---------------------------------------------------------------------------

namespace {
    struct World {
        explicit World(const int seed = 0) : seed(seed) {}
        int seed;
        int edits = 0;
    };
}

// Two scenes asking for one key get one object; nobody holding it, it goes, and the next to ask
// makes a new one.
TEST_F(VkWindowTest, ScenesShareStateByKeyWithoutGlobals) {
    auto& app = VkEnvironment::app();
    auto first = app.Shared<World>("world", 7);
    auto second = app.Shared<World>("world", 99);
    ASSERT_EQ(first.get(), second.get()) << "the second asker gets the first one's";
    EXPECT_EQ(second->seed, 7) << "made from the first asker's arguments";
    first->edits = 3;
    EXPECT_EQ(second->edits, 3);
    EXPECT_TRUE(app.IsShared("world"));
    EXPECT_THROW((void)app.Shared<int>("world"), std::logic_error) << "one key, one type";

    first.reset();
    second.reset();
    EXPECT_FALSE(app.IsShared("world")) << "held by nobody, it is gone";
    EXPECT_EQ(app.Shared<World>("world", 1)->seed, 1) << "and the next to ask makes a new one";
}

// A library of scenes, loaded while the application runs: its scenes opened by name, the library
// loaded again with them reopened, then unloaded with every window that showed one. @see sceneLibrary.h
TEST_F(VkWindowTest, ASceneLibraryIsLoadedOpenedReloadedAndUnloaded) {
    auto& app = VkEnvironment::app();
    const std::filesystem::path library = KORAL_TEST_SCENE_LIBRARY;
    const auto alive = [&] {
        // A second handle to the loaded library, released at once so it cannot keep it loaded.
        void* handle = dlopen(library.c_str(), RTLD_NOW | RTLD_NOLOAD);
        if (!handle) return -1;
        const auto count = reinterpret_cast<int (*)()>(dlsym(handle, "KoralTestScenesAlive"));
        const int result = count ? count() : -1;
        dlclose(handle);
        return result;
    };

    const auto names = app.LoadLibrary(library);
    ASSERT_TRUE(names) << names.error().message;
    EXPECT_EQ(*names, (std::vector<std::string>{"Library.Plain", "Library.Arguments", "Library.Stateful"}));
    EXPECT_FALSE(app.LoadLibrary(library)) << "loading it twice is refused; ReloadLibrary is for that";

    ASSERT_NE(app.Open("Library.Plain", kSmall), nullptr);
    auto* withArguments = app.Open("Library.Arguments", kSmall, {{"level", "3"}});
    ASSERT_NE(withArguments, nullptr);
    EXPECT_EQ(withArguments->SceneWindow().Title(), "level 3") << "the arguments reached the scene";
    for (int frame = 0; frame < 2; ++frame) settle();
    EXPECT_EQ(alive(), 2);
    EXPECT_EQ(app.Scenes().size(), 3u);

    // Reloaded: every window showing one of its scenes closes, and opens again with the same scene.
    ASSERT_TRUE(app.ReloadLibrary(library));
    EXPECT_EQ(app.Scenes().size(), 3u);
    EXPECT_EQ(alive(), 2);
    const auto scenes = app.Scenes();
    const auto reopened = std::ranges::find_if(scenes, [](const kor::Scene* s) { return s->Name() == "Library.Arguments"; });
    ASSERT_NE(reopened, scenes.end());
    EXPECT_EQ((*reopened)->SceneWindow().Title(), "level 3") << "reopened with the arguments it had";
    for (int frame = 0; frame < 2; ++frame) settle();

    // Unloaded: its windows close, its names are gone, and so is the library itself.
    ASSERT_TRUE(app.UnloadLibrary(library));
    EXPECT_EQ(app.Scenes().size(), 1u);
    EXPECT_EQ(alive(), -1) << "the library is still loaded";
    EXPECT_EQ(app.Open("Library.Plain", kSmall), nullptr);
    settle();
}

// A rebuilt library's scenes come back with the state they had: saved before the old code goes, given
// to the new scene before its Initialize.
TEST_F(VkWindowTest, AReloadedScenesStateSurvivesTheReload) {
    auto& app = VkEnvironment::app();
    const std::filesystem::path library = KORAL_TEST_SCENE_LIBRARY;
    ASSERT_TRUE(app.LoadLibrary(library));
    auto* scene = app.Open("Library.Stateful", kSmall);
    ASSERT_NE(scene, nullptr);
    for (int frame = 0; frame < 3; ++frame) settle();
    scene->State().Field("note").As<std::string>() = "kept";
    const int frames = scene->State().Field("frames").As<int>();
    EXPECT_GE(frames, 3);
    EXPECT_NE(scene->SaveState().find("\"note\":\"kept\""), std::string::npos);

    ASSERT_TRUE(app.ReloadLibrary(library));
    const auto scenes = app.Scenes();
    const auto reopened = std::ranges::find_if(scenes, [](const kor::Scene* s) { return s->Name() == "Library.Stateful"; });
    ASSERT_NE(reopened, scenes.end());
    ASSERT_NE(*reopened, nullptr);
    EXPECT_EQ((*reopened)->State().Field("note").As<std::string>(), "kept");
    EXPECT_EQ((*reopened)->State().Field("frames").As<int>(), frames) << "restored, and not a frame run yet";
    settle();
    EXPECT_EQ((*reopened)->State().Field("frames").As<int>(), frames + 1) << "and it carries on from there";

    ASSERT_TRUE(app.UnloadLibrary(library));
    settle();
}

// ---- per-frame device-local buffers ---------------------------------------------------------
//
// The same promise as the two tests above, for memory the CPU cannot map. A write there is staged, and
// each frame's copy has to receive it — as that frame's own command, since the other copies may still
// be in use by frames in flight when the write is made.

namespace {
    kor::Resource<kor::Buffer> makeDeviceLocalPerFrameU32() {
        kor::Buffer::RawBuilder rb;
        rb.SetRawSize(static_cast<kor::i64>(sizeof(kor::u32)))
          .SetUsage(kor::Buffer::Usage::eStorage | kor::Buffer::Usage::eTransferSrc | kor::Buffer::Usage::eTransferDst)
          .SetIsPerFrame(true)
          .SetType(kor::Buffer::Type::eDeviceLocal);
        return rb.Build();
    }
}

TEST_F(VkWindowTest, ADeviceLocalPerFrameBufferPropagatesAWriteToEveryCopy) {
    auto& scene = VkEnvironment::scene();
    const auto copies = kor::Context::Scheduler().ImageCount();
    ASSERT_GE(copies, 2u) << "nothing to propagate to with a single copy";
    auto buffer = makeDeviceLocalPerFrameU32();
    ASSERT_TRUE(static_cast<bool>(buffer));

    // Between frames, the way a loading coroutine or a scene's Initialize writes.
    constexpr kor::u32 kValue = 0xC0FFEE;
    buffer->Write(std::array<kor::u32, 1>{ kValue }, 0);

    for (kor::u32 i = 0; i < copies * 2 + 1; ++i) {
        drawFrame(scene);
        EXPECT_EQ(buffer->Read<kor::u32>(1).front(), kValue) << "frame " << i << " read a copy that never got the write";
    }
}

TEST_F(VkWindowTest, ADeviceLocalPerFrameBufferWrittenInsideAFramePropagatesToEveryCopy) {
    auto& scene = VkEnvironment::scene();
    const auto copies = kor::Context::Scheduler().ImageCount();
    auto buffer = makeDeviceLocalPerFrameU32();
    ASSERT_TRUE(static_cast<bool>(buffer));

    // Once, from inside a frame, and never again.
    constexpr kor::u32 kValue = 0xBEEF;
    scene.onUpdate = [&] { buffer->Write(std::array<kor::u32, 1>{ kValue }, 0); scene.onUpdate = nullptr; };

    for (kor::u32 i = 0; i < copies * 2 + 1; ++i) {
        drawFrame(scene);
        EXPECT_EQ(buffer->Read<kor::u32>(1).front(), kValue) << "frame " << i << " read a copy that never got the write";
    }
    scene.onUpdate = nullptr;
}

TEST_F(VkWindowTest, ADeviceLocalPerFrameBufferWrittenEveryFrameReadsBackThatFramesValue) {
    auto& scene = VkEnvironment::scene();
    auto buffer = makeDeviceLocalPerFrameU32();
    ASSERT_TRUE(static_cast<bool>(buffer));

    // Every frame a new value — so an older write still on its way to a copy must never land on top
    // of a newer one.
    kor::u32 value = 0;
    scene.onUpdate = [&] { ++value; buffer->Write(std::array<kor::u32, 1>{ value }, 0); };
    for (int i = 0; i < 12; ++i) {
        drawFrame(scene);
        EXPECT_EQ(buffer->Read<kor::u32>(1).front(), value) << "frame " << i << " read another frame's value";
    }
    scene.onUpdate = nullptr;
}

TEST_F(VkWindowTest, ADeviceLocalPerFrameBufferBuiltWithDataHoldsItInEveryCopy) {
    auto& scene = VkEnvironment::scene();
    const auto copies = kor::Context::Scheduler().ImageCount();

    constexpr kor::u32 kValue = 0xF00D;
    auto buffer = kor::Buffer::Builder<kor::u32>()
        .SetData(kValue)
        .SetUsage(kor::Buffer::Usage::eStorage | kor::Buffer::Usage::eTransferSrc | kor::Buffer::Usage::eTransferDst)
        .SetIsPerFrame(true)
        .SetType(kor::Buffer::Type::eDeviceLocal)
        .Build();
    ASSERT_TRUE(static_cast<bool>(buffer));

    for (kor::u32 i = 0; i < copies * 2 + 1; ++i) {
        drawFrame(scene);
        EXPECT_EQ(buffer->Read<kor::u32>(1).front(), kValue) << "frame " << i << " read a copy the initial data never reached";
    }
}

// The staging buffers behind those writes must not outlive them: each is released once every copy has
// its data and the last frame to copy from it has finished — or a buffer written every frame leaks one
// staging buffer per frame.
//
// "Every copy" means every swap-chain image, and the driver does not hand those out in turn: with a
// mailbox present mode it can alternate between two for a dozen frames before the third comes round.
// So this draws until the staging is gone, with a cap, rather than for a fixed count.
TEST_F(VkWindowTest, ADeviceLocalPerFrameBufferReleasesItsStagingOnceDelivered) {
    auto& scene = VkEnvironment::scene();
    const auto copies = kor::Context::Scheduler().ImageCount();
    auto buffer = makeDeviceLocalPerFrameU32();
    ASSERT_TRUE(static_cast<bool>(buffer));
    for (kor::u32 i = 0; i < copies + 1; ++i) drawFrame(scene);
    (void)buffer->Read<kor::u32>(1);
    drawFrame(scene);
    const auto baseline = kor::Context::Repository().TrackedResources();

    kor::u32 value = 0;
    scene.onUpdate = [&] { buffer->Write(std::array<kor::u32, 1>{ ++value }, 0); };
    for (int i = 0; i < 20; ++i) drawFrame(scene);
    scene.onUpdate = nullptr;
    buffer->Write(std::array<kor::u32, 1>{ ++value }, 0);   // and one between frames

    int frames = 0;
    for (; frames < 200 && kor::Context::Repository().TrackedResources() != baseline; ++frames) drawFrame(scene);
    EXPECT_EQ(kor::Context::Repository().TrackedResources(), baseline)
        << "staging buffers were still held " << frames << " frames after the last write";
    EXPECT_EQ(buffer->Read<kor::u32>(1).front(), value);
}

// The moving-camera case: a per-frame buffer written with a *different* value every frame must read
// back, on that frame, as the value written on that frame — never as a neighbour's.
//
// This is the shape of a camera being moved. If a frame ever reads another frame's value the view
// jumps back and forth by one frame's worth of movement, which is what "trembling" looks like.
TEST_F(VkWindowTest, APerFrameBufferWrittenEveryFrameReadsBackWhatItWasGiven) {
    auto& scene = VkEnvironment::scene();

    kor::Buffer::RawBuilder rb;
    rb.SetRawSize(static_cast<kor::i64>(sizeof(kor::u32)))
      .SetUsage(kor::Buffer::Usage::eUniform)
      .SetIsPerFrame(true)
      .SetType(kor::Buffer::Type::eDynamic);
    auto buffer = rb.Build();
    ASSERT_TRUE(static_cast<bool>(buffer));

    for (kor::u32 i = 1; i <= 12; ++i) {
        // Written where a camera writes it: from the repository update at the top of the frame.
        const std::array<kor::u32, 1> value{ i };
        buffer->Write(std::span<const kor::u32>(value), 0);

        const auto readBack = buffer->Read<kor::u32>(1).front();
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
    kor::log::ClearHistory();
    kor::log::SetRepeatLimit(0);          // no suppression: every one of these must land
    for (int i = 0; i < 2000; ++i) kor::log::Info("a log line with some text in it, number {}", i);
    kor::log::SetRepeatLimit(10);

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

    std::cout << "[ MEASURE  ] " << kor::log::History().size() << " records: "
              << withoutPanel << " ms/frame without the log panel, " << withPanel << " with it ("
              << (withPanel - withoutPanel) << " ms is the panel)" << std::endl;
    SUCCEED();
}

TEST_F(VkWindowTest, MeasurePerFrameBufferWriteCost) {
    auto& scene = VkEnvironment::scene();

    kor::Buffer::RawBuilder rb;
    rb.SetRawSize(static_cast<kor::i64>(sizeof(kor::u32)))
      .SetUsage(kor::Buffer::Usage::eUniform)
      .SetIsPerFrame(true)
      .SetType(kor::Buffer::Type::eDynamic);
    auto buffer = rb.Build();

    const auto time = [&](const int count, const bool writing) {
        for (int i = 0; i < 3; ++i) drawFrame(scene);
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < count; ++i) {
            if (writing) {
                const std::array<kor::u32, 1> value{ static_cast<kor::u32>(i) };
                buffer->Write(std::span<const kor::u32>(value), 0);
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

// The cursor mode is a mode of *every* window input is read from, and a change to it must not arrive
// as movement.
//
// Capturing warps the pointer and releasing it puts it back; reporting either as a delta would fling a
// camera the instant aiming began, which is the classic version of this bug.
TEST_F(VkWindowTest, CapturingTheCursorReportsNoMovementForIt) {
    auto& scene = VkEnvironment::scene();
    ASSERT_EQ(VkEnvironment::scene().SceneInput().CurrentCursorMode(), kor::Input::CursorMode::eNormal);

    for (int i = 0; i < 3; ++i) drawFrame(scene);

    VkEnvironment::scene().SceneInput().SetCursorMode(kor::Input::CursorMode::eCaptured);
    EXPECT_EQ(VkEnvironment::scene().SceneInput().CurrentCursorMode(), kor::Input::CursorMode::eCaptured);
    EXPECT_EQ(glfwGetInputMode(*VkEnvironment::scene().SceneWindow(), GLFW_CURSOR), GLFW_CURSOR_DISABLED);

    drawFrame(scene);
    EXPECT_EQ(VkEnvironment::scene().SceneInput().MousePositionDelta(), kor::Vec2(0.f, 0.f))
        << "the warp that capturing performs was reported as movement";

    VkEnvironment::scene().SceneInput().SetCursorMode(kor::Input::CursorMode::eNormal);
    EXPECT_EQ(glfwGetInputMode(*VkEnvironment::scene().SceneWindow(), GLFW_CURSOR), GLFW_CURSOR_NORMAL);

    drawFrame(scene);
    EXPECT_EQ(VkEnvironment::scene().SceneInput().MousePositionDelta(), kor::Vec2(0.f, 0.f))
        << "releasing the cursor was reported as movement";

    // Hidden is the middle setting: invisible, but still free to move.
    VkEnvironment::scene().SceneInput().SetCursorMode(kor::Input::CursorMode::eHidden);
    EXPECT_EQ(glfwGetInputMode(*VkEnvironment::scene().SceneWindow(), GLFW_CURSOR), GLFW_CURSOR_HIDDEN);
    VkEnvironment::scene().SceneInput().SetCursorMode(kor::Input::CursorMode::eNormal);
}

// A window attached while the cursor is captured has to arrive in the same mode, or the cursor
// reappears the moment the pointer crosses into an undocked panel.
TEST_F(VkWindowTest, AWindowAttachedWhileCapturedArrivesCaptured) {
    VkEnvironment::scene().SceneInput().SetCursorMode(kor::Input::CursorMode::eCaptured);

    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* second = glfwCreateWindow(64, 64, "second", nullptr, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(glfwGetInputMode(second, GLFW_CURSOR), GLFW_CURSOR_NORMAL) << "not attached yet";

    VkEnvironment::scene().SceneInput().AttachTo(second);
    EXPECT_EQ(glfwGetInputMode(second, GLFW_CURSOR), GLFW_CURSOR_DISABLED);

    VkEnvironment::scene().SceneInput().SetCursorMode(kor::Input::CursorMode::eNormal);
    EXPECT_EQ(glfwGetInputMode(second, GLFW_CURSOR), GLFW_CURSOR_NORMAL) << "and follows a change";

    VkEnvironment::scene().SceneInput().DetachFrom(second);
    glfwDestroyWindow(second);
}

// Input must be readable from more than the main window, because an undocked interface panel is a
// window of its own and GLFW delivers events to the window they happen over.
//
// A real undocked panel cannot be produced from a test — it takes dragging — so this attaches a second
// window directly, which is the same path an interface puts the windows of its floating panels through.
TEST_F(VkWindowTest, InputCanBeReadFromMoreThanTheMainWindow) {
    // Counted per window rather than compared against a total: an interface may attach windows of its
    // own, which is the mechanism working, not a leak.
    const auto timesAttached = [](GLFWwindow* window) {
        const auto attached = VkEnvironment::scene().SceneInput().AttachedWindows();
        return std::ranges::count(attached, window);
    };

    const auto before = VkEnvironment::scene().SceneInput().AttachedWindows();
    ASSERT_FALSE(before.empty()) << "the main window should be attached";
    EXPECT_EQ(before.front(), *VkEnvironment::scene().SceneWindow());

    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* second = glfwCreateWindow(64, 64, "second", nullptr, nullptr);
    ASSERT_NE(second, nullptr);

    VkEnvironment::scene().SceneInput().AttachTo(second);
    EXPECT_EQ(timesAttached(second), 1);
    // Attaching twice is not an error and does not double up — an interface may call it every frame.
    VkEnvironment::scene().SceneInput().AttachTo(second);
    EXPECT_EQ(timesAttached(second), 1);

    // A frame with the extra window attached must be no different from one without.
    auto& scene = VkEnvironment::scene();
    for (int i = 0; i < 3; ++i) drawFrame(scene);
    EXPECT_EQ(timesAttached(second), 1) << "a frame must not disturb a window attached by hand";

    VkEnvironment::scene().SceneInput().DetachFrom(second);
    EXPECT_EQ(timesAttached(second), 0);
    EXPECT_EQ(timesAttached(*VkEnvironment::scene().SceneWindow()), 1) << "and must leave the main window attached";
    glfwDestroyWindow(second);

    for (int i = 0; i < 2; ++i) drawFrame(scene);
}

// A per-frame image that is only ever *sampled* — never written — shown through a viewport across
// enough frames to come round to every copy in flight.
//
// The image is sampled by the interface's draws, so the barrier that makes each copy readable is inferred
// from that use: a frame-blind layout tracker would skip it for a copy that has never been transitioned.
TEST_F(VkWindowTest, APerFrameImageThatIsOnlySampledIsShownCleanly) {
    auto& scene = VkEnvironment::scene();

    // Enough frames to come round to every copy at least twice.
    for (int i = 0; i < 3; ++i) drawFrame(scene);
    kor::log::ClearHistory();
    kor::log::ResetRepeatCounts();

    for (int i = 0; i < 10; ++i) drawFrame(scene);
    kor::Context::Scheduler().WaitIdle();
    for (int i = 0; i < 2; ++i) drawFrame(scene);

    std::vector<std::string> complaints;
    for (const auto& record : kor::log::History())
        if (record.level == kor::log::Level::eError) complaints.push_back(record.message);
    EXPECT_TRUE(complaints.empty())
        << complaints.size() << " error(s), first: " << (complaints.empty() ? "" : complaints.front());
    EXPECT_GT(scene.sampledOnlyViewport->Size().x, 0u);
}

// ...and the picture is actually *there* after a resize settles: the target is rendered into at its new
// size, and the interface shows the new image.
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
    scene.viewportFramebuffer->Resize(kor::UVec2{ 96, 72 });
    for (int i = 0; i < 3; ++i) drawFrame(scene);

    EXPECT_EQ(scene.viewportTarget->Extent(), kor::UVec3(96, 72, 1));
    EXPECT_EQ(scene.directViewport->Image().Get(), scene.viewportTarget.Get()) << "the viewport still shows the target";

    // The target holds what the pass cleared it to, at the new size — so it was rendered into after
    // being replaced, not left undefined.
    kor::Buffer::RawBuilder rb;
    rb.SetRawSize(static_cast<kor::i64>(96) * 72 * 4)
      .SetUsage(kor::Buffer::Usage::eTransferDst)
      .SetType(kor::Buffer::Type::eReadback);
    auto readback = rb.Build();

    kor::CommandBuffer::SingleTimeCommand([&](kor::CommandBuffer& cb) {
        cb.CopyImageToBuffer(scene.viewportTarget,
                             readback);
    }, kor::CommandBuffer::Usage::eTransfer).Wait();

    const auto texels = readback->Read<kor::U8Vec4>();
    ASSERT_EQ(texels.size(), static_cast<std::size_t>(96) * 72);
    // The framebuffer's own clear colour, {0.2, 0, 0.4, 1}.
    EXPECT_NEAR(texels.front().x, 51, 3);
    EXPECT_NEAR(texels.front().z, 102, 3);
}

// A frame that never touches the window's framebuffer must still come out as the colour that
// framebuffer was given: a scene that renders into its own targets and shows them through the
// interface would otherwise present last frame's picture, or uninitialised memory.
TEST_F(VkWindowTest, AnUntouchedScreenIsClearedByTheRuntime) {
    // What the runtime does each frame, in the order it does it. The scene deliberately records
    // nothing against the screen — as a viewport-only scene does not.
    auto& scene = VkEnvironment::scene();
    bool cleared = false;

    drawCustomFrame(VkEnvironment::scene(), [&](kor::CommandBuffer& cb) {
        const auto framebuffer = VkEnvironment::scene().SceneWindow().DefaultFramebuffer();
        ASSERT_TRUE(framebuffer.Valid());
        ASSERT_FALSE(framebuffer->ColorAttachments().empty());
        const auto screen = framebuffer->ColorImage(0);

        // Nothing has been recorded, so nothing can have touched it.
        EXPECT_FALSE(cb.HasTouched(screen));

        cb.BeginRendering();
        cb.EndRendering();
        cleared = true;

        // ...and now it has, which is what stops the runtime clearing it a second time.
        EXPECT_TRUE(cb.HasTouched(screen));

    });

    EXPECT_TRUE(cleared);
}

// The other half: a frame that *did* touch the screen is left alone.
TEST_F(VkWindowTest, ATouchedScreenIsNotClearedAgain) {
    auto& scene = VkEnvironment::scene();

    drawCustomFrame(VkEnvironment::scene(), [&](kor::CommandBuffer& cb) {
        const auto framebuffer = VkEnvironment::scene().SceneWindow().DefaultFramebuffer();
        const auto screen = framebuffer->ColorImage(0);

        cb.ClearColorImage(screen, kor::Vec4{0.1f, 0.2f, 0.3f, 1.f});
        EXPECT_TRUE(cb.HasTouched(screen)) << "a clear is an interaction with the framebuffer";

    });
}

// The case the two above left open, and it shipped in 0.1.0: a scene whose entire output is
// Blit(image), which is how a compute rasterizer shows its canvas and how both IPG labs are
// written.
//
// The screen-targeting Blit resolves its destination inside the backend, so it used to declare only
// its *source* as a use. That made it the one way of drawing to the screen that HasTouched() could
// not see: the runtime concluded the frame had never touched the framebuffer and cleared it on top
// of the blit, leaving a blank window with the interface still drawn over it. Nothing caught it
// because every other screen test reaches the framebuffer through BeginRendering or a clear, both
// of which declare it.
TEST_F(VkWindowTest, BlittingToTheScreenCountsAsTouchingIt) {
    auto& scene = VkEnvironment::scene();

    const auto canvas = kor::Image::Builder{}
        .SetType(kor::Image::Type::e2D)
        .SetFormat(kor::Image::Format::eRGBA8_UNORM)
        .SetExtent(kor::UVec2{64, 64})
        .SetUsage(kor::Image::Usage::eTransferSrc | kor::Image::Usage::eTransferDst)
        .Build();
    ASSERT_TRUE(canvas);

    drawCustomFrame(VkEnvironment::scene(), [&](kor::CommandBuffer& cb) {
        const auto framebuffer = VkEnvironment::scene().SceneWindow().DefaultFramebuffer();
        ASSERT_TRUE(framebuffer.Valid());
        const auto screen = framebuffer->ColorImage(0);

        cb.ClearColorImage(canvas, kor::Vec4{0.9f, 0.2f, 0.1f, 1.f})
          .BlitToScreen(canvas);

        EXPECT_TRUE(cb.HasTouched(screen))
            << "a blit to the screen is the whole output of a compute-rasterizer scene; if the "
               "runtime cannot see it, it clears the picture away";

    });
}

// GPU timers over the real frame path, which is the one thing the headless timer tests cannot
// reach: the frame's command buffer is reset and re-recorded every frame, and its results are
// collected when its frame in flight comes round again. Run with validation on, this is also what
// proves the query pool is reset legally — outside a render pass, and never while in use.
TEST_F(VkWindowTest, FrameTimersReportTheFramesOwnWork) {
    auto& scene = VkEnvironment::scene();

    // Long enough for a frame that recorded a timer to complete and be recorded into again, which
    // takes a full cycle of the frames in flight.
    const int budget = static_cast<int>(kor::Context::Scheduler().ImageCount()) + 4;
    bool found = false;
    double milliseconds = 0.0;

    for (int frame = 0; frame < budget && !found; ++frame) {
        drawCustomFrame(VkEnvironment::scene(), [&](kor::CommandBuffer& cb) {
            // Fetched per frame: the default framebuffer's colour attachment is the swap-chain
            // image this frame presents, so a reference taken once outside the loop goes stale
            // the moment the chain rotates.
            const auto framebuffer = VkEnvironment::scene().SceneWindow().DefaultFramebuffer();
            ASSERT_TRUE(framebuffer.Valid());
            const auto screen = framebuffer->ColorImage(0);

            cb.Timer("frame.clear", [&](kor::CommandBuffer& inner) {
                inner.ClearColorImage(screen, kor::Vec4{0.1f, 0.2f, 0.3f, 1.f});
            });
        });

        for (const auto& f : kor::Context::Scheduler().Frames()) {
            for (const auto& timing : f.get().Commands().Timings()) {
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


// The default framebuffer's targets are reachable like any other framebuffer's: by name, as images,
// so the swap-chain image a frame is presented from can be copied, read back or shown elsewhere
// without reaching into the swap chain.
TEST_F(VkWindowTest, TheDefaultFramebuffersImagesAreReachableByName) {
    const auto framebuffer = VkEnvironment::scene().SceneWindow().DefaultFramebuffer();
    ASSERT_TRUE(framebuffer.Valid());
    ASSERT_TRUE(framebuffer->IsDefault());

    const auto colour = framebuffer->ImageNamed("color");
    ASSERT_TRUE(colour.Valid()) << "the presented image was not reachable by name";
    // The same image the index-addressed accessor gives, and the same one the command buffer calls
    // the screen — one image, three ways of asking for it.
    EXPECT_EQ(colour.Get(), framebuffer->ColorImage(0).Get());
    EXPECT_EQ(colour.Get(), kor::CommandBuffer::ScreenImage().Get());
    EXPECT_EQ(colour->Extent().x, framebuffer->Extent().x);
    EXPECT_EQ(colour->Extent().y, framebuffer->Extent().y);

    // The depth target too, which is what a scene wanting to read the frame's depth needs.
    const auto depth = framebuffer->ImageNamed("depth");
    ASSERT_TRUE(depth.Valid());
    EXPECT_EQ(depth.Get(), framebuffer->DepthImage().Get());

    const auto names = framebuffer->AttachmentNames();
    EXPECT_NE(std::ranges::find(names, "color"), names.end());
    EXPECT_NE(std::ranges::find(names, "depth"), names.end());

    EXPECT_FALSE(framebuffer->ImageNamed("nosuchattachment").Valid());
}

} // namespace


// A per-frame resource has one copy per frame in flight, and a command picks its copy when End()
// writes it out, not when it is recorded. For a command buffer handed to Execute() that is inside
// the frame it runs in — the frame ends it, after acquiring — so it must see that frame's copy even
// though it was recorded between frames, on another thread, while the previous frame was current.
TEST_F(VkWindowTest, AnExecutedCommandBufferUsesTheCopyOfTheFrameItRunsIn) {
    auto& scene = VkEnvironment::scene();
    auto& scheduler = kor::Context::Scheduler();
    ASSERT_GE(scheduler.ImageCount(), 2u) << "one copy per frame makes the question moot";

    kor::Buffer::RawBuilder rb;
    rb.SetRawSize(static_cast<kor::i64>(sizeof(kor::u32)))
      .SetUsage(kor::Flags(kor::Buffer::Usage::eUniform) | kor::Buffer::Usage::eTransferSrc)
      .SetIsPerFrame(true)
      .SetType(kor::Buffer::Type::eDynamic);
    auto perFrame = rb.Build();
    ASSERT_TRUE(static_cast<bool>(perFrame));

    kor::Buffer::Builder<kor::u32> db;
    db.SetData(std::vector<kor::u32>{0});
    db.SetUsage(kor::Flags(kor::Buffer::Usage::eStorage) | kor::Buffer::Usage::eTransferSrc | kor::Buffer::Usage::eTransferDst);
    db.SetType(kor::Buffer::Type::eDeviceLocal);
    auto destination = db.Build();
    ASSERT_TRUE(static_cast<bool>(destination));

    drawFrame(scene);
    for (kor::u32 value = 1; value <= scheduler.ImageCount() * 2; ++value) {
        std::unique_ptr<kor::CommandBuffer> copy;
        std::thread([&] {
            copy = kor::CommandBuffer::Create(kor::CommandBuffer::Usage::eGraphics);
            copy->Begin();
            copy->CopyBuffer(perFrame, destination);
        }).join();
        const kor::Token done = scheduler.Execute(std::move(copy));

        // This frame writes its own copy; the previous frame's still holds the previous value.
        drawCustomFrame(VkEnvironment::scene(), [&](kor::CommandBuffer&) {
            const std::array<kor::u32, 1> v{ value };
            perFrame->Write(std::span<const kor::u32>(v), 0);
        }, /*drawDefault=*/true);

        ASSERT_TRUE(seam::drawUntil(done, [&] { drawFrame(scene); }));
        EXPECT_EQ(destination->Read<kor::u32>(1).front(), value)
            << "the executed copy read another frame's copy of the per-frame buffer";
    }
}

// ---- Scheduler seam: Execute / WaitFor / frameCompletion (see scheduler_seam_shared.h) ----------

TEST_F(VkWindowTest, ExecutedWorkRunsInOrderAroundTheFrame) {
    seam::executedWorkRunsInOrderAroundTheFrame([] { drawFrame(VkEnvironment::scene()); });
}

TEST_F(VkWindowTest, AnEndedCommandBufferIsRefusedByExecute) {
    seam::anEndedCommandBufferIsRefused();
}

TEST_F(VkWindowTest, ACoroutineResumesWhenItsFrameCompletes) {
    seam::aCoroutineResumesWhenItsFrameCompletes([] { drawFrame(VkEnvironment::scene()); });
}

TEST_F(VkWindowTest, AFrameWaitsForAToken) {
    seam::aFrameWaitsForAToken([] { drawFrame(VkEnvironment::scene()); });
}

// Registered before RUN_ALL_TESTS (compatible with gtest_main). gtest owns and
// deletes the environment.
static ::testing::Environment* const kVkEnv =
    ::testing::AddGlobalTestEnvironment(new VkEnvironment);

// Each suite supplies the frame the shared orientation helpers draw with.
void orient::drawSharedFrame(const std::function<void(kor::CommandBuffer&)>& record) {
    drawCustomFrame(VkEnvironment::scene(), record);
}
