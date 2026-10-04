//
// koral-ui: a retained-mode interface, in three layers, each usable without the ones above it.
//

/**
 * @file koralUI.h
 * @brief Everything of koral-ui.
 *
 * - **Elements** (kui::ElementShader): a rectangle and a fragment shader that fills it — the base of
 *   everything else, and how a project draws what the built-in shapes cannot.
 * - **The canvas** (kui::Canvas, kui::Paint, kui::Path): lines, arcs, rectangles, circles, triangles,
 *   curves, paths, images and text, recorded into retained kui::Picture objects and shown through
 *   kui::Layer trees.
 * - **Widgets** (kui/widgets.h): declarative building blocks laid out by constraints, rebuilt only
 *   where their state changed.
 *
 * @code
 * // CMakeLists.txt:  target_link_libraries(MyScene PRIVATE Koral koral-ui)
 * @endcode
 */

#pragma once

#include "kui/kuiApi.h"
#include "kui/canvas.h"
#include "kui/text.h"
#include "kui/render.h"
#include "kui/rendering.h"
#include "kui/widgets.h"
#include "kui/dock.h"
