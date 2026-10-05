//
// The viewport — an image in a panel, the panel's size and the pointer over it told back — and the scene
// view, a viewport that shows another scene and drives it as its window would.
//

#include "koralSceneView.h"

#include <algorithm>
#include <cmath>

#include <input.h>
#include <window.h>

#include "kgui/layout.h"

namespace kgui
{
    /** What the viewport widget writes into the state the scene reads. */
    struct ViewportAccess {
        static void Box(ViewportState& s, const glm::vec2 box, const glm::vec2 pixels)
        {
            s._box = box;
            const glm::uvec2 size { static_cast<glm::u32>(std::max(0.f, std::floor(pixels.x))),
                                    static_cast<glm::u32>(std::max(0.f, std::floor(pixels.y))) };
            if (size != s._size && size.x > 0 && size.y > 0) s._resized = true;
            s._size = size;
        }
        static void Hover(ViewportState& s, const std::optional<glm::vec2> at)
        {
            s._hovered = at.has_value();
            if (at || !s._down) s._pointer = at;   // a drag that left the panel keeps its last place
        }
        static void Enter(ViewportState& s) { s._hovered = true; }
        static void Move(ViewportState& s, const glm::vec2 at) { s._pointer = at; }
        static void Press(ViewportState& s, const glm::vec2 at)
        {
            s._pointer = at;
            s._down = s._focused = true;
            ++s._presses;
        }
        static void Release(ViewportState& s)
        {
            s._down = false;
            if (!s._hovered) s._pointer.reset();
        }
        static void Unfocus(ViewportState& s) { s._focused = false; }
    };

    // ---- the state ---------------------------------------------------------------------------------------

    std::optional<glm::vec2> ViewportState::PointerPosition() const
    {
        if (!_pointer || _box.x <= 0.f || _box.y <= 0.f) return std::nullopt;
        // Where the image is in the panel: all of it, or (contained) centred at its own proportions.
        glm::vec2 origin { 0.f }, shown = _box;
        const glm::vec2 extent = _image.Valid() ? glm::vec2(_image->Extent()) : glm::vec2(_size);
        if (_fit == ViewportFit::eContain && extent.x > 0.f && extent.y > 0.f) {
            const float scale = std::min(_box.x / extent.x, _box.y / extent.y);
            shown = extent * scale;
            origin = (_box - shown) * 0.5f;
        }
        const glm::vec2 normalized = (*_pointer - origin) / shown;
        // Outside the image is not over it — unless a drag that began on it is still going.
        if (!_down && (normalized.x < 0.f || normalized.x > 1.f || normalized.y < 0.f || normalized.y > 1.f)) return std::nullopt;
        return normalized * (extent.x > 0.f ? extent : glm::vec2(_size));
    }

    kor::GizmoPointer ViewportState::GizmoPointer()
    {
        kor::GizmoPointer pointer;
        pointer.position = PointerPosition();
        const glm::vec2 extent = _image.Valid() ? glm::vec2(_image->Extent()) : glm::vec2(_size);
        pointer.viewport = extent;
        pointer.down = _down;
        pointer.pressed = _presses != _pressesSeen;
        _pressesSeen = _presses;
        return pointer;
    }

    // ---- the viewport ------------------------------------------------------------------------------------

    namespace {
        class ViewportWidget final : public Live {
        public:
            explicit ViewportWidget(std::shared_ptr<ViewportState> state) : _state(std::move(state)) {}

            void DidUpdateWidget(const kui::StatefulWidget& newer) override
            {
                _state = static_cast<const ViewportWidget&>(newer)._state;
            }

            kui::Widget Build() override
            {
                if (!_state) return Muted("no viewport");
                const auto& image = _state->Image();
                _shown = image.Valid() ? image.Get() : nullptr;
                _generation = image.Valid() ? image->Generation() : 0;
                _fit = _state->Fit();

                // No image, or one that has been destroyed: say so rather than draw a dangling texture. A
                // scene that re-creates its targets passes through this for a frame.
                kui::Widget content = image.Valid()
                    ? kui::Image(image, _fit == ViewportFit::eContain ? kui::ImageFit::eContain : kui::ImageFit::eFill)
                    : kui::Center(Muted("no image"));

                const auto state = _state;
                kui::GestureOptions pointer;
                pointer.onHover = [state](const glm::vec2 at) { ViewportAccess::Hover(*state, at); };
                pointer.onEnter = [state] { ViewportAccess::Enter(*state); };
                pointer.onExit = [state] { ViewportAccess::Hover(*state, std::nullopt); };
                pointer.onTapDown = [state](const glm::vec2 at) { ViewportAccess::Press(*state, at); };
                pointer.onTapUp = [state] { ViewportAccess::Release(*state); };
                pointer.onPanStart = [state](const glm::vec2 at) { ViewportAccess::Move(*state, at); };
                pointer.onPanUpdate = [state](glm::vec2, const glm::vec2 at) { ViewportAccess::Move(*state, at); };
                pointer.onPanEnd = [state] { ViewportAccess::Release(*state); };

                return kui::SizeObserver([state](const glm::vec2 size, const glm::vec2 pixels) { ViewportAccess::Box(*state, size, pixels); },
                                         kui::FractionallySizedBox(1.f, 1.f, kui::GestureDetector(std::move(pointer), std::move(content))));
            }

