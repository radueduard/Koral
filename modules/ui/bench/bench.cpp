//
// What a frame of koral-ui costs: a grid of cells — a rounded box of a colour with a number in it, twenty
// to a row, scrolling — at one load after another, every cell changing and none. What is measured is the
// interface's own work on the CPU, and the whole frame from one to the next with nothing waiting for the
// display.
//
//   koral_ui_bench [frames]     (a Release build: a Debug one measures the debugging)
//

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <app.h>
#include <commandBuffer.h>
#include <frameGraph.h>
#include <scene.h>

#include <koralUI.h>

namespace {
    constexpr int PerRow = 20;
    using Clock = std::chrono::steady_clock;
    double since(const Clock::time_point from) { return std::chrono::duration<double, std::milli>(Clock::now() - from).count(); }

    /** The colour of cell @p index at @p phase: a hue that moves along the grid, as the benchmark scene's does. */
    glm::vec3 hue(const float h)
    {
        const float x = (h - std::floor(h)) * 6.f;
        const auto channel = [x](const float n) {
            const float k = std::fmod(n + x, 6.f);
            return 0.9f - 0.9f * 0.6f * std::max(0.f, std::min({ k, 4.f - k, 1.f }));
        };
        return { channel(5.f), channel(3.f), channel(1.f) };
    }

    class ClearPass final : public kor::RenderPass {
    public:
        ClearPass() : RenderPass("Clear") {}
        void Setup(kor::PassBuilder& b) override { b.Write(kor::FrameGraph::Screen, kor::Image::Usage::eTransferDst); }
        void Initialize(const kor::PassResources& r) override { _screen = r.ImageNamed(kor::FrameGraph::Screen); }
        void Record(kor::CommandBuffer& cb) const override { cb.ClearColorImage(_screen, glm::vec4(0.f, 0.f, 0.f, 1.f)); }
    private:
        kor::ResourceRef<const kor::Image> _screen;
    };

    /** What the scene is told, and says back. */
    struct Load {
        int cells = 0;
        bool animate = true;
        float phase = 0.f;
        double interfaceMs = 0.0;   // the last frame's: the interface's own work, on the CPU
        double detail[8] {};        // koral-ui: build, layout, paint, compose, upload, prepare, kilobytes uploaded, shapes
    };

    // ---- koral-ui ---------------------------------------------------------------------------------------

    kui::Widget grid(const int cells, const float phase)
    {
        kui::TextStyle number { .size = 9.f, .color = kui::colors::Black };
        std::vector<kui::Widget> rows;
        for (int first = 0; first < cells; first += PerRow) {
            std::vector<kui::Widget> row;
            for (int i = first; i < std::min(first + PerRow, cells); ++i) {
                const glm::vec3 c = hue(static_cast<float>(i) * 0.011f + phase);
                row.push_back(kui::Container({
                    .width = 34.f, .height = 20.f,
                    .decoration = { .color = { c.r, c.g, c.b }, .radius = 5.f },
                    .alignment = kui::Alignment::Center(),
                }, kui::Text(std::to_string(i), number, kui::TextAlign::eStart, false)));
            }
            rows.push_back(kui::Row(std::move(row), { .gap = 3.f }));
        }
        return kui::ScrollView(kui::Column(std::move(rows), { .crossAxisAlignment = kui::CrossAxisAlignment::eStart, .gap = 3.f }));
    }

    struct KuiScene final : kor::Scene {
        void Initialize() override
        {
            Graph().Add<ClearPass>();
            Graph().Add<kui::UiPass>(ui);
        }
        void Update() override
        {
            const auto begin = Clock::now();
            // Every cell another colour: the whole grid described again, as a widget that changed would be.
            if (load.animate || load.cells != shown) { ui.SetRoot(grid(load.cells, load.phase)); shown = load.cells; }
            ui.Update();
            load.interfaceMs = since(begin);
            const auto& s = ui.Stats();
            load.detail[0] = s.buildMs; load.detail[1] = s.layoutMs; load.detail[2] = s.paintMs;
            const auto& r = ui.GetRenderer().Stats();               // the frame before's: it is put together after Update
            load.detail[3] = r.composeMs; load.detail[4] = r.uploadMs; load.detail[5] = r.prepareMs;
            load.detail[6] = static_cast<double>(r.uploadedBytes) / 1024.0; load.detail[7] = static_cast<double>(r.instances + r.vertices);
        }
        kui::Ui ui;
        Load load;
        int shown = -1;
    };

    struct Result { double interfaceMs = 0.0, frameMs = 0.0, detail[8] {}; };

    /** @p frames frames of @p load, after a few to settle: what each cost on average. */
    Result measure(kor::App& app, Load& load, const int cells, const bool animate, const int frames)
    {
        load.cells = cells;
        load.animate = animate;
        for (int i = 0; i < 20; ++i) { load.phase += 0.004f; app.Frame(); }
        Result result;
        const auto begin = Clock::now();
        for (int i = 0; i < frames; ++i) {
            load.phase += 0.004f;
            app.Frame();
            result.interfaceMs += load.interfaceMs;
            for (int d = 0; d < 8; ++d) result.detail[d] += load.detail[d];
        }
        result.frameMs = since(begin) / frames;
        result.interfaceMs /= frames;
        for (double& d : result.detail) d /= frames;
        return result;
    }
}

int main(const int argc, char** argv)
{
    const int frames = argc > 1 ? std::max(std::atoi(argv[1]), 10) : 200;
    const std::vector<int> loads { 0, 250, 500, 1000, 2000, 4000, 8000 };
    kor::App app;
    const kor::WindowSettings window { .title = "koral-ui bench", .extent = { 1280, 720 }, .vsync = false };

    std::vector<Result> moving, still;
    auto* scene = app.Open<KuiScene>(window);
    if (!scene) { std::fprintf(stderr, "no window\n"); return 1; }
    for (const int cells : loads) {
        moving.push_back(measure(app, scene->load, cells, true, frames));
        still.push_back(measure(app, scene->load, cells, false, frames));
    }

    std::printf("\n%d frames a load, 1280x720, no vsync. Milliseconds a frame: the interface's own CPU work | the whole frame.\n\n", frames);
    std::printf("%6s | %-21s | %-21s | all changing: build layout paint compose upload prepare | kB up, shapes\n", "cells", "all changing", "unchanged");
    for (std::size_t i = 0; i < loads.size(); ++i) {
        const Result &a = moving[i], &b = still[i];
        std::printf("%6d | %9.3f | %9.3f | %9.3f | %9.3f | %6.3f %6.3f %6.3f %6.3f %6.3f %6.3f | %7.0f %7.0f\n", loads[i],
                    a.interfaceMs, a.frameMs, b.interfaceMs, b.frameMs, a.detail[0], a.detail[1], a.detail[2], a.detail[3], a.detail[4], a.detail[5], a.detail[6], a.detail[7]);
    }
    return 0;
}
