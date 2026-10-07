//
// A frame graph's panels: its schedule and switches, its timing, and its passes' settings — all read from
// what the graph keeps, a few times a second.
//

#include "koralGraphPanels.h"

#include <algorithm>
#include <format>
#include <map>

#include <context.h>

#include "kgui/layout.h"
#include "koralInspector.h"

namespace kgui
{
    namespace {
        constexpr kui::Color Bad { 1.f, 0.4f, 0.4f, 1.f };

        kui::Widget Column(std::vector<kui::Widget> rows, const float gap = 4.f)
        {
            return kui::Column(std::move(rows), kui::FlexOptions {}.SetGap(gap).SetCrossAxisAlignment(kui::CrossAxisAlignment::eStretch));
        }

        kui::Widget Panel(std::vector<kui::Widget> rows)
        {
            return kui::ScrollView(kui::Padding(kui::EdgeInsets::All(10.f), Column(std::move(rows), 6.f)));
        }

        double megabytes(const kor::u64 bytes) { return static_cast<double>(bytes) / (1024.0 * 1024.0); }

        /** A heading over a list of names, each on its own line. */
        void list(std::vector<kui::Widget>& rows, std::string heading, const std::vector<std::string>& names)
        {
            if (names.empty()) return;
            rows.push_back(kui::Separator());
            rows.push_back(Muted(std::move(heading)));
            for (const auto& name : names) rows.push_back(kui::Text("  • " + name));
        }

        // ---- the schedule ----------------------------------------------------------------------------------

        class FrameGraphWidget final : public Live {
        public:
            explicit FrameGraphWidget(kor::FrameGraph& graph) : _graph(&graph) {}
            void DidUpdateWidget(const kui::StatefulWidget& newer) override { _graph = static_cast<const FrameGraphWidget&>(newer)._graph; }

            kui::Widget Build() override
            {
                const kor::FrameGraph& graph = *_graph;
                std::vector<kui::Widget> rows { Muted("Passes on one level do not depend on each other.") };
                for (const auto& [name, level, async] : graph.Schedule())
                    rows.push_back(kui::Text(std::format("{}{}  {}{}", std::string(level * 2, ' '), level, name, async ? "  (async compute)" : "")));
                if (std::ranges::any_of(graph.Schedule(), &kor::FrameGraph::Scheduled::async) && !kor::Context::SupportsAsyncCompute())
                    rows.push_back(Muted("This device has no async compute queue: async passes run in order."));

                std::vector<std::string> skipped;
                for (const auto& [name, resource, source] : graph.SkippedPasses())
                    skipped.push_back(std::format("{}: needs '{}' from {}", name, resource, source));
                list(rows, "Skipped (an input comes from a disabled pass):", skipped);
                list(rows, "Culled (nothing reads what they make):", graph.CulledPasses());
                list(rows, "Kept for the next frame:", graph.KeptForNextFrame());

                const auto& memory = graph.Memory();
                rows.push_back(kui::Separator());
                rows.push_back(kui::Text(std::format("Memory  {:.1f} MB: {} resources in {} allocations",
                                                     megabytes(memory.bytes), memory.resources, memory.allocations)));
                if (memory.unsharedBytes > memory.bytes)
                    rows.push_back(Muted(std::format("{:.1f} MB saved by sharing", megabytes(memory.unsharedBytes - memory.bytes))));
                rows.push_back(kui::Checkbox(graph.Aliasing(), [this](const bool on) { _graph->SetAliasing(on); SetState(); },
                                             "Share memory between resources"));

                rows.push_back(kui::Separator());
                for (kor::RenderPass* pass : graph.Passes())
                    rows.push_back(kui::Checkbox(pass->Enabled(), [this, pass](const bool on) { pass->SetEnabled(on); SetState(); }, pass->Name()));
                if (graph.Broken())
                    rows.push_back(kui::Text("The graph does not compile; see the log.", kui::TextStyle {}.SetColor(Bad)));
                return Panel(std::move(rows));
            }

        protected:
            bool Poll(const float dt) override { return Every(0.5f, dt); }

        private:
            kor::FrameGraph* _graph;
        };

        // ---- the timing ------------------------------------------------------------------------------------

        class PerformanceWidget final : public Live {
        public:
            explicit PerformanceWidget(kor::FrameGraph& graph) : _graph(&graph) {}
            void DidUpdateWidget(const kui::StatefulWidget& newer) override { _graph = static_cast<const PerformanceWidget&>(newer)._graph; }

