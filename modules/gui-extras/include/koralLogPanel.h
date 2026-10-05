/**
 * @file koralLogPanel.h
 * @brief What the engine, the scene and every module have logged, as a koral-ui panel.
 *
 * @code
 * kui::Ui _ui { kui::DockSpace(layout, {{"log", "Log", kgui::LogPanel()}, ...}) };
 * @endcode
 *
 * It reads kor::log's history, which every image in the process records into — so a message written
 * by the engine, by a scene and by a module all land here, in order, whichever thread wrote them.
 * Nothing has to be routed to it.
 *
 * Levels to filter by, each with how many there are; a text filter; follow-the-tail; times. Lines wrap
 * rather than run off the right edge, each one highlights under the pointer and can be selected,
 * hovering it tells its level, its sequence number and when it was logged, and right-clicking one
 * copies it or narrows the view to its level. Only the lines in view are built, however long the log.
 */

#pragma once

#include <kui/widgets.h>

#include "kgui/export.h"

namespace kgui
{
    /** @brief How a LogPanel starts out; the panel's own toggles change it from there. */
    struct LogPanelOptions {
        bool followTail = true;     ///< Keeps the newest message in view while the view is at the bottom.
        bool showTimes = true;      ///< Each line starts with when it was logged.
    };

    /** @brief A panel showing kor::log's history: levels, a filter, follow-the-tail. */
    KGUI_API kui::Widget LogPanel(LogPanelOptions options = {});
}
