/**
 * @file koralViewport.h
 * @brief A panel that shows an image you rendered, and tells you how big to render it next time.
 *
 * Showing a render target is one widget — `kui::Image` — and that is not the hard part. The hard part
 * is everything around it: the target has to be re-created when the panel is resized, the image's
 * aspect rarely matches the panel's, input must only reach the scene while the pointer is actually
 * over it, and a pick or a gizmo needs the pointer in the image's own pixels. That is what this holds.
 *
 * @code
 * auto _viewport = std::make_shared<kgui::ViewportState>();
 * kui::Ui _ui { kui::DockSpace(layout, {{"scene", "Scene", kgui::Viewport(_viewport)}}) };
 * void MyScene::Initialize() {
 *     _viewport->SetImage(_colorTarget);   // once: an image resized in place is followed
 * }
 * void MyScene::Update() {
 *     _ui.Update();
 *     if (_viewport->TakeResized()) _colorTarget->Resize(_viewport->Size());
 *     if (_viewport->Hovered()) _camera->ControllerSettings().enable();   // input only over the image
 *     Debug::Get().Gizmo(kor::GizmoMode::eTranslate, _transform, _camera->ViewProjection(), _viewport->GizmoPointer());
 * }
 * @endcode
 *
 * The size is one frame behind, on purpose: the panel learns its size by being laid out, which is after
 * the scene has drawn this frame. A scene resizes its target in the next Update and draws into it in the
 * same frame — one frame of lag, and the picture is right from then on.
 *
 * The image is yours: the viewport neither creates nor owns one, it holds a reference to it. An image
 * that is destroyed leaves the viewport showing nothing rather than a dangling texture. All it asks of
 * the image is kor::Image::Usage::eSampled.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include <kmath/matrix.h>

#include <debugDraw.h>
#include <image.h>
#include <resource.h>

#include <kui/widgets.h>

#include "kgui/export.h"

namespace kgui
{
    /** @brief How the image is laid out inside its panel. */
    enum class ViewportFit : std::uint8_t {
        /** Fills the panel, expecting a target of exactly its size (ViewportState::Size). The default. */
        eStretch,
        /** Keeps the image's proportions, centred, with bars where it does not reach: for an image whose
         *  size you do not control — a loaded texture, a fixed-resolution target. */
        eContain,
    };

    /**
     * @brief What a Viewport shows, and what it tells the scene back. Shared between the two: the scene
     *        sets the image and reads the rest, the viewport does the opposite. Main thread.
     */
    class KGUI_API ViewportState {
    public:
        /** @brief The image to show. Once is enough: an image resized in place is followed. */
        void SetImage(kor::ResourceRef<const kor::Image> image) { _image = std::move(image); }
        [[nodiscard]] const kor::ResourceRef<const kor::Image>& Image() const { return _image; }
        void SetFit(const ViewportFit fit) { _fit = fit; }
        [[nodiscard]] ViewportFit Fit() const { return _fit; }

        /** @brief The resolution the panel would like to be rendered at, in pixels. Zero until it is laid out. */
        [[nodiscard]] kor::UVec2 Size() const { return _size; }
        /** @brief Whether Size changed since this was last asked: when to re-create a render target. */
        [[nodiscard]] bool TakeResized() { return std::exchange(_resized, false); }
        /** @brief Whether the pointer is over the image. What to gate camera and picking input on. */
        [[nodiscard]] bool Hovered() const { return _hovered; }
        /** @brief Whether the viewport was the last thing clicked: what keys that act on it ask. */
        [[nodiscard]] bool Focused() const { return _focused; }
        /** @brief The pointer in the image's own pixels, whatever the panel's size or the fit did; none when it is not over it. */
        [[nodiscard]] std::optional<kor::Vec2> PointerPosition() const;
        /** @brief Whether the left button went down over the image and is still down. */
        [[nodiscard]] bool Dragging() const { return _down; }
        /**
         * @brief The pointer as kor::DebugDraw::Gizmo takes it: in the image's pixels, its button, and
         *        whether it went down since the last time this was asked.
         */
        [[nodiscard]] kor::GizmoPointer GizmoPointer();

    private:
        friend struct ViewportAccess;
        kor::ResourceRef<const kor::Image> _image;
        ViewportFit _fit = ViewportFit::eStretch;
        kor::UVec2 _size { 0 };
        kor::Vec2 _box { 0.f };                 // the panel, in the interface's units
        std::optional<kor::Vec2> _pointer;      // in the same units, from the panel's top-left
        bool _resized = false, _hovered = false, _focused = false, _down = false;
        std::uint64_t _presses = 0, _pressesSeen = 0;
    };

    /** @brief A panel showing @p state's image, sized to it, telling @p state about the pointer over it. */
    KGUI_API kui::Widget Viewport(std::shared_ptr<ViewportState> state);
}