            kui::Widget Build() override
            {
                const kor::FrameGraph& graph = *_graph;
                auto history = graph.Times();

                // The frame as a whole: CPU wall time between frames, and the graph's GPU time.
                float average = 0.f, worst = 0.f;
                for (const float ms : history.frameMs) { average += ms; worst = std::max(worst, ms); }
                if (!history.frameMs.empty()) average /= static_cast<float>(history.frameMs.size());
                const float ceiling = std::max(worst * 1.2f, 16.7f);

                const auto timing = graph.Timing();
                std::vector<kui::Widget> rows {
                    kui::Text(std::format("Frame  {:.2f} ms  ({:.0f} fps)   worst {:.2f} ms", average, average > 0.f ? 1000.f / average : 0.f, worst)),
                    kui::Plot(std::move(history.frameMs), kui::PlotOptions {}.SetRange(0.f, ceiling).SetSize({ -1.f, 50.f }).SetOverlay("CPU frame time")),
                    kui::Plot(std::move(history.gpuMs), kui::PlotOptions {}.SetRange(0.f, ceiling).SetSize({ -1.f, 50.f }).SetOverlay("GPU, frame graph")),
                    kui::Text(std::format("Graph GPU {:.2f} ms   prepare {:.2f} ms   record {:.2f} ms ({:.2f} ms of work across threads)",
                                          timing.gpuMs, timing.prepareMs, timing.recordWallMs, timing.recordWorkMs)),
                    kui::Separator(),
                };

                // Pass by pass, in the order they run, with a bar for each one's share of the GPU time.
                std::vector<std::vector<kui::Widget>> cells;
                for (const auto& pass : graph.PassTimings()) {
                    cells.push_back({
                        Line(pass.name),
                        pass.gpuMeasured ? kui::Text(std::format("{:.3f}", pass.gpuMs)) : Muted("n/a"),
                        kui::ProgressBar(timing.gpuMs > 0.0 ? static_cast<float>(pass.gpuMs / timing.gpuMs) : 0.f),
                        kui::Text(std::format("{:.3f}", pass.recordMs)),
                        kui::Text(std::format("{:.3f}", pass.prepareMs)),
                    });
                }
                rows.push_back(kui::Table({ { "Pass", -1.f, 1.4f }, { "GPU ms", 64.f }, { "share", -1.f, 1.f }, { "Record ms", 76.f }, { "Prepare ms", 80.f } },
                                          std::move(cells)));
                rows.push_back(Muted("Averaged over recent frames. GPU times arrive once the GPU has run the frame."));
                return Panel(std::move(rows));
            }

        protected:
            bool Poll(const float dt) override { return Every(0.25f, dt); }

        private:
            kor::FrameGraph* _graph;
        };

        // ---- the passes' settings --------------------------------------------------------------------------

        class PassSettingsWidget final : public Live {
        public:
            explicit PassSettingsWidget(kor::FrameGraph& graph) : _graph(&graph) {}
            void DidUpdateWidget(const kui::StatefulWidget& newer) override { _graph = static_cast<const PassSettingsWidget&>(newer)._graph; }

            kui::Widget Build() override
            {
                _passes = _graph->Passes();
                std::vector<kui::Widget> rows;
                for (kor::RenderPass* pass : _passes) {
                    const kor::Ref settings = pass->Settings();
                    if (!settings.Valid()) continue;
                    const bool open = !_closed.contains(pass);
                    rows.push_back(kui::CollapsingHeader(pass->Name(), open, [this, pass](const bool now) {
                        SetState([&] { if (now) _closed.erase(pass); else _closed[pass] = true; });
                    }, Inspector(settings, [pass] { pass->SettingsChanged(); })).Key(pass->Name()));
                }
                if (rows.empty()) rows.push_back(Muted("No pass has settings (RenderPass::Settings)."));
                return Panel(std::move(rows));
            }

        protected:
            /** A pass added or taken away: shown again. Their settings keep themselves up to date. */
            bool Poll(float) override { return _graph->Passes() != _passes; }

        private:
            kor::FrameGraph* _graph;
            std::vector<kor::RenderPass*> _passes;
            std::map<const kor::RenderPass*, bool> _closed;
        };
    }

    kui::Widget FrameGraphPanel(kor::FrameGraph& graph) { return kui::Make<FrameGraphWidget>(graph); }
    kui::Widget PerformancePanel(kor::FrameGraph& graph) { return kui::Make<PerformanceWidget>(graph); }
    kui::Widget PassSettings(kor::FrameGraph& graph) { return kui::Make<PassSettingsWidget>(graph); }
}
