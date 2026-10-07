// The GUI extras' panels, shown in a koral-ui interface over an offscreen scene: what they say, read off
// the interface (kui::debug::Texts), and what they do when they are typed into or pointed at. Their own
// executable, as an application is one per process.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include <app.h>
#include <context.h>
#include <frameGraph.h>
#include <log.h>
#include <scheduler.h>

#include <kui/render.h>
#include <kui/widgets.h>

#include <koralCamera.h>
#include <koralCameraPanel.h>
#include <koralGuiExtras.h>

namespace {
    constexpr kor::UVec2 Size { 480, 360 };

    std::unique_ptr<kor::App> s_app;
    std::string s_reason = "no device";

    class Environment final : public ::testing::Environment {
    public:
        void SetUp() override {
            try { s_app = std::make_unique<kor::App>(kor::AppSettings{ .platform = kor::WindowPlatform::eNone }); }
            catch (const std::exception& e) { s_reason = e.what(); s_app.reset(); }
        }
        void TearDown() override { s_app.reset(); }
    };
    const auto* const s_environment = ::testing::AddGlobalTestEnvironment(new Environment);

    void settle() {
        s_app->Frame();
        kor::Context::Scheduler().WaitIdle();
    }

    /** A pass with settings of its own, for the pass settings panel. */
    struct Glow { float strength = 0.5f; bool on = true; };
    KORAL_REFLECT(Glow, strength, on)

    class GlowPass final : public kor::RenderPass {
    public:
        GlowPass() : RenderPass("Glow") {}
        void Setup(kor::PassBuilder& b) override { b.Write(kor::FrameGraph::Screen, kor::Image::Usage::eTransferDst); }
        void Initialize(const kor::PassResources& r) override { _screen = r.ImageNamed(kor::FrameGraph::Screen); }
        void Record(kor::CommandBuffer& cb) const override { cb.ClearColorImage(_screen, kor::Vec4(0.f, 0.f, 0.f, 1.f)); }
        kor::Ref Settings() override { return settings; }
        void SettingsChanged() override { ++changes; }
        Glow settings;
        int changes = 0;
    private:
        kor::ResourceRef<const kor::Image> _screen;
    };

    class Panels final : public kor::Scene {
    public:
        void Initialize() override {
            glow = &Graph().Add<GlowPass>();
            Graph().Add<kui::UiPass>(ui);
        }
        void Update() override { ui.Update(); }
        kui::Ui ui;
        GlowPass* glow = nullptr;
    };

    class GuiExtras : public ::testing::Test {
    protected:
        void SetUp() override {
            if (!s_app) GTEST_SKIP() << "no Vulkan device: " << s_reason;
            scene = s_app->OpenOffscreen<Panels>({ .title = "gui extras", .extent = Size });
            ASSERT_NE(scene, nullptr);
            settle();
        }
        void TearDown() override {
            if (scene) { s_app->Close(*scene); settle(); }
        }
        void Show(kui::Widget root) { scene->ui.SetRoot(std::move(root)); settle(); settle(); }
        [[nodiscard]] std::vector<std::string> Texts() const { return kui::debug::Texts(scene->ui); }
        [[nodiscard]] bool Says(const std::string& part) const {
            return std::ranges::any_of(Texts(), [&](const std::string& t) { return t.find(part) != std::string::npos; });
        }
        void Press(const kor::Key key) {
            auto& input = scene->SceneInput();
            input.FeedKey(key, true); settle(); input.FeedKey(key, false); settle();
        }
        /** Frames until a panel that refreshes a few times a second has. */
        void Wait() { std::this_thread::sleep_for(std::chrono::milliseconds(150)); settle(); settle(); }
        Panels* scene = nullptr;
    };
}

// ---- the log ----------------------------------------------------------------------------------------------

