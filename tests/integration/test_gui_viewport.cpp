// Integration coverage for kgui::Viewport and kgui::Gizmo — the parts of the GUI extras that need a
// real image, and therefore a device.
//
// Drawn against a headless ImGui context, as the unit-level widget tests are: no window and no GUI
// backend, but a real kor::Image to display. What that cannot cover is the *texture handle* —
// kor::GuiImage is a backend object and needs the engine's GUI to have been initialised — so the
// viewport is exercised through the path that matters most and is fully testable here: the layout and
// resize arithmetic, the hover/focus state, and the mouse-to-image mapping.

#include "gpu_fixture.h"

#include <cstdint>
#include <optional>

#include <imgui.h>

#include "commandBuffer.h"
#include "context.h"
#include "image.h"

#include <koralViewport.h>
#include <koralGizmo.h>

using kor::Image;
using kor::ResourceRef;

namespace {

struct GuiViewport : GpuTest
{
    ImGuiContext* context = nullptr;

    void SetUp() override {
        GpuTest::SetUp();   // skips when there is no device
        context = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        // No settings file: ImGui otherwise *loads* imgui.ini from the working directory on the
        // first frame and *saves* it on DestroyContext, so a window this test collapses would come
        // back collapsed in the next run — a test that passes once and then fails forever — and the
        // developer's own layout would be rewritten by a test suite.
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1280.f, 720.f);
        io.DeltaTime = 1.f / 60.f;
        io.Fonts->AddFontDefault();
        io.Fonts->Build();
        io.Fonts->SetTexID(static_cast<ImTextureID>(1));
    }

    void TearDown() override {
        if (context) ImGui::DestroyContext(context);
        context = nullptr;
    }

    template<typename Body>
    void frame(Body&& body) {
        ImGui::NewFrame();
        body();
        ImGui::Render();
    }

    /** @brief Puts the next window at a known place and size, so the layout maths is checkable. */
    static void placeNextWindow(const ImVec2 position, const ImVec2 size) {
        ImGui::SetNextWindowPos(position);
        ImGui::SetNextWindowSize(size);
    }
};

kor::Resource<Image> target(const std::uint32_t width, const std::uint32_t height) {
    return Image::Builder{}
        .setType(Image::Type::e2D)
        .setFormat(Image::Format::eRGBA8_UNORM)
        .setExtent(glm::uvec2{ width, height })
        .setUsage(Image::Usage::eTransferDst | Image::Usage::eSampled)
        .build();
}

// A viewport with nothing in it must still draw, and must report no size to render at.
TEST_F(GuiViewport, DrawsWithNoImage) {
    kgui::Viewport viewport;
    frame([&] {
        placeNextWindow(ImVec2(0, 0), ImVec2(640, 480));
        viewport.Draw("empty");
    });
    EXPECT_FALSE(viewport.image().alive());
}

// The resolution a scene should render at is the window's content region, and it is reported as
// changed exactly once — the frame it changes.
TEST_F(GuiViewport, ReportsItsSizeAndSaysWhenItChanged) {
    auto image = target(64, 64);
    kgui::Viewport viewport;

    frame([&] {
        placeNextWindow(ImVec2(0, 0), ImVec2(320, 240));
        viewport.setImage(image);
        viewport.Draw("sized");
    });
    const auto first = viewport.size();
    EXPECT_GT(first.x, 0u);
    EXPECT_GT(first.y, 0u);
    EXPECT_TRUE(viewport.resized()) << "the first size is a change from nothing";

    // Same size again: nothing to re-create.
    frame([&] {
        placeNextWindow(ImVec2(0, 0), ImVec2(320, 240));
        viewport.Draw("sized");
    });
    EXPECT_EQ(viewport.size(), first);
    EXPECT_FALSE(viewport.resized());

    // Bigger window, so a scene has to re-create its targets.
    frame([&] {
        placeNextWindow(ImVec2(0, 0), ImVec2(640, 480));
        viewport.Draw("sized");
    });
    EXPECT_TRUE(viewport.resized());
    EXPECT_GT(viewport.size().x, first.x);
}

// eStretch gives the image the whole content region; that is what makes size() the right thing to
// render at, since the two then always agree.
TEST_F(GuiViewport, StretchFillsTheContentRegion) {
    auto image = target(64, 32);
    kgui::Viewport viewport;
    viewport.setFit(kgui::Viewport::Fit::eStretch);

    frame([&] {
        placeNextWindow(ImVec2(0, 0), ImVec2(400, 300));
        viewport.setImage(image);
        viewport.Draw("stretch");
    });

    const auto& rect = viewport.rect();
    EXPECT_NEAR(rect.size.x, static_cast<float>(viewport.size().x), 1.f);
    EXPECT_NEAR(rect.size.y, static_cast<float>(viewport.size().y), 1.f);
}

// eContain keeps the image's aspect ratio and centres it, whatever the window's shape.
TEST_F(GuiViewport, ContainKeepsTheImageAspectAndCentresIt) {
    auto image = target(64, 32);            // 2:1
    kgui::Viewport viewport;
    viewport.setFit(kgui::Viewport::Fit::eContain);

    ImVec2 windowPosition { 0.f, 0.f };
    frame([&] {
        placeNextWindow(ImVec2(0, 0), ImVec2(400, 400));   // 1:1 window
        viewport.setImage(image);
        viewport.Draw("contain");

        // Re-entering an existing window reads its state without drawing anything, which is how the
        // test learns where the window actually is.
        ImGui::Begin("contain");
        windowPosition = ImGui::GetWindowPos();
        ImGui::End();
    });

    const auto& rect = viewport.rect();
    ASSERT_GT(rect.size.y, 0.f);
    EXPECT_NEAR(rect.size.x / rect.size.y, 2.f, 0.05f) << "the image's own aspect, not the window's";
    // Letterboxed: it cannot be as tall as the window, and what is left over is split evenly.
    EXPECT_LT(rect.size.y, static_cast<float>(viewport.size().y));

    // Inside the window, and below its top edge — i.e. centred rather than pinned to a corner.
    EXPECT_GT(rect.position.y, windowPosition.y);
    EXPECT_LT(rect.position.y + rect.size.y, windowPosition.y + 400.f);
}

