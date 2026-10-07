//
// koral-ui's docking, drag and drop and modifier chains, in a window: drag tabs between groups, onto
// edges, into the middle (they float) and out of the window (they get windows of their own); drag a
// colour from the Assets panel onto the Scene.
//

#include <cstdlib>
#include <format>
#include <string>
#include <vector>

#include <app.h>
#include <scene.h>

#include <koralUI.h>

namespace {
    struct SceneView final : kui::StatefulWidget {
        kui::Color fill = kui::Color::Hex(0x2B2F6B);
        kui::Widget Build() override {
            return kui::CustomPaint([c = fill](kui::Canvas& canvas, const kor::Vec2 size) {
                    canvas.DrawRect(kui::Rect::FromSize(size), kui::Paint::Fill(kui::Color::Hex(0x101216)));
                    canvas.DrawCircle(size * 0.5f, std::min(size.x, size.y) * 0.3f, kui::Paint::Fill(c).SetStroke(3.f, kui::colors::White));
                })
                .OnDrop("color", [this](const kui::DragData& d) { SetState([&] { fill = *d.As<kui::Color>(); }); });
        }
    };

    struct Inspector final : kui::StatefulWidget {
        bool shadows = true;
        float exposure = 0.6f;
        int clicks = 0;
        kui::Widget Build() override {
            const kui::Theme& t = kui::Theme::Current();
            return kui::Column({
                kui::Text("Inspector", { .size = 18.f }),
                kui::Checkbox(shadows, [this](const bool v) { SetState([&] { shadows = v; }); }, "Shadows"),
                kui::Text(std::format("Exposure {:.2f}", exposure), { .color = t.textMuted }),
                kui::Slider(exposure, [this](const float v) { SetState([&] { exposure = v; }); }),
                kui::Button(std::format("Clicked {}", clicks), [this] { SetState([&] { ++clicks; }); }),
            }, { .crossAxisAlignment = kui::CrossAxisAlignment::eStart, .gap = 10.f }).Padding(12.f).Align(kui::Alignment::TopLeft());
        }
    };

    kui::Widget Assets() {
        std::vector<kui::Widget> swatches;
        for (const std::uint32_t hex : { 0xE5484Du, 0x46A758u, 0x3E63DDu, 0xF5D90Au, 0x8E4EC6u, 0xF76808u }) {
            const kui::Color color = kui::Color::Hex(hex);
            swatches.push_back(kui::SizedBox(36.f, 36.f).Background(color, 6.f).Draggable({ "color", color }));
        }
        return kui::Column({
            kui::Text("Drag a colour onto the Scene", { .color = kui::Theme::Current().textMuted }),
            kui::Row(std::move(swatches), { .gap = 8.f }),
        }, { .crossAxisAlignment = kui::CrossAxisAlignment::eStart, .gap = 10.f }).Padding(12.f).Align(kui::Alignment::TopLeft());
    }

    kui::Widget Log() {
        std::vector<kui::Widget> lines;
        for (int i = 1; i <= 40; ++i) lines.push_back(kui::Text(std::format("[info] line {} of the log", i), { .size = 12.f }));
        return kui::Column(std::move(lines), { .crossAxisAlignment = kui::CrossAxisAlignment::eStart, .gap = 2.f }).Padding(8.f).Scrollable();
    }

    class Editor final : public kor::Scene {
    public:
        void Initialize() override {
            _layout->Dock("scene")
                .Dock("inspector", kui::DockSide::eRight, "scene", 0.26f)
                .Dock("log", kui::DockSide::eBottom, "scene", 0.3f)
                .Dock("assets", kui::DockSide::eCenter, "log");
            _ui.SetRoot(kui::DockSpace(_layout, {
                { "scene", "Scene", kui::Make<SceneView>(), false },
                { "inspector", "Inspector", kui::Make<Inspector>() },
                { "log", "Log", Log() },
                { "assets", "Assets", Assets() },
            }));
            Graph().Add<kui::UiPass>(_ui);
        }
        void Update() override {
            // For a look without a hand on the mouse: KUI_DEMO_POPOUT gives the Inspector its own window.
            if (++_frame == 30 && std::getenv("KUI_DEMO_POPOUT")) _layout->PopOut("inspector", { 320.f, 280.f });
            if (_frame == 30 && std::getenv("KUI_DEMO_FLOAT")) _layout->Float("assets", kui::Rect::XYWH(260.f, 140.f, 330.f, 190.f));
            _ui.Update();
        }
    private:
        std::shared_ptr<kui::DockLayout> _layout = std::make_shared<kui::DockLayout>();
        kui::Ui _ui;
        int _frame = 0;
    };
}

int main() {
    kor::App app;
    app.Open<Editor>({ .title = "koral-ui docking", .extent = { 1100, 700 } });
    return app.Run();
}