TEST_F(GuiExtras, TheLogPanelShowsWhatWasLoggedAndKeepsUp) {
    kor::log::Info("before the panel: marker one");
    kor::log::Warn("a warning to count");
    Show(kgui::LogPanel());
    EXPECT_TRUE(Says("marker one")) << "what was logged before it opened";
    EXPECT_TRUE(Says("warn (")) << "each level, counted on its toggle";

    kor::log::Error("after the panel: marker two");
    settle(); settle();
    EXPECT_TRUE(Says("marker two")) << "and what is logged while it is open";
}

TEST_F(GuiExtras, TheLogPanelFiltersByWhatIsTyped) {
    kor::log::Info("apples are red");
    kor::log::Info("pears are green");
    Show(kgui::LogPanel({ .showTimes = false }));
    ASSERT_TRUE(Says("apples are red"));
    // Tab: the keyboard to the first field there is — the filter.
    Press(kor::Key::eTab);
    scene->SceneInput().FeedText(U"PEAR");
    settle(); settle();
    EXPECT_TRUE(Says("pears are green"));
    EXPECT_FALSE(Says("apples are red")) << "filtered out, whatever the case typed";
}

// ---- the statistics ---------------------------------------------------------------------------------------

TEST_F(GuiExtras, TheStatsPanelShowsTheFrameAndCountersOfYourOwn) {
    auto counters = std::make_shared<kgui::StatsCounters>();
    counters->Set("draw calls", 42LL);
    counters->Set("ratio", 0.5, 3);
    Show(kgui::StatsPanel(counters));
    EXPECT_TRUE(Says("fps"));
    EXPECT_TRUE(Says("resources tracked"));
    EXPECT_TRUE(Says("draw calls"));
    EXPECT_TRUE(Says("42"));
    EXPECT_TRUE(Says("0.500"));

    counters->Set("draw calls", 43LL);
    settle(); settle();
    EXPECT_TRUE(Says("43")) << "a counter changed is shown changed";
    counters->Clear();
    settle(); settle();
    EXPECT_FALSE(Says("draw calls"));
}

// ---- the inspector ----------------------------------------------------------------------------------------

namespace inspected {
    enum class Mode { eOff, eOn };
    struct Light {
        std::string name = "lamp";
        float intensity = 1.f;
        kor::Vec3 color { 1.f };
        Mode mode = Mode::eOn;
        std::vector<int> tags { 1, 2 };
    };
    KORAL_REFLECT_ENUM(Mode, eOff, eOn)
    inline void KoralReflect(kor::TypeBuilder<Light>& t) {
        t.Name("Light");
        t.Field("name", &Light::name);
        t.Field("intensity", &Light::intensity).Range(0.f, 10.f).Tooltip("how bright");
        t.Field("color", &Light::color).Color();
        t.Field("mode", &Light::mode);
        t.Field("tags", &Light::tags);
    }
}

TEST_F(GuiExtras, TheInspectorEditsAReflectedObjectAndFollowsIt) {
    inspected::Light light;
    int changes = 0;
    Show(kgui::Inspector(light, [&] { ++changes; }, "Light"));
    EXPECT_TRUE(Says("intensity"));
    EXPECT_TRUE(Says("1.000")) << "a float, as a drag";
    EXPECT_TRUE(Says("eOn")) << "an enum, by name";
    EXPECT_TRUE(Says("tags [2]")) << "an array, with its length";
    EXPECT_TRUE(Says("lamp")) << "a string, in a field";

    // Typed into: the name is the first field Tab reaches.
    Press(kor::Key::eTab);
    auto& input = scene->SceneInput();
    input.FeedKey(kor::Key::eLeftControl, true);
    Press(kor::Key::eA);
    input.FeedKey(kor::Key::eLeftControl, false);
    input.FeedText(U"sun");
    settle(); settle();
    EXPECT_EQ(light.name, "sun");
    EXPECT_GT(changes, 0);

    // Changed by the program: shown changed.
    light.intensity = 7.25f;
    Wait();
    EXPECT_TRUE(Says("7.250"));
}

// ---- the viewport -----------------------------------------------------------------------------------------

