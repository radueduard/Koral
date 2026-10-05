//
// The stats panel: the frame time sampled every frame, summarised and plotted ten times a second; the
// engine's resource counts; and a project's own numbers under them.
//

#include "koralStatsPanel.h"

#include <algorithm>
#include <array>
#include <format>

#include <context.h>
#include <scene.h>

#include "kgui/layout.h"

namespace kgui
{
    // ---- counters --------------------------------------------------------------------------------------

    void StatsCounters::Set(const std::string& name, const long long value) { SetText(name, std::to_string(value)); }

    void StatsCounters::Set(const std::string& name, const double value, const int decimals)
    {
        SetText(name, std::format("{:.{}f}", value, std::clamp(decimals, 0, 9)));
    }

    void StatsCounters::SetText(const std::string& name, std::string value)
    {
        const std::lock_guard lock(_mutex);
        auto& slot = _values[name];
        if (slot == value) return;
        slot = std::move(value);
        ++_revision;
    }

    void StatsCounters::Clear()
    {
        const std::lock_guard lock(_mutex);
        if (_values.empty()) return;
        _values.clear();
        ++_revision;
    }

    std::map<std::string, std::string> StatsCounters::Snapshot() const
    {
        const std::lock_guard lock(_mutex);
        return _values;
    }

    std::uint64_t StatsCounters::Revision() const
    {
        const std::lock_guard lock(_mutex);
        return _revision;
    }

    // ---- the panel -------------------------------------------------------------------------------------

    namespace {
        class StatsPanelWidget final : public Live {
        public:
            explicit StatsPanelWidget(std::shared_ptr<const StatsCounters> counters) : _counters(std::move(counters)) {}

            void DidUpdateWidget(const kui::StatefulWidget& newer) override
            {
                _counters = static_cast<const StatsPanelWidget&>(newer)._counters;
            }

            kui::Widget Build() override
            {
                const Summary summary = Summarise();
                const float fps = summary.mean > 0.f ? 1000.f / summary.mean : 0.f;

                // Scaled to the worst frame rather than to a fixed ceiling, so a hitch is visible instead of
                // clipping off the top; the floor keeps a steady run from looking like noise.
                std::vector<float> history;
                history.reserve(_filled);
                for (std::size_t i = 0; i < _filled; ++i) history.push_back(_frames[(_next + StatsHistory - _filled + i) % StatsHistory]);
                const float ceiling = std::max(summary.worst * 1.2f, 20.f);

                std::vector<kui::Widget> rows {
                    kui::Row({ kui::Text(std::format("{:.1f} fps", fps)), Muted(std::format("({:.2f} ms)", summary.mean)) },
                             kui::FlexOptions {}.SetGap(8.f)),
                    kui::Row({ kui::Text(std::format("worst {:.2f} ms", summary.worst)), Muted(std::format("| 99% {:.2f} ms", summary.percentile99)) },
                             kui::FlexOptions {}.SetGap(8.f)),
                    kui::Plot(std::move(history), kui::PlotOptions {}.SetRange(0.f, ceiling).SetSize({ -1.f, 60.f })),
                    Muted(std::format("{:.1f} s since the window opened", kor::Scene::Current() ? kor::Scene::Time::Elapsed() : 0.f)),
                    kui::Separator(),
                };

                if (!kor::Context::HasRepository()) {
                    rows.push_back(Muted("no repository yet"));
                } else {
                    const auto& repository = kor::Context::Repository();
                    std::vector<kui::Widget> resources { kui::Text(std::format("{} resources tracked", repository.TrackedResources())) };
                    // Poisoned resources are silent by design — they report once and then wait to be repaired
                    // — so this is the one place a broken shader shows up without reading a log.
                    if (const auto unusable = repository.UnusableResources(); unusable > 0)
                        resources.push_back(kui::Text(std::format("({} unusable)", unusable),
                                                      kui::TextStyle {}.SetColor({ 1.00f, 0.38f, 0.35f, 1.f })));
                    rows.push_back(kui::Row(std::move(resources), kui::FlexOptions {}.SetGap(8.f)));
                }

                if (_counters) {
                    std::vector<std::vector<kui::Widget>> cells;
                    for (const auto& [name, value] : _counters->Snapshot()) cells.push_back({ kui::Text(name), kui::Text(value) });
                    if (!cells.empty()) {
                        rows.push_back(kui::Separator());
                        rows.push_back(kui::Table({ { "counter" }, { "value" } }, std::move(cells),
                                                  kui::TableOptions {}.SetHeader(false).SetBorders(false)));
                    }
                }
                return kui::ScrollView(kui::Padding(kui::EdgeInsets::All(10.f),
                    kui::Column(std::move(rows), kui::FlexOptions {}.SetGap(6.f).SetCrossAxisAlignment(kui::CrossAxisAlignment::eStretch))));
            }

        protected:
            bool Poll(const float dt) override
            {
                // Every frame is counted; the panel is shown again ten times a second, or when a counter changed.
                const float milliseconds = (kor::Scene::Current() ? kor::Scene::Time::UnscaledFrameTime() : dt) * 1000.f;
                _frames[_next] = milliseconds;
                _next = (_next + 1) % StatsHistory;
                if (_filled < StatsHistory) ++_filled;
                const std::uint64_t revision = _counters ? _counters->Revision() : 0;
                const bool counted = revision != _revision;
                _revision = revision;
                return Every(0.1f, dt) || counted;
            }

        private:
            struct Summary { float mean = 0.f, worst = 0.f, percentile99 = 0.f; };

            [[nodiscard]] Summary Summarise() const
            {
                if (_filled == 0) return {};
                std::array<float, StatsHistory> sorted {};
                float total = 0.f;
                for (std::size_t i = 0; i < _filled; ++i) {
                    sorted[i] = _frames[i];
                    total += _frames[i];
                }
                std::sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(_filled));
                Summary summary;
                summary.mean = total / static_cast<float>(_filled);
                summary.worst = sorted[_filled - 1];
                // The frame 99% of frames are faster than: what reflects a hitch without being dominated by
                // a single outlier.
                summary.percentile99 = sorted[static_cast<std::size_t>(static_cast<double>(_filled - 1) * 0.99)];
                return summary;
            }

            std::shared_ptr<const StatsCounters> _counters;
            std::array<float, StatsHistory> _frames {};
            std::size_t _next = 0, _filled = 0;
            std::uint64_t _revision = 0;
        };
    }

    kui::Widget StatsPanel(std::shared_ptr<const StatsCounters> counters) { return kui::Make<StatsPanelWidget>(std::move(counters)); }
}
