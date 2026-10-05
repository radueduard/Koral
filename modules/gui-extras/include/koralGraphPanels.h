/**
 * @file koralGraphPanels.h
 * @brief A frame graph's own panels: how it was scheduled, what it costs, and its passes' settings.
 *
 * The graph keeps the numbers (FrameGraph::Schedule, PassTimings, History, Memory) and each pass says
 * what can be changed about it (RenderPass::Settings); these show them.
 *
 * @code
 * kui::Ui _ui { kui::DockSpace(layout, {
 *     {"graph", "Frame graph", kgui::FrameGraphPanel(Graph())},
 *     {"performance", "Performance", kgui::PerformancePanel(Graph())},
 *     {"passes", "Pass settings", kgui::PassSettings(Graph())},
 * }) };
 * @endcode
 *
 * The graph must outlive the panels — a scene's own graph does, its interface going with the scene.
 */

#pragma once

#include <frameGraph.h>

#include <kui/widgets.h>

#include "kgui/export.h"

namespace kgui
{
    /** @brief The schedule, level by level; what was skipped and culled and kept; memory; a switch per pass. */
    KGUI_API kui::Widget FrameGraphPanel(kor::FrameGraph& graph);
    /** @brief The frame's CPU and GPU time, plotted, and every pass's share of it in a table. */
    KGUI_API kui::Widget PerformancePanel(kor::FrameGraph& graph);
    /** @brief An inspector for each pass that has settings (RenderPass::Settings), under its name. */
    KGUI_API kui::Widget PassSettings(kor::FrameGraph& graph);
}
