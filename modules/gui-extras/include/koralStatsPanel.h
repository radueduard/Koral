/**
 * @file koralStatsPanel.h
 * @brief How the frame is going and what the engine is holding, as a koral-ui panel.
 *
 * @code
 * auto _counters = std::make_shared<kgui::StatsCounters>();
 * kui::Ui _ui { kgui::StatsPanel(_counters) };
 * void MyScene::Update() {
 *     _counters->Set("draw calls", _draws);      // whatever your renderer counts
 *     _counters->Set("visible", _visible);
 *     _ui.Update();
 * }
 * @endcode
 *
 * Frame timing comes from kor::Time and the resource counts from the engine's repository, so the
 * panel needs nothing wired up. Counters a project sets itself are shown underneath, which is what
 * keeps a renderer's own numbers out of a fixed list somebody has to extend.
 *
 * The interesting number is not the average, it is the *worst* recent frame: a run at a steady 60 with
 * one 40 ms hitch every second reads as 62 fps on an average and feels broken. So the panel shows the
 * window's high-water mark and the 1%-worst frame beside the mean, and plots the lot.
 */

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include <kui/widgets.h>

#include "kgui/export.h"

namespace kgui
{
    /** @brief A project's own numbers, for a StatsPanel to show under the engine's. Set them whenever. */
    class KGUI_API StatsCounters {
    public:
        /** @brief Shows a number of your own. Overwrites the previous value. */
        void Set(const std::string& name, long long value);
        /** @brief Shows a value of your own, formatted. */
        void Set(const std::string& name, double value, int decimals = 2);
        /** @brief Shows text of your own. */
        void SetText(const std::string& name, std::string value);
        /** @brief Forgets every counter. */
        void Clear();

        /** @brief What there is to show, by name: ordered, so rows do not jump about. */
        [[nodiscard]] std::map<std::string, std::string> Snapshot() const;
        /** @brief Changes each time a counter does: what a panel asks to know whether to show them again. */
        [[nodiscard]] std::uint64_t Revision() const;

    private:
        mutable std::mutex _mutex;
        std::map<std::string, std::string> _values;
        std::uint64_t _revision = 0;
    };

    /** @brief How many frames of history the plot and the statistics cover. */
    inline constexpr std::size_t StatsHistory = 240;

    /** @brief Frame time, its history, the engine's resource counts, and @p counters under them. */
    KGUI_API kui::Widget StatsPanel(std::shared_ptr<const StatsCounters> counters = {});
}
