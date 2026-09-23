//
// Created by radue on 30.07.2026.
//

/**
 * @file koralGizmo.h
 * @brief ImGuizmo, in glm, drawn into a kgui::Viewport.
 *
 * ImGuizmo itself is available whole — `#include <ImGuizmo.h>` and use it — and this is the thin part
 * around it: its matrices are `const float*`, its rectangle has to be set to wherever the viewport's
 * image landed on screen, and it has to be told which ImGui context it is drawing into (it keeps its
 * own, being a separate library).
 *
 * @code
 * kgui::Viewport _viewport;
 * kgui::Gizmo _gizmo;
 * glm::mat4 _transform { 1.f };
 *
 * void MyScene::RenderUI() {
 *     _viewport.Draw("Scene");   // its image was set once, at Initialize
 *
 *     if (_gizmo.Manipulate(_viewport, _camera->view(), _camera->projection(), _transform))
 *         onTransformEdited(_transform);
 * }
 * @endcode
 *
 * Keyboard shortcuts and a mode toggle are not here: which key means "rotate" is a project's decision,
 * and @ref kgui::Gizmo::setOperation is how you make it.
 */

#pragma once
#include <cstdint>

#include <glm/glm.hpp>

#include <imgui.h>

#include <ImGuizmo.h>

#include "koralViewport.h"

namespace kgui
{
    /**
     * @brief Prepares ImGuizmo to draw inside @p viewport, for a hand-written ImGuizmo call.
     *
     * kgui::Gizmo does this for you; use it directly when you want ImGuizmo's own API — DrawGrid,
     * ViewManipulate, bounds handles — inside a viewport.
     */
    inline bool BeginGizmo(const Viewport& viewport)
    {
        const auto& rect = viewport.rect();
        if (rect.size.x <= 0.f || rect.size.y <= 0.f) return false;

        // Into *this* window's draw list, or the gizmo is drawn behind the image; and over the image's
        // own rectangle, or it is drawn in the wrong place — which under eContain is not the window's
        // content region.
        ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
        ImGuizmo::SetRect(rect.position.x, rect.position.y, rect.size.x, rect.size.y);
        return true;
    }

    /**
     * @brief A transform handle: translate, rotate or scale a glm::mat4 in a viewport.
     *
     * Holds only what it is set to do — the operation, the space, and an optional snap — so a scene can
     * keep one per gizmo or one for the lot.
     */
    class Gizmo
    {
    public:
        /** @brief What the handle does. Mirrors ImGuizmo's operations, in the three that are usual. */
        enum class Operation : std::uint8_t { eTranslate, eRotate, eScale, eUniversal };

        /** @brief Whether the handle is aligned to the object or to the world. */
        enum class Space : std::uint8_t { eLocal, eWorld };

        void setOperation(const Operation operation) { _operation = operation; }
        [[nodiscard]] Operation operation() const { return _operation; }

        void setSpace(const Space space) { _space = space; }
        [[nodiscard]] Space space() const { return _space; }

        /**
         * @brief Snaps the handle's steps: metres for translate, degrees for rotate, factor for scale.
         *
         * One value per axis. Zero — the default — snaps nothing.
         */
        void setSnap(const glm::vec3 snap) { _snap = snap; }
        [[nodiscard]] glm::vec3 snap() const { return _snap; }

        /** @brief Whether the handle is being dragged right now. Gate camera input on the inverse. */
        [[nodiscard]] bool isUsing() const { return ImGuizmo::IsUsing(); }

        /** @brief Whether the pointer is over any part of the handle. */
        [[nodiscard]] bool isHovered() const { return ImGuizmo::IsOver(); }

        /**
         * @brief Draws the handle over @p viewport and edits @p transform.
         * @param viewport The viewport it belongs to; its rectangle is where the handle is drawn.
         * @param view The camera's view matrix.
         * @param projection The camera's projection matrix.
         * @param transform The transform to manipulate, edited in place.
         * @param deltaOut Optionally receives the change this frame, for applying it to a selection.
         * @return Whether @p transform was changed.
         *
         * Must be called while the viewport's window is the current one — from Scene::RenderUI, right
         * after Viewport::Draw.
         */
        bool Manipulate(const Viewport& viewport, const glm::mat4& view, const glm::mat4& projection,
                        glm::mat4& transform, glm::mat4* deltaOut = nullptr) const
        {
            if (!BeginGizmo(viewport)) return false;

            glm::mat4 delta { 1.f };
            const bool changed = ImGuizmo::Manipulate(
                &view[0][0], &projection[0][0],
                toImGuizmo(_operation), toImGuizmo(_space),
                &transform[0][0], &delta[0][0],
                _snap == glm::vec3(0.f) ? nullptr : &_snap.x);

            if (changed && deltaOut) *deltaOut = delta;
            return changed;
        }

    private:
        static ImGuizmo::OPERATION toImGuizmo(const Operation operation)
        {
            switch (operation) {
            case Operation::eRotate:    return ImGuizmo::ROTATE;
            case Operation::eScale:     return ImGuizmo::SCALE;
            case Operation::eUniversal: return ImGuizmo::UNIVERSAL;
            default:                    return ImGuizmo::TRANSLATE;
            }
        }

        static ImGuizmo::MODE toImGuizmo(const Space space)
        {
            return space == Space::eWorld ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
        }

        Operation _operation = Operation::eTranslate;
        Space _space = Space::eWorld;
        glm::vec3 _snap { 0.f };
    };

    // Defined here rather than in koralViewport.h: the viewport itself has no business knowing about
    // ImGuizmo, and a project that only wants to show an image should not have to compile it.
    inline bool Viewport::BeginGizmo() const { return kgui::BeginGizmo(*this); }
}