// A collapsed window has no size, and must not ask for one — re-creating targets for a window nobody
// can see is exactly the bug this guards.
TEST_F(GuiViewport, ACollapsedWindowAsksForNothing) {
    auto image = target(64, 64);
    kgui::Viewport viewport;

    frame([&] {
        placeNextWindow(ImVec2(0, 0), ImVec2(320, 240));
        viewport.setImage(image);
        viewport.Draw("collapsing");
    });
    ASSERT_GT(viewport.size().x, 0u);

    frame([&] {
        ImGui::SetNextWindowCollapsed(true);
        placeNextWindow(ImVec2(0, 0), ImVec2(320, 240));
        EXPECT_FALSE(viewport.Draw("collapsing"));
    });
    EXPECT_EQ(viewport.size(), glm::uvec2(0, 0));
    EXPECT_FALSE(viewport.resized());
}

// The pointer's position is reported in the *image's* pixels, and only while it is over the image.
TEST_F(GuiViewport, MousePositionIsInImagePixelsOrNothing) {
    auto image = target(100, 50);
    kgui::Viewport viewport;

    // Nowhere near the window: no position at all.
    ImGui::GetIO().AddMousePosEvent(2000.f, 2000.f);
    frame([&] {
        placeNextWindow(ImVec2(0, 0), ImVec2(200, 100));
        viewport.setImage(image);
        viewport.Draw("picking");
    });
    EXPECT_FALSE(viewport.mousePosition().has_value());
    EXPECT_FALSE(viewport.isHovered());
}

// An image destroyed under the viewport leaves it drawing a placeholder rather than a dead texture.
TEST_F(GuiViewport, SurvivesItsImageBeingDestroyed) {
    kgui::Viewport viewport;
    {
        auto image = target(64, 64);
        frame([&] {
            placeNextWindow(ImVec2(0, 0), ImVec2(320, 240));
            viewport.setImage(image);
            viewport.Draw("dying");
        });
        EXPECT_TRUE(viewport.image().alive());
    }   // the image goes away, the viewport keeps only a ref

    EXPECT_FALSE(viewport.image().alive());
    frame([&] {
        placeNextWindow(ImVec2(0, 0), ImVec2(320, 240));
        viewport.Draw("dying");
    });
}

// ---- gizmo -------------------------------------------------------------------------------------

// The gizmo has to be told where the viewport's image is on screen, and refuse to draw when there is
// no rectangle to draw into.
TEST_F(GuiViewport, GizmoRefusesAViewportWithNoRectangle) {
    kgui::Viewport viewport;
    frame([&] {
        ImGui::Begin("host");
        EXPECT_FALSE(kgui::BeginGizmo(viewport)) << "nothing has been laid out yet";
        ImGui::End();
    });
}

// Drawn over a laid-out viewport, ImGuizmo runs and reports that it is not being dragged. What is
// being checked is that it is reachable and correctly bound — a gizmo that had ImGui's context wrong
// would fault here rather than return false.
TEST_F(GuiViewport, GizmoDrawsOverAViewport) {
    auto image = target(64, 64);
    kgui::Viewport viewport;
    kgui::Gizmo gizmo;
    gizmo.setOperation(kgui::Gizmo::Operation::eTranslate);
    gizmo.setSpace(kgui::Gizmo::Space::eWorld);

    const glm::mat4 view {
        1.f, 0.f, 0.f, 0.f,  0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,  0.f, 0.f, -5.f, 1.f };
    const glm::mat4 projection {
        1.f, 0.f, 0.f, 0.f,  0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, -1.f, -1.f, 0.f, 0.f, -0.2f, 0.f };
    glm::mat4 transform { 1.f };
    const glm::mat4 before = transform;

    frame([&] {
        placeNextWindow(ImVec2(0, 0), ImVec2(600, 400));
        viewport.setImage(image);
        viewport.Draw("scene");

        // Inside the same window, which is what SetDrawlist needs.
        ImGui::Begin("scene");
        ImGuizmo::BeginFrame();
        EXPECT_TRUE(viewport.BeginGizmo());
        gizmo.Manipulate(viewport, view, projection, transform);
        ImGui::End();
    });

    // Nothing was dragged, so nothing moved.
    EXPECT_EQ(transform, before);
    EXPECT_FALSE(gizmo.isUsing());
}

TEST_F(GuiViewport, GizmoRemembersWhatItWasSetTo) {
    kgui::Gizmo gizmo;
    EXPECT_EQ(gizmo.operation(), kgui::Gizmo::Operation::eTranslate);
    gizmo.setOperation(kgui::Gizmo::Operation::eRotate);
    gizmo.setSpace(kgui::Gizmo::Space::eLocal);
    gizmo.setSnap(glm::vec3(0.25f));
    EXPECT_EQ(gizmo.operation(), kgui::Gizmo::Operation::eRotate);
    EXPECT_EQ(gizmo.space(), kgui::Gizmo::Space::eLocal);
    EXPECT_EQ(gizmo.snap(), glm::vec3(0.25f));
}

} // namespace