TEST_F(GuiExtras, AViewportSaysItsSizeAndWhereThePointerIsInTheImage) {
    auto image = kor::Image::Builder{}.SetFormat(kor::Image::Format::eRGBA8_UNORM)
        .SetUsage(kor::Image::Usage::eSampled | kor::Image::Usage::eColorAttachment).SetExtent(kor::UVec2{ 240, 180 }).Build();
    ASSERT_TRUE(image.Valid());
    auto state = std::make_shared<kgui::ViewportState>();
    Show(kgui::Viewport(state));
    EXPECT_FALSE(state->Image().Valid());
    EXPECT_TRUE(Says("no image"));

    state->SetImage(image);
    settle(); settle();
    EXPECT_EQ(state->Size(), Size) << "as big as the panel, in pixels";
    EXPECT_TRUE(state->TakeResized());
    EXPECT_FALSE(state->TakeResized()) << "told once";

    // Over it: the pointer, in the image's own pixels — half the panel's here.
    auto& input = scene->SceneInput();
    input.FeedMousePosition({ 240.f, 90.f });
    settle(); settle();
    ASSERT_TRUE(state->Hovered());
    const auto at = state->PointerPosition();
    ASSERT_TRUE(at.has_value());
    EXPECT_NEAR(at->x, 120.f, 1.f);
    EXPECT_NEAR(at->y, 45.f, 1.f);

    // Pressed: a gizmo hears it once.
    input.FeedMouseButton(kor::MouseButton::eLeft, true);
    settle(); settle();
    auto pointer = state->GizmoPointer();
    EXPECT_TRUE(pointer.down);
    EXPECT_TRUE(pointer.pressed);
    EXPECT_EQ(pointer.viewport, kor::Vec2(240.f, 180.f));
    EXPECT_FALSE(state->GizmoPointer().pressed) << "a press is heard once";
    input.FeedMouseButton(kor::MouseButton::eLeft, false);
    settle(); settle();
    EXPECT_FALSE(state->GizmoPointer().down);

    // Contained: the image keeps its proportions, and the bars beside it are not over it.
    state->SetFit(kgui::ViewportFit::eContain);
    image->Resize(kor::UVec3{ 120, 180, 1 });   // taller than the panel's shape: bars left and right
    settle(); settle();
    input.FeedMousePosition({ 20.f, 180.f });
    settle(); settle();
    EXPECT_FALSE(state->PointerPosition().has_value()) << "over a bar, not the image";
    input.FeedMousePosition({ 240.f, 180.f });
    settle(); settle();
    ASSERT_TRUE(state->PointerPosition().has_value());
    EXPECT_NEAR(state->PointerPosition()->x, 60.f, 1.f) << "the middle of the image";

    // Destroyed under it: nothing shown, nothing broken.
    image = {};
    settle(); settle();
    EXPECT_TRUE(Says("no image"));
}

// ---- the frame graph's panels -----------------------------------------------------------------------------

TEST_F(GuiExtras, TheGraphPanelsShowTheScheduleTheTimingAndThePassesSettings) {
    Show(kui::Column({
        kui::SizedBox(-1.f, 120.f, kgui::FrameGraphPanel(scene->Graph())),
        kui::SizedBox(-1.f, 120.f, kgui::PerformancePanel(scene->Graph())),
        kui::SizedBox(-1.f, 120.f, kgui::PassSettings(scene->Graph())),
    }));
    Wait();
    EXPECT_TRUE(Says("Glow")) << "a pass, by name";
    EXPECT_TRUE(Says("Memory"));
    EXPECT_TRUE(Says("Share memory between resources"));
    EXPECT_TRUE(Says("CPU frame time")) << "the plot's label";
    EXPECT_TRUE(Says("strength")) << "the pass's settings, inspected";
    EXPECT_TRUE(Says("0.500"));

    // Changed in the inspector: the pass hears of it. Its one text field is the first Tab reaches... it has
    // none, so its drag: the first focusable drag is the strength.
    scene->glow->settings.strength = 0.75f;
    Wait();
    EXPECT_TRUE(Says("0.750")) << "the settings follow the pass";
}

