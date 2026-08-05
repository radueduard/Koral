//
// Created by radue on 30.07.2026.
//

/**
 * @file koralStatsPanel.h
 * @brief A window showing how the frame is going, and what the engine is holding.
 *
 * @code
 * kgui::StatsPanel _stats;
 *
 * void MyScene::RenderUI() {
 *     _stats.Set("draw calls", _draws);      // whatever your renderer counts
 *     _stats.Set("visible", _visible, "of %zu", _total);
 *     _stats.Draw();
 * }
 * @endcode
 *
 * Frame timing comes from kor::Time and the resource counts from the engine's repository, so the
 * panel needs nothing wired up. Counters a project sets itself are shown underneath, which is what
 * keeps a renderer's own numbers out of a fixed list somebody has to extend.
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstddef>
#include <map>
#include <string>

#include <imgui.h>

#include <context.h>
#include <gtime.h>
#include <resource.h>

namespace kgui
{
    /**
     * @brief Frame time, its history, and the counts the engine can answer for.
     *
     * The interesting number is not the average, it is the *worst* recent frame: a run at a steady 60
     * with one 40 ms hitch every second reads as 62 fps on an average and feels broken. So the panel
     * shows the window's high-water mark and the 1%-worst frame beside the mean, and plots the lot.
     */
    class StatsPanel
    {
    public:
        /** @brief How many frames of history the plot and the statistics cover. */
        static constexpr std::size_t kHistory = 240;

        /**
         * @brief Draws the window.
         * @param title Its title, which is also its ImGui id.
         * @param open Optional flag the window's close button clears.
         *
         * Call it once a frame: it samples the frame time as a side effect of drawing, so calling it
         * twice in a frame would count that frame twice.
         */
        void Draw(const char* title = "Statistics", bool* open = nullptr)
        {
            sample();

            if (!ImGui::Begin(title, open)) { ImGui::End(); return; }

            drawFrameTime();
            ImGui::Separator();
            drawResources();
            if (!_counters.empty()) {
                ImGui::Separator();
                drawCounters();
            }

            ImGui::End();
        }

        /** @brief Shows a number of your own, under the engine's. Overwrites the previous value. */
        void Set(const std::string& name, const long long value)
        {
            char text[64];
            std::snprintf(text, sizeof(text), "%lld", value);
            _counters[name] = text;
        }

        /** @brief Shows a value of your own, formatted. @see Set */
        void Set(const std::string& name, const double value, const int decimals = 2)
        {
            char text[64];
            std::snprintf(text, sizeof(text), "%.*f", decimals, value);
            _counters[name] = text;
        }

        /** @brief Shows text of your own. @see Set */
        void SetText(const std::string& name, std::string value) { _counters[name] = std::move(value); }

        /** @brief Forgets every counter set by Set/SetText. */
        void ClearCounters() { _counters.clear(); }

    private:
        /** @brief Records this frame's time, oldest sample falling off the end. */
        void sample()
        {
            const float milliseconds = kor::Time::FrameTime() * 1000.f;
            _frames[_next] = milliseconds;
            _next = (_next + 1) % kHistory;
            if (_filled < kHistory) ++_filled;
        }

        /** @brief mean, worst, and the 99th percentile of the window, in milliseconds. */
        struct Summary { float mean = 0.f, worst = 0.f, percentile99 = 0.f; };

        [[nodiscard]] Summary summarise() const
        {
            if (_filled == 0) return {};

            std::array<float, kHistory> sorted { };
            float total = 0.f;
            for (std::size_t i = 0; i < _filled; ++i) {
                sorted[i] = _frames[i];
                total += _frames[i];
            }
            std::sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(_filled));

            Summary summary;
            summary.mean = total / static_cast<float>(_filled);
            summary.worst = sorted[_filled - 1];
            // The frame 99% of frames are faster than: the number that reflects a hitch without being
            // dominated by a single outlier.
            const auto at = static_cast<std::size_t>(static_cast<double>(_filled - 1) * 0.99);
            summary.percentile99 = sorted[at];
            return summary;
        }

        void drawFrameTime() const
        {
            const auto summary = summarise();
            const float fps = summary.mean > 0.f ? 1000.f / summary.mean : 0.f;

            ImGui::Text("%.1f fps", fps);
            ImGui::SameLine();
            ImGui::TextDisabled("(%.2f ms)", summary.mean);

            ImGui::Text("worst %.2f ms", summary.worst);
            ImGui::SameLine();
            ImGui::TextDisabled("| 99%% %.2f ms", summary.percentile99);

            // Scaled to the worst frame rather than to a fixed ceiling, so a hitch is visible instead
            // of clipping off the top; the floor keeps a steady run from looking like noise.
            const float ceiling = std::max(summary.worst * 1.2f, 20.f);
            ImGui::PlotLines("##frames", _frames.data(), static_cast<int>(_filled),
                             static_cast<int>(_next % kHistory), nullptr, 0.f, ceiling, ImVec2(-FLT_MIN, 60.f));

            ImGui::TextDisabled("%.1f s since the window opened", kor::Time::WindowTime());
        }

        static void drawResources()
        {
            if (!kor::Context::HasRepository()) {
                ImGui::TextDisabled("no repository yet");
                return;
            }

            const auto& repository = kor::Context::Repository();
            const auto tracked = repository.trackedResources();
            const auto unusable = repository.unusableResources();

            ImGui::Text("%zu resources tracked", tracked);
            if (unusable > 0) {
                // Poisoned resources are silent by design — they report once and then wait to be
                // repaired — so this is the one place a broken shader shows up without reading a log.
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 0.38f, 0.35f, 1.f));
                ImGui::Text("(%zu unusable)", unusable);
                ImGui::PopStyleColor();
            }
        }

        void drawCounters() const
        {
            if (!ImGui::BeginTable("##counters", 2, ImGuiTableFlags_SizingStretchProp)) return;
            for (const auto& [name, value] : _counters) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(name.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(value.c_str());
            }
            ImGui::EndTable();
        }

        std::array<float, kHistory> _frames { };
        std::size_t _next = 0;
        std::size_t _filled = 0;
        std::map<std::string, std::string> _counters;   // ordered, so rows do not jump about
    };
}
