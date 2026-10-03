//
// koral-ui: a widget tree, live — input in, a layer tree out, and as little work as the frame allows.
//

#include <algorithm>
#include <array>
#include <chrono>

#include <optional>

#include <scene.h>
#include <window.h>

#include "boxes.h"
#include "element.h"

namespace kui
{
    namespace {
        /** @brief The top of every Ui's render tree: the whole viewport, and a layer of its own. */
        class RootRender final : public RenderContainer {
        public:
            [[nodiscard]] bool IsRepaintBoundary() const override { return true; }
        protected:
            void PerformLayout() override
            {
                for (auto* child : Children()) {
                    child->Layout(Constraints());
                    child->SetOffset({});
                }
                SetSize(Constraints().Biggest());
            }
        };

        /** @brief The top of the element tree, holding the view's root widget over the root render object. */
        class RootElement final : public Element {
        public:
            explicit RootElement(RootRender& render) : _render(render) {}
            void SetRoot(const Widget& widget)
            {
                _child = UpdateChild(std::move(_child), widget);
                ChildRenderObjectChanged();
            }
            void Unmount() override
            {
                if (_child) { _child->Unmount(); _child.reset(); }
                Element::Unmount();
            }
            [[nodiscard]] RenderObject* RenderObjectOf() const override { return &_render; }
            void VisitChildren(const std::function<void(Element&)>& visit) override { if (_child) visit(*_child); }
            void ChildRenderObjectChanged() override
            {
                RenderObject* object = _child ? _child->RenderObjectOf() : nullptr;
                _render.SetChildren(object ? std::vector<RenderObject*> { object } : std::vector<RenderObject*> {});
            }
        private:
            RootRender& _render;
            std::unique_ptr<Element> _child;
        };

        constexpr std::array EditKeys {
            kor::Key::eBackspace, kor::Key::eDelete, kor::Key::eLeft, kor::Key::eRight, kor::Key::eUp, kor::Key::eDown,
            kor::Key::eHome, kor::Key::eEnd, kor::Key::eEnter, kor::Key::eKPEnter, kor::Key::eEsc, kor::Key::eTab,
        };
    }

    struct Ui::Impl {
        UiSettings settings;
        Widget root;
        bool rootChanged = true;
        Renderer renderer;
        Owner owner;
        BuildOwner builder;
        // The screen: the widgets, and over them what floats above everything (a drag's feedback).
        RootRender render, content, overlay;
        std::unique_ptr<RootElement> element, overlayElement;
        Statistics stats;

        // The pointer.
        glm::vec2 pointer {};
        bool hasPointer = false;
        kor::Input* pointerInput = nullptr;     // whose pointer it was last frame: the view's own window's, or a viewport's

        /** A drag in progress: what it carries, what shows under the pointer, and the receiver it is over. */
        struct Drag {
            DragData data;
            Widget feedback;
            glm::vec2 hotspot {};
            std::function<void(bool)> onEnd;
            RenderObject* over = nullptr;
            Impl* overUi = nullptr;         ///< The view the receiver is in: this one, or another the pointer is over.
            glm::vec2 overAt {};            ///< The pointer, in that view's coordinates.
        };
        std::optional<Drag> drag;
        std::vector<RenderObject*> hovered;     // deepest first
        std::vector<RenderObject*> captured;    // what the button went down on
        RenderObject* claimed = nullptr;        // the one of them that took the drag
        bool overSomething = false;

        static std::vector<Impl*>& Live() { static std::vector<Impl*> views; return views; }

        Impl()
        {
            Live().push_back(this);
            owner.onForget = [this](RenderObject& object) {
                std::erase(hovered, &object);
                std::erase(captured, &object);
                if (claimed == &object) claimed = nullptr;
                // A drag from any view may be over it.
                for (auto* view : Live()) if (view->drag && view->drag->over == &object) { view->drag->over = nullptr; view->drag->overUi = nullptr; }
            };
            owner.beginDrag = [this](RenderObject&, DragData data, const Widget& feedback, const glm::vec2 hotspot, std::function<void(bool)> onEnd) {
                if (drag) return false;
                drag = Drag { std::move(data), feedback, hotspot, std::move(onEnd) };
                ShowFeedback();
                return true;
            };
            render.SetChildren({ &content, &overlay });
            render.Attach(&owner);
            element = std::make_unique<RootElement>(content);
            element->Mount(nullptr, &builder);
            overlayElement = std::make_unique<RootElement>(overlay);
            overlayElement->Mount(nullptr, &builder);
            renderer.SetRoot(render.OwnLayer());
        }

