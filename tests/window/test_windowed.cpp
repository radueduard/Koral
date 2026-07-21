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

#include <memory>

#include <GLFW/glfw3.h>
#include <imgui.h>

#include "commandBuffer.h"
#include "context.h"
#include "gui.h"
#include "image.h"
#include "resource.h"
#include "scene.h"
#include "scheduler.h"
#include "window.h"

#include "orientation_shared.h"

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
    }

    void Update() override { ++updates; }

    void Render(kor::CommandBuffer& cb) override {
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
    }

    void OnResize(glm::uvec2 extent) override { lastResize = extent; }

    int updates = 0;
    glm::uvec2 lastResize{0, 0};

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

// -----------------------------------------------------------------------------
// Y-orientation parity (see orientation_shared.h): a top-half quad drawn by the
// rasterizer and the same pattern written by a compute imageStore must land in
// the same place, and both are blit to the screen. This is the Vulkan reference
// for the identical check run on OpenGL in test_windowed_gl.cpp.
// -----------------------------------------------------------------------------
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
