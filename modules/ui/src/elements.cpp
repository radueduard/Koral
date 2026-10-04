//
// koral-ui: building, and rebuilding as little as possible.
//

#include <algorithm>
#include <map>
#include <typeinfo>

#include <log.h>

#include "element.h"

namespace kui
{
    // ---- widgets ----------------------------------------------------------------------------------------

    WidgetBase::~WidgetBase() = default;

    Widget Widget::Key(std::string key) &&
    {
        // A widget is only given away once built; until then the handle is its only owner.
        if (_widget) const_cast<WidgetBase*>(_widget.get())->_key = std::move(key);
        return std::move(*this);
    }

    std::unique_ptr<Element> StatelessWidget::CreateElement() const { return std::make_unique<StatelessElement>(); }
    std::unique_ptr<Element> StatefulWidget::CreateElement() const { return std::make_unique<StatefulElement>(); }
    std::unique_ptr<Element> RenderObjectWidget::CreateElement() const { return std::make_unique<RenderObjectElement>(); }

    const std::vector<Widget>& RenderObjectWidget::Children() const
    {
        static const std::vector<Widget> none;
        return none;
    }

    void StatefulWidget::SetState(const std::function<void()>& change)
    {
        if (change) change();
        if (_element) _element->MarkDirty();
    }

    void StatefulWidget::Animate(std::function<bool(float)> tick)
    {
        if (_element && _element->GetOwner()) _element->GetOwner()->AddTicker(*_element, std::move(tick));
    }

    // ---- theme ------------------------------------------------------------------------------------------

    namespace {
        thread_local const Theme* currentTheme = nullptr;
    }

    int& detail::ThemedCount() { static int count = 0; return count; }

    namespace {
        struct DeferredWidget final : StatelessWidget {
            std::function<Widget()> build;
            explicit DeferredWidget(std::function<Widget()> b) : build(std::move(b)) {}
            [[nodiscard]] Widget Build() const override { return build(); }
        };
    }

    Widget detail::Deferred(std::function<Widget()> build) { return Make<DeferredWidget>(std::move(build)); }

    ThemeScope::ThemeScope(const Theme& theme) : previous(currentTheme) { currentTheme = &theme; }
    ThemeScope::~ThemeScope() { currentTheme = previous; }

    const Theme& Theme::Current()
    {
        static const Theme fallback = Dark();
        return currentTheme ? *currentTheme : fallback;
    }

    Theme Theme::Dark() { return {}; }

    Theme Theme::Light()
    {
        Theme t;
        // One UI's light ground: soft grey behind near-white cards, the same coral.
        t.background = Color::Hex(0xF2F2F2);
        t.surface = Color::Hex(0xFCFCFC);
        t.surfaceHover = Color::Hex(0xEFEFEF);
        t.surfacePressed = Color::Hex(0xE2E2E2);
        t.primary = Color::Hex(0xFF7F50);
        t.primaryHover = Color::Hex(0xFF946B);
        t.primaryPressed = Color::Hex(0xE86A3C);
        t.text = Color::Hex(0x252525);
        t.textMuted = Color::Hex(0x7A7A7A);
        t.border = Color::Hex(0xE0E0E0);
        t.focus = Color::Hex(0xE86A3C);
        return t;
    }

    // ---- elements ---------------------------------------------------------------------------------------

    Element::~Element() = default;

    bool Element::CanUpdate(const Widget& a, const Widget& b)
    {
        if (!a || !b) return false;
        return typeid(*a.Get()) == typeid(*b.Get()) && a.Get()->TypeTag() == b.Get()->TypeTag()
            && a.Get()->WidgetKey() == b.Get()->WidgetKey();
    }

    void Element::Mount(Element* parent, BuildOwner* owner)
    {
        _parent = parent;
        _owner = owner;
        _depth = parent ? parent->_depth + 1 : 0;
        _mounted = true;
    }

    void Element::Update(const Widget& widget) { _widget = widget; }

    void Element::Unmount()
    {
        _mounted = false;
        if (_owner) _owner->Forget(*this);
    }

    void Element::MarkDirty()
    {
        if (_dirty || !_mounted) return;
        _dirty = true;
        if (_owner) _owner->Schedule(*this);
    }

    void Element::MarkTreeDirty()
    {
        if (dynamic_cast<ComponentElement*>(this)) MarkDirty();
        VisitChildren([](Element& child) { child.MarkTreeDirty(); });
    }

    void Element::ChildRenderObjectChanged()
    {
        if (_parent) _parent->ChildRenderObjectChanged();
    }