// ---- the file browser -------------------------------------------------------------------------------------

TEST_F(GuiExtras, TheFileBrowserListsFoldersFirstAndOnlyTheFilesAskedFor) {
    const auto root = std::filesystem::temp_directory_path() / "koral-gui-extras-browser";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "textures");
    std::ofstream(root / "a.png") << "x";
    std::ofstream(root / "b.txt") << "x";
    std::ofstream(root / ".hidden.png") << "x";

    std::filesystem::path chosen;
    Show(kgui::FileBrowser({ .start = root, .extensions = { ".png" } }, [&](const std::filesystem::path& p) { chosen = p; }));
    EXPECT_TRUE(Says("textures/")) << "a folder, marked as one";
    EXPECT_TRUE(Says("a.png"));
    EXPECT_FALSE(Says("b.txt")) << "not an extension asked for";
    EXPECT_FALSE(Says(".hidden")) << "hidden, unless asked";

    const auto texts = Texts();
    const auto folder = std::ranges::find_if(texts, [](const std::string& t) { return t == "textures/"; });
    const auto file = std::ranges::find_if(texts, [](const std::string& t) { return t == "a.png"; });
    EXPECT_LT(folder - texts.begin(), file - texts.begin()) << "folders first";
    std::filesystem::remove_all(root);
}

// ---- the camera panel -------------------------------------------------------------------------------------

TEST_F(GuiExtras, TheCameraPanelShowsBothKindsOfCameraAndTheirControllers) {
    auto perspective = kcam::PerspectiveCamera::Builder{}.SetName("player").SetController({ .kind = kcam::Controller::Kind::eFly }).Build();
    auto orthographic = kcam::OrthographicCamera::Builder{}.SetName("minimap").SetController({ .kind = kcam::Controller::Kind::eOrbit }).Build();
    ASSERT_TRUE(perspective);
    ASSERT_TRUE(orthographic);
    Show(kgui::CameraPanel(*perspective, *orthographic));
    EXPECT_TRUE(Says("player (perspective)"));
    EXPECT_TRUE(Says("minimap (orthographic)"));
    EXPECT_TRUE(Says("fov"));
    EXPECT_TRUE(Says("orbit target"));
    // Showing them must not move them: the panel reads, and writes only on an edit.
    EXPECT_EQ(perspective->ControllerSettings().kind, kcam::Controller::Kind::eFly);
    EXPECT_EQ(orthographic->ControllerSettings().kind, kcam::Controller::Kind::eOrbit);

    // A camera following an image says so, and offers the way back.
    auto image = kor::Image::Builder{}.SetFormat(kor::Image::Format::eRGBA8_UNORM)
        .SetUsage(kor::Image::Usage::eColorAttachment).SetExtent(kor::UVec2{ 320, 200 }).Build();
    perspective->FollowAspectOf(image);
    Wait();
    EXPECT_TRUE(Says("aspect follows an image"));
    EXPECT_TRUE(Says("release"));
}

TEST_F(GuiExtras, TheCameraPanelShowsNothingToEditWithNoCameras) {
    Show(kgui::CameraPanel());
    EXPECT_TRUE(Says("no cameras"));
}

// The names an interface shows for a binding come from the engine, not from a table in the panel — which is
// what makes writing your own rebinding interface possible at all.
TEST(CameraPanelNaming, TheEngineNamesEveryKeyAndButton) {
    EXPECT_EQ(kor::Input::Describe(kor::Key::eA), "A");
    EXPECT_EQ(kor::Input::Describe(kor::Key::eLeftShift), "Left Shift");
    EXPECT_EQ(kor::Input::Describe(kor::Key::eEsc), "Esc");
    EXPECT_EQ(kor::Input::Describe(kor::Key::eF11), "F11");
    EXPECT_EQ(kor::Input::Describe(kor::MouseButton::eLeft), "Left Mouse");
    EXPECT_EQ(kor::Input::Describe(kor::MouseButton::e5), "Mouse 5");
}
