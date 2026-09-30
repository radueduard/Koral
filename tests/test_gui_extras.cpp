// Smoke tests for the GUI extras module's widgets, drawn against a headless ImGui context.
//
// No window, no backend, no GPU: ImGui will run a whole frame into its own draw lists given a display
// size and a font atlas, which is all these widgets need. That is enough to catch what actually breaks
// in ImGui code — an unbalanced Begin/End, a table left open, a bad id stack — because ImGui asserts on
// every one of those, and an assert here fails the test rather than a user's build.
//
// Header-only widgets are otherwise compiled by nobody but their consumers, so this is also the only
// place their code is exercised at all.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include <imgui.h>

#include <log.h>

#include <koralGuiExtras.h>

namespace {

/**
 * @brief An ImGui context with no backend, and a frame to draw into.
 *
 * ImGui needs three things before NewFrame: a display size, a built font atlas, and a texture id for
 * it (there being no renderer to supply one). With those it renders into memory quite happily.
 */
struct HeadlessImGui : testing::Test
{
    ImGuiContext* context = nullptr;

    void SetUp() override {
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

    /** @brief Runs one whole frame, drawing whatever @p body draws. */
    template<typename Body>
    void frame(Body&& body) {
        ImGui::NewFrame();
        body();
        ImGui::Render();
    }
};

TEST_F(HeadlessImGui, LogPanelDrawsWhatWasLogged) {
    kor::log::ClearHistory();
    kor::log::Info("a quiet message");
    kor::log::Warn("something to look at");
    kor::log::Error("something to fix");

    kgui::LogPanel panel;
    frame([&] { panel.Draw(); });

    // The history is the panel's only state, and drawing must not consume it.
    EXPECT_EQ(kor::log::History().size(), 3u);

    // Drawn a second time with the levels filtered down, which is the branch a reader uses most.
    panel.SetShows(kor::log::Level::eInfo, false);
    EXPECT_FALSE(panel.Shows(kor::log::Level::eInfo));
    frame([&] { panel.Draw("Log", nullptr); });

    kor::log::ClearHistory();
}

TEST_F(HeadlessImGui, LogPanelSurvivesAnEmptyHistoryAndABigOne) {
    kgui::LogPanel panel;

    kor::log::ClearHistory();
    frame([&] { panel.Draw(); });

    for (int i = 0; i < 500; ++i) kor::log::Info("message {}", i);
    frame([&] { panel.Draw(); });
    kor::log::ClearHistory();
}

// Wrapping is what makes the rows different heights, and different heights are what the panel's own
// culling exists for — ImGuiListClipper cannot do them. A message far wider than the window is the
// case that exercises it: measured, skipped over and drawn from offsets rather than a fixed line
// height. Nothing here can assert *where* a line landed (there is no window to look at), but ImGui
// does assert on a cursor left somewhere impossible, a broken id stack, or an unbalanced push.
TEST_F(HeadlessImGui, LogPanelWrapsLinesTooLongForItsWidth) {
    kor::log::ClearHistory();

    const std::string wide(600, 'x');   // several lines' worth at any sane panel width
    kor::log::Info("{}", wide);
    kor::log::Warn("short one");
    kor::log::Error("{}", wide);

    kgui::LogPanel panel;

    // Narrow, then wide: the second frame must re-measure, since every cached height belongs to a
    // width that is no longer the panel's.
    frame([&] {
        ImGui::SetNextWindowSize(ImVec2(220.f, 300.f));
        panel.Draw();
    });
    frame([&] {
        ImGui::SetNextWindowSize(ImVec2(900.f, 300.f));
        panel.Draw();
    });

    // Times off changes the text of every line, and so its height. Same story.
    panel.SetShowsTimes(false);
    frame([&] { panel.Draw(); });

    // So does the padding: it is inside the highlight, so it both adds to a row's height and takes
    // width away from the text that wraps in it.
    panel.SetPadding(ImVec2(20.f, 10.f));
    EXPECT_FLOAT_EQ(panel.Padding().x, 20.f);
    frame([&] {
        ImGui::SetNextWindowSize(ImVec2(220.f, 300.f));
        panel.Draw();
    });

    // Rounding is drawn, not measured — but a negative value means "whatever the application's style
    // says", and that branch should be walked at least once.
    panel.SetRounding(6.f);
    frame([&] { panel.Draw(); });
    panel.SetRounding(-1.f);
    frame([&] { panel.Draw(); });

    kor::log::ClearHistory();
}

// A selection is by sequence number, not by position, so it survives the log growing underneath it —
// which is the only reason it is worth keeping at all in a list that scrolls on its own.
TEST_F(HeadlessImGui, LogPanelSelectionOutlivesTheLinesAroundIt) {
    kor::log::ClearHistory();
    kor::log::Info("the interesting one");

    const auto chosen = kor::log::LastSequence();
    ASSERT_NE(chosen, 0u);

    kgui::LogPanel panel;
    EXPECT_EQ(panel.Selected(), 0u);

    panel.Select(chosen);
    frame([&] { panel.Draw(); });
    EXPECT_EQ(panel.Selected(), chosen);

    for (int i = 0; i < 50; ++i) kor::log::Info("noise {}", i);
    frame([&] { panel.Draw(); });
    EXPECT_EQ(panel.Selected(), chosen);

    // Selecting something that is no longer in the history is not an error: it simply highlights
    // nothing, which is what happens as a chosen line ages out of the log.
    kor::log::ClearHistory();
    frame([&] { panel.Draw(); });
    EXPECT_EQ(panel.Selected(), chosen);

    panel.Select(0);
    EXPECT_EQ(panel.Selected(), 0u);
}

// The history is a bounded ring: once it is full, a frame can drop as many records off the front as it
// gains at the back, leaving the count unchanged while every index shifts. The filtered list is indices
// into that, so a panel that decides "same size, nothing to do" would draw the wrong lines.
TEST_F(HeadlessImGui, LogPanelKeepsUpWhenTheHistoryRollsOver) {
    const auto limit = kor::log::HistoryLimit();
    kor::log::SetHistoryLimit(8);
    kor::log::ClearHistory();

    kgui::LogPanel panel;
    for (int i = 0; i < 8; ++i) kor::log::Info("first {}", i);
    frame([&] { panel.Draw(); });

    // Exactly as many again: the count is the same before and after, the contents entirely different.
    for (int i = 0; i < 8; ++i) kor::log::Info("second {}", i);
    frame([&] { panel.Draw(); });

    const auto history = kor::log::History();
    ASSERT_EQ(history.size(), 8u);
    EXPECT_EQ(history.front().message, "second 0");

    kor::log::SetHistoryLimit(limit);
    kor::log::ClearHistory();
}

// The panel's own culling, on its own. Wrapped lines are not all the same height, so ImGuiListClipper
// cannot do this and the panel keeps running offsets instead: which of them a view covers is pure
// arithmetic, and the only part of the change that fails silently — get it wrong and the list is empty
// rather than broken. Four lines, at 0, 10, 30 and 40, the last ten tall.
TEST(LogPanelCulling, PicksOutTheLinesAViewCovers) {
    using kgui::log_detail::LinesIn;
    const std::vector<float> offsets { 0.f, 10.f, 30.f, 40.f, 50.f };

    // A view over the middle takes every line it touches, including the one it starts part-way down.
    EXPECT_EQ(LinesIn(offsets, 15.f, 35.f).first, 1u);
    EXPECT_EQ(LinesIn(offsets, 15.f, 35.f).last,  3u);

    // Scrolled to the very top, where the cursor sits below the scroll origin by the window's padding
    // and `from` is *negative*: the first line, not the one before it — which does not exist.
    EXPECT_EQ(LinesIn(offsets, -4.f, 20.f).first, 0u);
    EXPECT_EQ(LinesIn(offsets, -4.f, 20.f).last,  2u);

    // A view taller than the list: all of it, and no more than all of it.
    EXPECT_EQ(LinesIn(offsets, 0.f, 500.f).first, 0u);
    EXPECT_EQ(LinesIn(offsets, 0.f, 500.f).last,  4u);

    // Past the end, which is where a stale scroll position lands after the log is cleared.
    EXPECT_EQ(LinesIn(offsets, 200.f, 300.f).first, 4u);
    EXPECT_EQ(LinesIn(offsets, 200.f, 300.f).last,  4u);

    // Exactly on a boundary: a line starting at the bottom edge is the first one *not* drawn.
    EXPECT_EQ(LinesIn(offsets, 10.f, 30.f).first, 1u);
    EXPECT_EQ(LinesIn(offsets, 10.f, 30.f).last,  2u);

    // Nothing measured yet, and one lone offset with no line after it.
    EXPECT_EQ(LinesIn(std::vector<float>{}, 0.f, 100.f).last, 0u);
    EXPECT_EQ(LinesIn(std::vector<float>{ 0.f }, 0.f, 100.f).last, 0u);
}

TEST_F(HeadlessImGui, StatsPanelDrawsWithNoDeviceAtAll) {
    kgui::StatsPanel panel;
    // Two frames, because the first has no history to summarise and the second does — the branch that
    // divides by the sample count is the one worth reaching.
    frame([&] { panel.Draw(); });
    frame([&] { panel.Draw(); });
}

TEST_F(HeadlessImGui, StatsPanelShowsCountersOfYourOwn) {
    kgui::StatsPanel panel;
    panel.Set("draw calls", 1234ll);
    panel.Set("cull time", 0.42, 3);
    panel.SetText("renderer", "forward+");

    frame([&] { panel.Draw(); });

    panel.ClearCounters();
    frame([&] { panel.Draw(); });
}

// Long enough to wrap the frame-time ring buffer, which is where an off-by-one would live.
TEST_F(HeadlessImGui, StatsPanelWrapsItsHistory) {
    kgui::StatsPanel panel;
    for (std::size_t i = 0; i < kgui::StatsPanel::History + 10; ++i) {
        frame([&] { panel.Draw(); });
    }
}

TEST_F(HeadlessImGui, GradientEditorAndFileBrowserDraw) {
    kgui::GradientEditor gradient;
    ImGui::FileBrowser browser;

    frame([&] {
        ImGui::Begin("widgets");
        gradient.Draw("Falloff");
        ImGui::End();

        browser.Display();   // closed: draws nothing, and must not assert
    });
}

namespace inspected {
    enum class Mode { eOff, eOn };
    KORAL_REFLECT_ENUM(Mode, eOff, eOn)
    struct Part { glm::vec3 color{1.f}; std::string name = "part"; };
    inline void KoralReflect(kor::TypeBuilder<Part>& t) {
        t.Name("InspectedPart");
        t.Field("color", &Part::color).Color();
        t.Field("name", &Part::name).Tooltip("What it is called");
    }
    struct Everything {
        bool flag = true; std::int32_t count = 3; std::uint8_t small = 2; float speed = 1.f; double precise = 0.5;
        std::string label = "hello"; glm::vec2 v2{0.f}; glm::vec4 v4{1.f}; glm::ivec3 cell{1}; glm::uvec2 size{4};
        glm::quat rotation{1.f, 0.f, 0.f, 0.f}; glm::mat4 transform{1.f}; Mode mode = Mode::eOn;
        Part part; std::vector<Part> parts{Part{}, Part{}}; std::uint64_t id = 7;
    };
    inline void KoralReflect(kor::TypeBuilder<Everything>& t) {
        t.Name("InspectedEverything");
        t.Field("flag", &Everything::flag); t.Field("count", &Everything::count).Range(0, 10);
        t.Field("small", &Everything::small); t.Field("speed", &Everything::speed); t.Field("precise", &Everything::precise);
        t.Field("label", &Everything::label); t.Field("v2", &Everything::v2); t.Field("v4", &Everything::v4);
        t.Field("cell", &Everything::cell); t.Field("size", &Everything::size); t.Field("rotation", &Everything::rotation);
        t.Field("transform", &Everything::transform); t.Field("mode", &Everything::mode); t.Field("part", &Everything::part);
        t.Field("parts", &Everything::parts); t.Field("id", &Everything::id).ReadOnly();
    }
}

// Every kind of field gets its editor, nested structs and arrays opened, in a real ImGui frame — and
// drawing it changes nothing.
TEST_F(HeadlessImGui, TheInspectorDrawsAnEditorForEveryKindOfField) {
    inspected::Everything everything;
    const std::string before = kor::ToJson(everything);
    bool changed = true;
    for (int i = 0; i < 2; ++i) {
        frame([&] {
            ImGui::Begin("Properties");
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);
            changed = kgui::Inspect("Everything", everything);
            ImGui::End();
        });
    }
    EXPECT_FALSE(changed);
    EXPECT_EQ(kor::ToJson(everything), before);
}

} // namespace