    std::unique_ptr<Element> Element::Inflate(const Widget& widget)
    {
        auto element = widget.Get()->CreateElement();
        element->_widget = widget;
        element->Mount(this, _owner);
        return element;
    }

    std::unique_ptr<Element> Element::UpdateChild(std::unique_ptr<Element> child, const Widget& widget)
    {
        if (!widget) {
            if (child) child->Unmount();
            return nullptr;
        }
        if (child) {
            // The very same widget: nothing below can have changed because of it.
            if (child->_widget.Get() == widget.Get()) return child;
            if (CanUpdate(child->_widget, widget)) {
                child->Update(widget);
                return child;
            }
            child->Unmount();
            child.reset();
        }
        return Inflate(widget);
    }

    std::vector<std::unique_ptr<Element>> Element::UpdateChildren(std::vector<std::unique_ptr<Element>> old, const std::vector<Widget>& widgets)
    {
        // The common case by far: as many as before, each the kind that was in its place — and under the
        // key that was there, where they are keyed: every child is updated where it is, and the list is
        // the one there was.
        if (old.size() == widgets.size()) {
            bool aligned = true;
            for (std::size_t i = 0; i < old.size() && aligned; ++i) {
                const Widget& widget = widgets[i];
                aligned = widget && old[i] && (old[i]->_widget.Get() == widget.Get() || CanUpdate(old[i]->_widget, widget));
            }
            if (aligned) {
                for (std::size_t i = 0; i < old.size(); ++i)
                    if (old[i]->_widget.Get() != widgets[i].Get()) old[i]->Update(widgets[i]);
                return old;
            }
        }

        std::vector<std::unique_ptr<Element>> result;
        result.reserve(widgets.size());

        // Keyed children are found by key wherever they were; the rest are matched in order.
        std::map<std::string, std::size_t> keyed;
        for (std::size_t i = 0; i < old.size(); ++i)
            if (old[i] && !old[i]->_widget.Get()->WidgetKey().empty()) keyed.emplace(old[i]->_widget.Get()->WidgetKey(), i);
        std::size_t cursor = 0;

        for (const auto& widget : widgets) {
            if (!widget) continue;
            std::unique_ptr<Element> match;
            const auto& key = widget.Get()->WidgetKey();
            if (!key.empty()) {
                if (const auto it = keyed.find(key); it != keyed.end() && old[it->second] && CanUpdate(old[it->second]->_widget, widget))
                    match = std::move(old[it->second]);
            } else {
                for (std::size_t i = cursor; i < old.size(); ++i) {
                    if (!old[i] || !old[i]->_widget.Get()->WidgetKey().empty()) continue;
                    if (CanUpdate(old[i]->_widget, widget)) { match = std::move(old[i]); cursor = i + 1; }
                    break;   // in order: an unkeyed child is only matched by the one in its place
                }
            }
            result.push_back(UpdateChild(std::move(match), widget));
        }
        for (auto& left : old)
            if (left) left->Unmount();
        return result;
    }

    // ---- components ---------------------------------------------------------------------------------

    void ComponentElement::Mount(Element* parent, BuildOwner* owner)
    {
        Element::Mount(parent, owner);
        Rebuild();
    }

    void ComponentElement::Update(const Widget& widget)
    {
        Element::Update(widget);
        _dirty = true;
        Rebuild();
    }

    void ComponentElement::Unmount()
    {
        if (_child) { _child->Unmount(); _child.reset(); }
        Element::Unmount();
    }

    void ComponentElement::Rebuild()
    {
        if (!_mounted) return;
        RenderObject* before = RenderObjectOf();
        // Built with the theme set nearest over it, where any is: the element above that has a render object
        // is where to look from, since this one's own is not made yet the first time.
        const Theme* theme = nullptr;
        if (detail::ThemedCount() > 0)
            for (const Element* up = _parent; up && !theme; up = up->Parent())
                if (const RenderObject* object = dynamic_cast<const RenderObjectElement*>(up) ? up->RenderObjectOf() : nullptr) theme = object->ProvidedTheme();
        const std::optional<ThemeScope> scope = theme ? std::optional<ThemeScope>(std::in_place, *theme) : std::nullopt;
        const Widget built = Build();
        _dirty = false;
        if (_owner) ++_owner->builds;
        _child = UpdateChild(std::move(_child), built);
        if (RenderObjectOf() != before) ChildRenderObjectChanged();
    }

    Widget StatelessElement::Build() { return static_cast<const StatelessWidget*>(_widget.Get())->Build(); }

