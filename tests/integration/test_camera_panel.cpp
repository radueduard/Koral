// The camera panel: the seam between the GUI extras and the camera module.
//
// It lives here rather than in the unit suite because it needs both modules and a device behind the
// cameras, and it is drawn against a headless ImGui context — no window, no backend. That is enough
// to catch what actually breaks in ImGui code (an unbalanced Begin/End, a broken id stack, a tree
// left open), because ImGui asserts on every one of those.
//
// It is also the only thing that compiles <koralCameraPanel.h> apart from a consumer: the umbrella
// header deliberately does not include it, since it requires the camera module.

#include "gpu_fixture.h"

#include <imgui.h>

#include <koralCamera.h>
#include <koralCameraPanel.h>

namespace {

struct CameraPanelTest : GpuTest
{
    ImGuiContext* context = nullptr;

    void SetUp() override {
        GpuTest::SetUp();   // skips when there is no device
        context = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        // No settings file: ImGui otherwise loads imgui.ini from the working directory and saves it
        // back on DestroyContext, so a window this test collapses would come back collapsed next run
        // and the developer's own layout would be rewritten by a test suite.
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
};

TEST_F(CameraPanelTest, DrawsBothKindsOfCameraInOneWindow) {
    auto perspective = kcam::PerspectiveCamera::Builder{}
        .setName("player")
        .setController({ .kind = kcam::Controller::Kind::eFly })
        .build();
    auto orthographic = kcam::OrthographicCamera::Builder{}
        .setName("minimap")
        .setController({ .kind = kcam::Controller::Kind::eOrbit })
        .build();
    ASSERT_TRUE(perspective);
    ASSERT_TRUE(orthographic);

    kgui::CameraPanel panel;
    bool open = true;

    frame([&] { panel.Draw("Cameras", &open, *perspective, *orthographic); });
    EXPECT_TRUE(open);

    // Drawing must not move the cameras: a panel reads and writes on edit, and an edit needs input
    // this test does not supply.
    EXPECT_EQ(perspective->controller().kind, kcam::Controller::Kind::eFly);
    EXPECT_EQ(orthographic->controller().kind, kcam::Controller::Kind::eOrbit);
}

TEST_F(CameraPanelTest, DrawsWithNoCamerasAtAllAndInsideAWindowOfTheCallersOwn) {
    kgui::CameraPanel panel;
    bool open = true;
    frame([&] { panel.Draw("Empty", &open); });

    // The other half of the API: one camera, drawn where the cursor is, for a project that puts the
    // controls in its own layout rather than a window of ours.
    auto camera = kcam::PerspectiveCamera::Builder{}.setName("inline").build();
    ASSERT_TRUE(camera);
    frame([&] {
        ImGui::Begin("someone else's window");
        panel.Draw(*camera);
        ImGui::End();
    });
}

// Every controller kind and every aspect source has its own branch in the panel — including the one
// that cannot offer a target to follow and says so instead.
TEST_F(CameraPanelTest, DrawsEveryControllerKindAndAspectSource) {
    auto image = kor::Image::Builder{}
        .setFormat(kor::Image::Format::eRGBA8_UNORM)
        .addUsage(kor::Image::Usage::eColorAttachment)
        .setExtent(glm::uvec2{ 320, 200 })
        .build();
    ASSERT_TRUE(image);

    auto camera = kcam::PerspectiveCamera::Builder{}.setName("cycled").build();
    ASSERT_TRUE(camera);

    kgui::CameraPanel panel;
    bool open = true;

    for (const auto kind : { kcam::Controller::Kind::eNone, kcam::Controller::Kind::eFly,
                             kcam::Controller::Kind::eOrbit }) {
        camera->setController({ .kind = kind });
        frame([&] { panel.Draw("Cameras", &open, *camera); });
    }

    camera->setFollowWindowAspect(true);
    frame([&] { panel.Draw("Cameras", &open, *camera); });

    camera->followAspectOf(image);
    frame([&] { panel.Draw("Cameras", &open, *camera); });
    EXPECT_EQ(camera->aspectSource().kind, kcam::AspectSource::Kind::eImage);
}

// The names an interface shows for a binding come from the engine, not from a table in the panel —
// which is what makes writing your own rebinding interface possible at all. Keys above 128 are the
// case that silently returned "?" before, and they are most of the interesting ones.
TEST(CameraPanelNaming, TheEngineNamesEveryKeyAndButton) {
    EXPECT_EQ(kor::Input::describe(kor::Key::eA), "A");
    EXPECT_EQ(kor::Input::describe(kor::Key::eLeftShift), "Left Shift");
    EXPECT_EQ(kor::Input::describe(kor::Key::eEsc), "Esc");
    EXPECT_EQ(kor::Input::describe(kor::Key::eF11), "F11");
    EXPECT_EQ(kor::Input::describe(kor::Key::eMenu), "Menu");

    EXPECT_EQ(kor::Input::describe(kor::MouseButton::eLeft), "Left Mouse");
    EXPECT_EQ(kor::Input::describe(kor::MouseButton::eMiddle), "Middle Mouse");
    EXPECT_EQ(kor::Input::describe(kor::MouseButton::e5), "Mouse 5");

    // Nothing is pressed in a test, which is the answer a rebind waits on rather than a crash.
    EXPECT_FALSE(kor::Input::firstKeyPressed().has_value());
    EXPECT_FALSE(kor::Input::firstMouseButtonPressed().has_value());
}

} // namespace
