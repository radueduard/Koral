/**
 * @file koralSceneView.h
 * @brief Another scene, or one of this scene's views, shown in a panel of this one's interface: an
 *        editor's game view.
 *
 * The scene shown is offscreen (App::OpenOffscreen) — or it is one of this scene's own views
 * (Scene::AddView), shown the same way without the input. An offscreen scene draws into an image rather
 * than an OS window, and this is what puts that image on screen and gives the scene what a window would
 * have — a size that follows the panel, and the input of whoever is using the panel.
 *
 * @code
 * kor::Scene* _game = kor::Navigator::OpenOffscreen("Level");
 * kui::Ui _ui { kui::DockSpace(layout, {{"game", "Game", kgui::SceneView(*_game)}}) };
 * @endcode
 *
 * - **Size:** the scene's window is resized to the panel (Window::Resize); the scene gets OnResize as it
 *   would from an OS window, one frame after the panel changed.
 * - **Input:** while the pointer is over the panel — or a button pressed over it is still held — the
 *   scene gets this scene's buttons, movement and scroll, with the pointer's position in its own pixels.
 *   While the panel was the last thing clicked it gets the keys. When the panel stops feeding it,
 *   whatever it still held is released, so nothing sticks.
 *
 * Offscreen scenes are drawn before the scenes in OS windows, so the panel shows this frame's picture.
 * The scene, or the view, must outlive the panel.
 */

#pragma once

#include <memory>

#include <scene.h>

#include "koralViewport.h"

namespace kgui
{
    /**
     * @brief A panel showing @p scene's window, sized to the panel and fed its input. @p state, when given,
     *        is the viewport underneath: its pointer, for a gizmo over the scene.
     */
    KGUI_API kui::Widget SceneView(kor::Scene& scene, std::shared_ptr<ViewportState> state = {});
    /** @brief A panel showing one of the current scene's views, sized to the panel. A view shares its scene's input, so nothing is fed. */
    KGUI_API kui::Widget SceneView(kor::View& view, std::shared_ptr<ViewportState> state = {});
}