    void StatefulElement::Mount(Element* parent, BuildOwner* owner)
    {
        // The widget first placed here is the one that holds the state from now on.
        _state = std::const_pointer_cast<StatefulWidget>(std::static_pointer_cast<const StatefulWidget>(_widget.Shared()));
        _state->_element = this;
        Element::Mount(parent, owner);
        _state->InitState();
        Rebuild();
    }

    void StatefulElement::Update(const Widget& widget)
    {
        const auto& newer = *static_cast<const StatefulWidget*>(widget.Get());
        Element::Update(widget);
        if (&newer != _state.get()) _state->DidUpdateWidget(newer);
        _dirty = true;
        Rebuild();
    }

    void StatefulElement::Unmount()
    {
        ComponentElement::Unmount();
        _state->Dispose();
        _state->_element = nullptr;
    }

    Widget StatefulElement::Build() { return _state->Build(); }

    // ---- render elements ----------------------------------------------------------------------------

    void RenderObjectElement::Mount(Element* parent, BuildOwner* owner)
    {
        Element::Mount(parent, owner);
        const auto& widget = *static_cast<const RenderObjectWidget*>(_widget.Get());
        _object = widget.CreateRenderObject();
        widget.UpdateRenderObject(*_object);
        _takingChildren = true;
        for (const auto& child : widget.Children())
            if (child) _children.push_back(Inflate(child));
        _takingChildren = false;
        TakeChildren();
    }

    void RenderObjectElement::Update(const Widget& widget)
    {
        Element::Update(widget);
        const auto& w = *static_cast<const RenderObjectWidget*>(_widget.Get());
        w.UpdateRenderObject(*_object);
        _takingChildren = true;
        _children = UpdateChildren(std::move(_children), w.Children());
        _takingChildren = false;
        TakeChildren();
    }

    void RenderObjectElement::Unmount()
    {
        for (auto& child : _children) child->Unmount();
        _children.clear();
        Element::Unmount();
        // The render object goes with the element; its parent forgets it as it does.
        _object.reset();
        _container = nullptr;
        _containerKnown = false;
    }

    void RenderObjectElement::ChildRenderObjectChanged()
    {
        if (!_takingChildren) TakeChildren();
    }

    void RenderObjectElement::TakeChildren()
    {
        if (!_containerKnown) {
            _container = dynamic_cast<RenderContainer*>(_object.get());
            _containerKnown = true;
        }
        if (!_container) return;
        // Nearly always the ones it has already: looked at before a list of them is made.
        const auto& have = _container->Children();
        std::size_t at = 0;
        bool same = true;
        for (const auto& child : _children) {
            auto* object = child->RenderObjectOf();
            if (!object) continue;
            if (at >= have.size() || have[at] != object) { same = false; break; }
            ++at;
        }
        if (same && at == have.size()) return;
        std::vector<RenderObject*> objects;
        objects.reserve(_children.size());
        for (const auto& child : _children)
            if (auto* object = child->RenderObjectOf()) objects.push_back(object);
        _container->SetChildren(objects);
    }

    // ---- the owner --------------------------------------------------------------------------------------

    void BuildOwner::Schedule(Element& element)
    {
        if (std::ranges::find(_dirty, &element) == _dirty.end()) _dirty.push_back(&element);
    }

    void BuildOwner::Forget(Element& element)
    {
        std::erase(_dirty, &element);
        std::ranges::replace(_pending, &element, static_cast<Element*>(nullptr));
        std::erase_if(_tickers, [&](const Ticker& t) { return t.element == &element; });
    }

    void BuildOwner::Flush()
    {
        while (!_dirty.empty()) {
            _pending = std::move(_dirty);
            _dirty.clear();
            std::ranges::sort(_pending, [](const Element* a, const Element* b) { return a->Depth() < b->Depth(); });
            // By index, and checked each time: a parent's rebuild may unmount (and destroy) an element
            // further down the list, which Forget nulls out.
            for (std::size_t i = 0; i < _pending.size(); ++i) {
                Element* element = _pending[i];
                if (element && element->Mounted() && element->Dirty()) element->Rebuild();
            }
            _pending.clear();
        }
    }

    void BuildOwner::AddTicker(Element& element, std::function<bool(float)> tick)
    {
        _tickers.push_back({ &element, std::move(tick) });
    }

    void BuildOwner::Tick(const float dt)
    {
        auto tickers = std::move(_tickers);
        _tickers.clear();
        for (auto& t : tickers) {
            if (!t.element->Mounted()) continue;
            if (t.tick(dt)) _tickers.push_back(std::move(t));
        }
    }
}