        ~Impl()
        {
            std::erase(Live(), this);
            for (auto* view : Live()) if (view->drag && view->drag->overUi == this) { view->drag->over = nullptr; view->drag->overUi = nullptr; }
            overlayElement->Unmount();
            overlayElement.reset();
            element->Unmount();
            element.reset();
            owner.onForget = nullptr;
            owner.beginDrag = nullptr;
            render.SetChildren({});
        }

        void ShowFeedback()
        {
            overlayElement->SetRoot(drag ? Translate(pointer - drag->hotspot, IgnorePointer(drag->feedback)) : Widget {});
        }

        /** The drag, as the pointer moves over @p path and the button is let go. */
        /** Where the pointer of @p from's window is in this view, when it is over it. */
        std::optional<glm::vec2> PointerFrom(const kor::Window* from, const glm::vec2 pixels) const
        {
            if (!owner.window || !from) return std::nullopt;
            glm::vec2 mine = pixels;
            if (from != owner.window) {
                // Another window: by where the two are on the desktop, where the platform says.
                if (!kor::Window::CanBePositioned() || from->IsOffscreen() || owner.window->IsOffscreen()) return std::nullopt;
                mine = glm::vec2(from->Position()) + pixels - glm::vec2(owner.window->Position());
            }
            mine /= settings.scale;
            if (mine.x < 0.f || mine.y < 0.f || mine.x >= render.Size().x || mine.y >= render.Size().y) return std::nullopt;
            return mine;
        }

        /** The deepest receiver under @p position that takes the drag. */
        RenderObject* ReceiverAt(const glm::vec2 position, const DragData& data)
        {
            HitTestResult hit;
            render.HitTest(hit, position);
            for (const auto& entry : hit.path)
                if (auto* candidate = dynamic_cast<DropReceiver*>(entry.target); candidate && candidate->AcceptsDrag(data)) return entry.target;
            return nullptr;
        }

        void Dragging(kor::Input& input, const kor::Window* window, const std::vector<RenderObject*>& path, const glm::vec2 position, const bool moved)
        {
            RenderObject* over = nullptr;
            Impl* overUi = nullptr;
            glm::vec2 at = position;
            for (auto* object : path) {
                auto* candidate = dynamic_cast<DropReceiver*>(object);
                if (!candidate || !candidate->AcceptsDrag(drag->data)) continue;
                over = object;
                overUi = this;
                break;
            }
            // Nothing of this view's takes it: another view under the pointer may — one drawn over this
            // one in the same window, or one in another window.
            if (!over) {
                for (auto* view : Live()) {
                    if (view == this) continue;
                    const auto there = view->PointerFrom(window, input.MousePosition());
                    if (!there) continue;
                    if (RenderObject* found = view->ReceiverAt(*there, drag->data)) { over = found; overUi = view; at = *there; break; }
                }
            }
            auto* receiver = over ? dynamic_cast<DropReceiver*>(over) : nullptr;
            if (over != drag->over) {
                if (drag->over) dynamic_cast<DropReceiver*>(drag->over)->DragLeft();
                drag->over = over;
                drag->overUi = overUi;
                if (receiver) receiver->DragEntered(drag->data);
            }
            drag->overAt = at;
            if (receiver && moved) receiver->DragMoved(drag->data, LocalOf(*over, at));
            if (moved) ShowFeedback();

            const bool released = input.IsMouseButtonReleased(kor::MouseButton::eLeft);
            const bool cancelled = input.IsKeyPressed(kor::Key::eEsc) || (!released && input.MouseButtonState(kor::MouseButton::eLeft) == kor::KeyState::eNotPressed);
            if (!released && !cancelled) return;
            const bool accepted = released && receiver != nullptr;
            Drag done = std::move(*drag);
            drag.reset();
            ShowFeedback();
            if (accepted) receiver->Dropped(done.data, LocalOf(*over, at));
            if (done.over) if (auto* last = dynamic_cast<DropReceiver*>(done.over)) last->DragLeft();
            if (done.onEnd) done.onEnd(accepted);
        }

        /** Where the pointer's events come from this frame: a viewport's window when the pointer is in it. */
        const PointerViewport* ViewportFor(kor::Input& own)
        {
            // A gesture stays with the window it began in: the button's release is reported there.
            if (!captured.empty() || drag) {
                for (const auto& v : owner.viewports) if (v.input == pointerInput) return &v;
                if (pointerInput == &own || pointerInput == nullptr) return nullptr;
            }
            for (const auto& v : owner.viewports) if (v.input && v.window && v.window->IsHovered()) return &v;
            return nullptr;
        }