        protected:
            bool Poll(float) override
            {
                if (!_state) return false;
                // Clicked anywhere else: it no longer has the keys.
                if (const kor::Scene* host = kor::Scene::Current(); host && !_state->Hovered() && host->SceneInput().FirstMouseButtonPressed())
                    ViewportAccess::Unfocus(*_state);
                // A different image, or the same one resized in place, is shown again.
                const auto& image = _state->Image();
                return (image.Valid() ? image.Get() : nullptr) != _shown
                    || (image.Valid() && image->Generation() != _generation) || _state->Fit() != _fit;
            }

        private:
            std::shared_ptr<ViewportState> _state;
            const void* _shown = nullptr;
            glm::u64 _generation = 0;
            ViewportFit _fit = ViewportFit::eStretch;
        };

        // ---- the scene view ------------------------------------------------------------------------------

        constexpr kor::MouseButton HeldButtons[] { kor::MouseButton::eLeft, kor::MouseButton::eRight, kor::MouseButton::eMiddle };

        /** A viewport over another scene's window — or a view's — sized to the panel and, for a scene, fed its input. */
        class SceneViewWidget final : public Live {
        public:
            SceneViewWidget(kor::Scene* scene, kor::View* view, std::shared_ptr<ViewportState> state)
                : _scene(scene), _view(view), _state(state ? std::move(state) : std::make_shared<ViewportState>()) {}

            void DidUpdateWidget(const kui::StatefulWidget& newer) override
            {
                const auto& other = static_cast<const SceneViewWidget&>(newer);
                _scene = other._scene;
                _view = other._view;
            }

            kui::Widget Build() override { return Viewport(_state); }

        protected:
            bool Poll(float) override
            {
                kor::Window& window = _scene ? _scene->SceneWindow() : _view->Target();
                _state->SetImage(_scene ? window.Image() : _view->Image());
                // The size it would like, as a window being resized by hand would ask for it.
                if (const glm::uvec2 size = _state->Size(); size.x > 0 && size.y > 0 && size != window.Extent()) {
                    if (_scene) window.Resize(size);
                    else _view->Resize(size);
                }
                if (_scene) Feed();
                return false;
            }

        private:
            /** The host's input, handed on while the pointer is over the panel or the panel has the keys. */
            void Feed()
            {
                const kor::Scene* host = kor::Scene::Current();
                if (!host || host == _scene) return;
                const kor::Input& from = host->SceneInput();
                kor::Input& to = _scene->SceneInput();
                // A drag that began over the panel keeps going to it until the button comes up.
                const bool anyHeld = std::ranges::any_of(HeldButtons, [&](const kor::MouseButton b) {
                    return from.MouseButtonState(b) == kor::KeyState::eHeld || from.MouseButtonState(b) == kor::KeyState::ePressed;
                });
                const bool hovered = _state->Hovered();
                if (hovered && from.FirstMouseButtonPressed()) _dragging = true;
                if (!anyHeld) _dragging = false;
                const bool mouse = hovered || _dragging;
                const bool keys = _state->Focused();
                if (mouse || keys) {
                    if (const auto position = _state->PointerPosition()) to.FeedMousePosition(*position);
                    to.FeedFrom(from, keys, mouse);
                }
                // It stops being fed: nothing it held stays held.
                if ((_feedingMouse && !mouse) || (_feedingKeys && !keys)) to.ReleaseAll();
                _feedingMouse = mouse;
                _feedingKeys = keys;
            }

            kor::Scene* _scene;
            kor::View* _view;
            std::shared_ptr<ViewportState> _state;
            bool _dragging = false, _feedingMouse = false, _feedingKeys = false;
        };
    }

    kui::Widget Viewport(std::shared_ptr<ViewportState> state) { return kui::Make<ViewportWidget>(std::move(state)); }

    kui::Widget SceneView(kor::Scene& scene, std::shared_ptr<ViewportState> state)
    {
        return kui::Make<SceneViewWidget>(&scene, nullptr, std::move(state));
    }

    kui::Widget SceneView(kor::View& view, std::shared_ptr<ViewportState> state)
    {
        return kui::Make<SceneViewWidget>(nullptr, &view, std::move(state));
    }
}
