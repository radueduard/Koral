/**
 * @file koralSceneView.h
 * @brief Another scene, shown in a panel of this one's interface: an editor's game view.
 *
 * The scene shown is offscreen (App::OpenOffscreen): it draws into an image rather than an OS window,
 * and this is what puts that image on screen and gives the scene what a window would have — a size
 * that follows the panel, and the input of whoever is using the panel.
 *
 * @code
 * kor::Scene* _game = nullptr;
 * kgui::SceneView _gameView;
 *
 * void Editor::Initialize() { _game = kor::Navigator::OpenOffscreen("Level"); }
 * void Editor::RenderUI()   { _gameView.Draw("Game", *_game); }
 * @endcode
 *
 * - **Size:** the scene's window is resized to the panel (Window::Resize); the scene gets OnResize
 *   as it would from an OS window, one frame after the panel changed.
 * - **Input:** while the pointer is over the panel — or a button pressed over it is still held — the
 *   scene gets this scene's buttons, movement and scroll, with the pointer's position in its own
 *   pixels. While the panel has keyboard focus it gets the keys. When the panel stops feeding it,
 *   whatever it still held is released, so nothing sticks.
 *
 * Offscreen scenes are drawn before the scenes in OS windows, so the panel shows this frame's picture.
 */

#pragma once

#include <algorithm>

#include "koralViewport.h"

#include <input.h>
#include <scene.h>
#include <window.h>

namespace kgui
{
    /** @brief A panel showing another scene's window, sized to the panel and fed its input. */
    class SceneView
    {
    public:
        /**
         * @brief Draws the panel.
         * @param title Its title, which is also its ImGui id.
         * @param scene The scene to show; normally an offscreen one.
         * @param open Optional flag the panel's close button clears.
         * @return Whether the panel is open and has a size.
         *
         * Call it from Scene::RenderUI, once a frame.
         */
        bool Draw(const char* title, kor::Scene& scene, bool* open = nullptr)
        {
            auto& window = scene.SceneWindow();
            _viewport.SetImage(window.Image());
            const bool drawn = _viewport.Draw(title, open);

            // The size it would like, as a window being resized by hand would ask for it.
            if (drawn && _viewport.size() != window.Extent()) window.Resize(_viewport.size());

            Feed(scene, drawn);
            return drawn;
        }

        /** @brief The viewport underneath, for what a Viewport offers: its rectangle, a gizmo over it. */
        [[nodiscard]] Viewport& View() { return _viewport; }
        [[nodiscard]] const Viewport& View() const { return _viewport; }

        /** @brief Whether the scene is being fed input at the moment. */
        [[nodiscard]] bool IsFeeding() const { return _feedingMouse || _feedingKeys; }

    private:
        void Feed(kor::Scene& scene, const bool drawn)
        {
            const kor::Scene* host = kor::Scene::Current();
            if (!host || host == &scene) return;
            const kor::Input& from = host->SceneInput();
            kor::Input& to = scene.SceneInput();

            // A drag that began over the panel keeps going to it until the button comes up.
            const bool anyHeld = std::ranges::any_of(heldButtons, [&](const kor::MouseButton b) {
                return from.MouseButtonState(b) == kor::KeyState::eHeld || from.MouseButtonState(b) == kor::KeyState::ePressed;
            });
            const bool hovered = drawn && _viewport.IsHovered();
            if (hovered && from.FirstMouseButtonPressed()) _dragging = true;
            if (!anyHeld) _dragging = false;

            const bool mouse = hovered || _dragging;
            const bool keys = drawn && _viewport.IsFocused();
            if (mouse || keys) {
                if (const auto position = _viewport.MousePosition()) to.FeedMousePosition(*position);
                to.FeedFrom(from, keys, mouse);
            }
            // It stops being fed: nothing it held stays held.
            if ((_feedingMouse && !mouse) || (_feedingKeys && !keys)) to.ReleaseAll();
            _feedingMouse = mouse;
            _feedingKeys = keys;
        }

        static constexpr kor::MouseButton heldButtons[] = { kor::MouseButton::eLeft, kor::MouseButton::eRight, kor::MouseButton::eMiddle };

        Viewport _viewport;
        bool _dragging = false;
        bool _feedingMouse = false;
        bool _feedingKeys = false;
    };
}