        static glm::vec2 LocalOf(const RenderObject& object, const glm::vec2 position) { return position - object.ToGlobal({ 0.f, 0.f }); }

        static bool Send(RenderObject& target, PointerEvent event)
        {
            event.local = LocalOf(target, event.position);
            return target.HandleEvent(event);
        }

        void Pointer(kor::Input& own)
        {
            // Positions are in the root's coordinates whichever window they came from, so every render
            // object finds its own from them the same way.
            const PointerViewport* viewport = ViewportFor(own);
            kor::Input& input = viewport ? *viewport->input : own;
            HitTestResult hit;
            glm::vec2 position;
            if (viewport) {
                const glm::vec2 local = viewport->origin + input.MousePosition() / settings.scale;
                viewport->root->HitTest(hit, local);
                position = viewport->root->ToGlobal(local);
            } else {
                position = input.MousePosition() / settings.scale;
                render.HitTest(hit, position);
            }
            const bool sameSource = pointerInput == &input;
            const bool moved = !hasPointer || !sameSource || position != pointer;
            const glm::vec2 delta = hasPointer && sameSource ? position - pointer : glm::vec2(0.f);
            pointer = position;
            hasPointer = true;
            pointerInput = &input;

            // What is under the pointer now — the roots' own entries are not "something".
            std::vector<RenderObject*> path;
            for (const auto& entry : hit.path)
                if (entry.target != &render && entry.target != &content && entry.target != &overlay) path.push_back(entry.target);
            overSomething = !path.empty();

            // Leaving and entering, as the hover set changes.
            for (auto* old : std::vector(hovered))
                if (std::ranges::find(path, old) == path.end()) Send(*old, { .type = PointerEvent::Type::eExit, .position = position });
            for (auto* now : path)
                if (std::ranges::find(hovered, now) == hovered.end()) Send(*now, { .type = PointerEvent::Type::eEnter, .position = position });
            hovered = path;
            if (moved) for (auto* now : std::vector(path)) Send(*now, { .type = PointerEvent::Type::eHover, .position = position });

            // A press: everything under the pointer hears of it, and is what the rest of the gesture goes to.
            for (const auto button : { kor::MouseButton::eLeft, kor::MouseButton::eRight, kor::MouseButton::eMiddle }) {
                if (!input.IsMouseButtonPressed(button)) continue;
                if (button == kor::MouseButton::eLeft) {
                    // Clicking elsewhere takes the keyboard away from whatever had it.
                    if (auto* focused = owner.Focused(); focused && std::ranges::find(path, focused) == path.end()) owner.RequestFocus(nullptr);
                    captured = path;
                    claimed = nullptr;
                }
                for (auto* target : std::vector(path))
                    Send(*target, { .type = PointerEvent::Type::eDown, .position = position, .button = button });
            }

            // A drag: the first of them to want it takes it, and the rest are told the gesture is off.
            if (moved && !captured.empty() && input.MouseButtonState(kor::MouseButton::eLeft) != kor::KeyState::eNotPressed) {
                if (claimed) {
                    Send(*claimed, { .type = PointerEvent::Type::eMove, .position = position, .delta = delta });
                } else {
                    for (auto* target : std::vector(captured)) {
                        if (!Send(*target, { .type = PointerEvent::Type::eMove, .position = position, .delta = delta })) continue;
                        claimed = target;
                        for (auto* other : std::vector(captured))
                            if (other != target) Send(*other, { .type = PointerEvent::Type::eCancel, .position = position });
                        captured = { target };
                        break;
                    }
                }
            }

            if (input.IsMouseButtonReleased(kor::MouseButton::eLeft)) {
                for (auto* target : std::vector(captured)) Send(*target, { .type = PointerEvent::Type::eUp, .position = position });
                captured.clear();
                claimed = nullptr;
            }

            // The wheel: the deepest thing under the pointer that uses it.
            if (const glm::vec2 wheel = input.MouseScrollDelta(); wheel != glm::vec2(0.f)) {
                for (auto* target : std::vector(path))
                    if (Send(*target, { .type = PointerEvent::Type::eScroll, .position = position, .delta = wheel })) break;
            }

            if (drag) Dragging(input, viewport ? viewport->window : owner.window, path, position, moved);
        }

