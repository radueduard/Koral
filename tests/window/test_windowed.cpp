// Windowed integration tests: boot a real window + surface + swap chain +
// scheduler + ImGui, render a few frames (with an ImGui overlay and a GUI_Image),
// resize, and tear down. This is the only way to exercise the presentation stack
// (surface.cpp, swapChain.cpp, scheduler.cpp, the default-framebuffer path in
// framebuffer.cpp, and gui.cpp), none of which the headless harness can reach.
//
// It lives in its own executable because it creates a windowed Vulkan context,
// incompatible with the headless global environment used by the other integration
// tests. A real display (X11/Wayland) and a WSI-capable Vulkan loader must be
// available; the test skips gracefully if a window cannot be created.

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

namespace {

// A scene that draws a cleared default framebuffer plus an ImGui overlay and a
// GUI_Image, so a single frame drives the scheduler, the swap chain and the whole
// ImGui-on-Vulkan path (GUI_Image blit helper, textured widget, font access).
class OverlayScene : public gfx::Scene {
public:
    void Initialize() override {
        // A small sampled image to feed a GUI_Image (its contents are irrelevant to
        // coverage; the point is to drive the blit/upload/descriptor plumbing).
        _image = gfx::Image::Builder{}
                     .setType(gfx::Image::Type::e2D)
                     .setFormat(gfx::Image::Format::eRGBA8_UNORM)
                     .setExtent(glm::uvec2{16, 16})
                     .addUsage(gfx::Image::Usage::eTransferSrc)
                     .addUsage(gfx::Image::Usage::eTransferDst)
                     .addUsage(gfx::Image::Usage::eSampled)
                     .build();
        gfx::CommandBuffer::SingleTimeCommand([&](gfx::CommandBuffer& cb) {
            cb.ClearColorImage(gfx::ResourceRef<const gfx::Image>(_image), glm::vec4{0.3f, 0.6f, 0.9f, 1.f});
        }, gfx::CommandBuffer::Usage::eGraphics);
        _guiImage = gfx::GUI_Image::Create(gfx::ResourceRef<const gfx::Image>(_image));
    }

    void Update() override { ++updates; }

    void Render(gfx::CommandBuffer& cb) override {
        cb.BeginRendering();   // default framebuffer: clears the swap-chain image
        cb.EndRendering();
    }

    void RenderUI(ImGuiContext* context) override {
        ImGui::SetCurrentContext(context);
        ImGui::Begin("gfx test overlay");
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
    gfx::Resource<gfx::Image> _image;
    gfx::Resource<gfx::GUI_Image> _guiImage;
};

// Drives one engine-style frame through the scheduler.
void drawFrame(gfx::Scene& scene) {
    glfwPollEvents();
    gfx::Context::DrainMainThread();
    gfx::Context::Scheduler().Draw([&](gfx::CommandBuffer& cb) {
        gfx::Context::Repository().update();
        scene.Update();
        scene.Render(cb);
        gfx::GUI::Render(cb, scene);
    });
}

std::unique_ptr<gfx::Window> buildWindow(std::unique_ptr<gfx::Scene> scene, const char* title) {
    return gfx::Window::Builder(std::move(scene))
        .setTitle(title)
        .setExtent({320, 240})
        .setResizable(true)
        .setVSync(false)
        .setAPI(gfx::API::eVulkan)
        .build();
}

// A single window lifecycle drives everything: render frames with an ImGui overlay
// (acquire/record/submit/present + the whole ImGui-on-Vulkan path), then resize and
// keep drawing so the next Acquire/Present sees an out-of-date swap chain and
// recreates it (SwapChain::Resize + the default-framebuffer Resize path).
//
// This is deliberately ONE test: a second windowed Vulkan context in the same
// process corrupts the heap (GLFW + the Vulkan/ImGui statics are fully torn down on
// the first window's destruction and don't survive re-initialization).
TEST(WindowedTest, RenderResizeAndPresent) {
    auto scenePtr = std::make_unique<OverlayScene>();
    auto* scene = scenePtr.get();

    std::unique_ptr<gfx::Window> window;
    try {
        window = buildWindow(std::move(scenePtr), "gfx windowed test");
    } catch (const std::exception& e) {
        GTEST_SKIP() << "windowed context unavailable: " << e.what();
    }
    ASSERT_NE(window, nullptr);

    // Touch the font accessor (pure gui.cpp coverage, harmless if null).
    (void)gfx::GUI::GetFont(gfx::Font::Regular);

    // Phase 1: render enough frames to cycle every in-flight frame slot twice.
    for (int i = 0; i < 8 && !window->shouldClose(); ++i) {
        drawFrame(*scene);
        window->LateUpdate();
    }
    EXPECT_GE(scene->updates, 1);

    // Phase 2: resize. Ask the compositor for a new size and pump events so the
    // surface actually changes, then keep drawing to hit the out-of-date/recreate
    // path in the scheduler and swap chain.
    glfwSetWindowSize(**window, 480, 360);
    for (int i = 0; i < 20; ++i) glfwPollEvents();
    for (int i = 0; i < 12 && !window->shouldClose(); ++i) {
        drawFrame(*scene);
        window->LateUpdate();
    }

    window.reset(); // WaitIdle + full teardown of the presentation stack
}

} // namespace
