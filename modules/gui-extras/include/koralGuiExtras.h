/**
 * @file koralGuiExtras.h
 * @brief The GUI extras: panels built on koral-ui that are useful without being the engine's business.
 *
 * - kgui::LogPanel — what has been logged, filtered.
 * - kgui::StatsPanel — frame time, the engine's counts, a project's own.
 * - kgui::Inspector — an editor for any reflected object.
 * - kgui::Viewport and kgui::SceneView — an image, or another scene, in a panel, with the pointer over it.
 * - kgui::FrameGraphPanel, PerformancePanel, PassSettings — a frame graph's own panels.
 * - kgui::FileBrowser — picking a file or a folder.
 * - kgui::CameraPanel (koralCameraPanel.h, with the camera module) — a camera's pose, projection and controls.
 *
 * Each is a kui::Widget: put it in a dock panel, a tab, or a column of your own. A gradient's stops are
 * edited with koral-ui's own kui::GradientEditor, and a transform with kor::DebugDraw::Gizmo — over a
 * Viewport, with its GizmoPointer.
 */

#pragma once

#include "kgui/export.h"
#include "kgui/layout.h"
#include "koralFileBrowser.h"
#include "koralGraphPanels.h"
#include "koralInspector.h"
#include "koralLogPanel.h"
#include "koralSceneView.h"
#include "koralStatsPanel.h"
#include "koralViewport.h"