        /** The keyboard of whichever window has it: a viewport's when that is focused. */
        kor::Input& KeyboardOf(kor::Input& own)
        {
            for (const auto& v : owner.viewports) if (v.input && v.window && v.window->IsFocused()) return *v.input;
            return own;
        }

        void Keyboard(kor::Input& input, const float dt)
        {
            RenderObject* focused = owner.Focused();
            if (!focused) return;
            if (const auto text = input.TypedText(); !text.empty()) focused->HandleText(text);
            for (const kor::Key key : EditKeys) {
                const bool pressed = input.IsKeyPressed(key), repeated = input.IsKeyRepeated(key);
                if ((pressed || repeated) && owner.Focused()) owner.Focused()->HandleKey(key, repeated && !pressed);
            }
            if (owner.Focused()) owner.Focused()->FocusTick(dt);
        }

        void Frame(kor::Input* input, const glm::vec2 viewport, const float dt)
        {
            const ThemeScope theme(settings.theme);
            builder.builds = 0;
            owner.ResetCounters();

            using Clock = std::chrono::steady_clock;
            const auto ms = [](const Clock::time_point from, const Clock::time_point to) { return std::chrono::duration<double, std::milli>(to - from).count(); };
            const auto t0 = Clock::now();
            owner.scale = settings.scale;
            if (input) {
                Pointer(*input);
                Keyboard(KeyboardOf(*input), dt);
            }
            const auto t1 = Clock::now();
            if (rootChanged) {
                element->SetRoot(root);
                rootChanged = false;
            }
            builder.Tick(dt);
            builder.Flush();
            const auto t2 = Clock::now();

            const glm::vec2 logical = viewport / settings.scale;
            render.Layout(BoxConstraints::Tight(logical), false);
            owner.FlushLayout();
            const auto t3 = Clock::now();
            owner.FlushPaint();
            render.RepaintIfNeeded();
            renderer.SetScale(settings.scale);
            const auto t4 = Clock::now();
            stats.inputMs = ms(t0, t1);
            stats.buildMs = ms(t1, t2);
            stats.layoutMs = ms(t2, t3);
            stats.paintMs = ms(t3, t4);

            stats.builds = builder.builds;
            stats.layouts = owner.layouts;
            stats.paints = owner.paints;
            if (input) {
                const bool pointing = overSomething || !captured.empty() || drag.has_value();
                const bool typing = owner.Focused() != nullptr;
                input->ClaimInterface(this, pointing && (pointerInput == input || pointerInput == nullptr), typing);
                for (const auto& v : owner.viewports) if (v.input) v.input->ClaimInterface(this, pointing && pointerInput == v.input, typing);
            }
        }
    };

    Ui::Ui(Widget root, UiSettings settings) : _impl(std::make_unique<Impl>())
    {
        _impl->settings = std::move(settings);
        _impl->root = std::move(root);
    }

    Ui::~Ui() = default;

    void Ui::SetRoot(Widget root)
    {
        _impl->root = std::move(root);
        _impl->rootChanged = true;
    }

    void Ui::SetTheme(const Theme& theme)
    {
        _impl->settings.theme = theme;
        Reassemble();   // everything built read the old one
    }

    void Ui::Reassemble()
    {
        _impl->element->MarkTreeDirty();
        _impl->overlayElement->MarkTreeDirty();
    }

    void Ui::ReassembleAll()
    {
        for (auto* view : Impl::Live()) { view->element->MarkTreeDirty(); view->overlayElement->MarkTreeDirty(); }
    }

    const Theme& Ui::GetTheme() const { return _impl->settings.theme; }

    void Ui::SetScale(const float scale)
    {
        _impl->settings.scale = std::max(scale, 0.1f);
    }

    void Ui::Update()
    {
        auto& input = kor::Scene::Input::Get();
        const glm::uvec2 extent = kor::Scene::Window::Extent();
        _impl->owner.window = &kor::Scene::Window::Get();
        _impl->Frame(&input, glm::vec2(extent), kor::Scene::Time::FrameTime());
    }

    void Ui::Update(kor::Input& input, const glm::vec2 viewport, const float dt) { _impl->Frame(&input, viewport, dt); }

    Renderer& Ui::GetRenderer() { return _impl->renderer; }
    RenderObject* Ui::RootRenderObject() const { return &_impl->render; }
    bool Ui::WantsPointer() const { return _impl->overSomething || !_impl->captured.empty() || _impl->drag.has_value(); }
    bool Ui::WantsKeyboard() const { return _impl->owner.Focused() != nullptr; }
    const Ui::Statistics& Ui::Stats() const { return _impl->stats; }

}
