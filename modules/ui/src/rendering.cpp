//
// koral-ui: the render tree's bookkeeping — constraints, relayout and repaint boundaries, hit testing.
//

#include <algorithm>
#include <cmath>
#include <unordered_set>

#include <kui/rendering.h>
#include <kui/widgets.h>

#include "element.h"

namespace kui
{
    // ---- constraints ----------------------------------------------------------------------------------

    glm::vec2 BoxConstraints::Constrain(const glm::vec2 size) const
    {
        return { std::clamp(size.x, minWidth, std::max(minWidth, maxWidth)), std::clamp(size.y, minHeight, std::max(minHeight, maxHeight)) };
    }

    BoxConstraints BoxConstraints::Deflate(const EdgeInsets& insets) const
    {
        const float h = insets.Horizontal(), v = insets.Vertical();
        const float minW = std::max(0.f, minWidth - h), minH = std::max(0.f, minHeight - v);
        return { minW, std::max(minW, maxWidth - h), minH, std::max(minH, maxHeight - v) };
    }

    BoxConstraints BoxConstraints::Enforce(const BoxConstraints& o) const
    {
        const auto clamp = [](const float v, const float lo, const float hi) { return std::clamp(v, lo, std::max(lo, hi)); };
        return { clamp(minWidth, o.minWidth, o.maxWidth), clamp(maxWidth, o.minWidth, o.maxWidth),
                 clamp(minHeight, o.minHeight, o.maxHeight), clamp(maxHeight, o.minHeight, o.maxHeight) };
    }

    BoxConstraints BoxConstraints::Tighten(const float width, const float height) const
    {
        BoxConstraints c = *this;
        if (width >= 0.f) c.minWidth = c.maxWidth = std::clamp(width, minWidth, std::max(minWidth, maxWidth));
        if (height >= 0.f) c.minHeight = c.maxHeight = std::clamp(height, minHeight, std::max(minHeight, maxHeight));
        return c;
    }

    // ---- render objects ------------------------------------------------------------------------------

    RenderObject::RenderObject() = default;

    RenderObject::~RenderObject()
    {
        if (_owner) _owner->Forget(*this);
        if (_parent) _parent->ChildDestroyed(*this);
        VisitChildren([](RenderObject& child) { child._parent = nullptr; });
    }

    void RenderObject::AdoptChild(RenderObject& child)
    {
        child._parent = this;
        child._relayoutBoundary = nullptr;
        if (_owner) child.Attach(_owner);
        MarkNeedsLayout();
    }

    void RenderObject::DropChild(RenderObject& child)
    {
        if (child._parent != this) return;
        child._parent = nullptr;
        child.Detach();
        MarkNeedsLayout();
    }

    void RenderObject::Attach(Owner* owner)
    {
        _owner = owner;
        VisitChildren([owner](RenderObject& child) { child.Attach(owner); });
        // Work it was waiting for, now that someone will do it.
        if (_needsLayout && _relayoutBoundary == this && _owner) _owner->ScheduleLayout(*this);
        if (_needsPaint && IsRepaintBoundary() && _owner) _owner->SchedulePaint(*this);
    }

    void RenderObject::Detach()
    {
        if (_owner) _owner->Forget(*this);
        _owner = nullptr;
        VisitChildren([](RenderObject& child) { child.Detach(); });
    }

    int RenderObject::Depth() const
    {
        int depth = 0;
        for (auto* p = _parent; p; p = p->_parent) ++depth;
        return depth;
    }

    void RenderObject::SetSize(const glm::vec2 size)
    {
        const glm::vec2 constrained = _constraints.Constrain(size);
        _size = { std::isfinite(constrained.x) ? constrained.x : 0.f, std::isfinite(constrained.y) ? constrained.y : 0.f };
    }

    void RenderObject::SetOffset(const glm::vec2 offset)
    {
        if (offset == _offset) return;
        _offset = offset;
        // It is painted where its parent put it: the parent's picture is what changes.
        if (_parent) _parent->MarkNeedsPaint();
    }

    void RenderObject::Layout(const BoxConstraints& constraints, const bool parentUsesSize)
    {
        // A relayout boundary: nothing that happens inside can change what its parent decided.
        RenderObject* boundary = (!parentUsesSize || SizedByParent() || constraints.IsTight() || !_parent)
            ? this : _parent->_relayoutBoundary;
        if (!_needsLayout && _hasLaidOut && constraints == _constraints && boundary == _relayoutBoundary) return;
        _constraints = constraints;
        _relayoutBoundary = boundary;
        PerformLayout();
        _needsLayout = false;
        _hasLaidOut = true;
        if (_owner) ++_owner->layouts;
        MarkNeedsPaint();
    }

    void RenderObject::MarkNeedsLayout()
    {
        if (_needsLayout) return;
        _needsLayout = true;
        if (_relayoutBoundary != this && _parent) {
            _parent->MarkNeedsLayout();
        } else if (_owner) {
            _owner->ScheduleLayout(*this);
        }
    }

    void RenderObject::MarkNeedsPaint()
    {
        if (_needsPaint) return;
        _needsPaint = true;
        if (IsRepaintBoundary()) {
            if (_owner) _owner->SchedulePaint(*this);
        } else if (_parent) {
            _parent->MarkNeedsPaint();
        }
    }

    const std::shared_ptr<Layer>& RenderObject::OwnLayer()
    {
        if (!_layer) _layer = Layer::Create();
        return _layer;
    }

    namespace {
        bool paintBounds = false;
        unsigned debugRevision = 0;

        /** The outline debug::SetPaintBounds asks for, of @p object laid out at @p at. */
        void OutlineBounds(RenderObject& object, Canvas& canvas, const glm::vec2 at)
        {
            const glm::vec2 size = object.Size();
            if (size.x <= 0.f || size.y <= 0.f) return;
            bool holds = false;
            object.VisitChildren([&holds](RenderObject&) { holds = true; });
            const Color color = object.IsRepaintBoundary() ? Color::Hex(0x22d3ee).WithAlpha(0.9f)
                              : holds ? Color::Hex(0xff3ea5).WithAlpha(0.85f)
                              : Color::Hex(0xfacc15).WithAlpha(0.45f);
            // Half a unit in, so the line is inside what it outlines and two neighbours' lines do not share one.
            canvas.DrawRect(Rect::XYWH(at.x + 0.5f, at.y + 0.5f, std::max(size.x - 1.f, 0.f), std::max(size.y - 1.f, 0.f)),
                            Paint::Stroked(color, 1.f));
        }
    }

    void debug::SetPaintBounds(const bool enabled)
    {
        if (enabled == paintBounds) return;
        paintBounds = enabled;
        ++debugRevision;
    }
    bool debug::PaintBounds() { return paintBounds; }
    unsigned debug::Revision() { return debugRevision; }

    void RenderObject::RepaintIfNeeded()
    {
        if (!_needsPaint && _layer && _layer->GetPicture()) return;
        Canvas canvas;
        // About as much as last time, as a rule: room for it at once.
        if (_layer && _layer->GetPicture()) canvas.Reserve(_layer->GetPicture()->InstanceCount() + 16);
        if (_cull) canvas.SetCullRect(*_cull);
        Paint(canvas, { 0.f, 0.f });
        if (paintBounds) OutlineBounds(*this, canvas, { 0.f, 0.f });
        OwnLayer()->SetPicture(canvas.Finish());
        _needsPaint = false;
        if (_owner) ++_owner->paints;
    }

    void RenderObject::PaintChild(RenderObject& child, Canvas& canvas, const glm::vec2 offset)
    {
        PaintChildAt(child, canvas, offset + ChildOrigin(child));
    }

    void RenderObject::SetPaintCull(const std::optional<Rect>& cull)
    {
        if (cull == _cull) return;
        _cull = cull;
        MarkNeedsPaint();
    }

    void RenderObject::PaintChildAt(RenderObject& child, Canvas& canvas, const glm::vec2 at)
    {
        // Wholly outside what will be looked at — and by enough that a shadow or something hung off it
        // would be too: left out. It keeps whatever it was owed; it is painted when it comes into view.
        constexpr float Overhang = 48.f;
        if (canvas.QuickReject(Rect::XYWH(at.x, at.y, child.Size().x, child.Size().y).Inflate(Overhang))) return;
        if (child.IsRepaintBoundary()) {
            child.RepaintIfNeeded();
            canvas.Save();
            canvas.Translate(at);
            canvas.DrawLayer(child.OwnLayer());
            canvas.Restore();
        } else {
            child._needsPaint = false;
            child.Paint(canvas, at);
            if (paintBounds) OutlineBounds(child, canvas, at);
        }
    }

    bool RenderObject::HitTest(HitTestResult& result, const glm::vec2 position)
    {
        if (position.x < 0.f || position.y < 0.f || position.x >= _size.x || position.y >= _size.y) return false;
        const bool hit = HitTestChildren(result, position) || HitTestSelf(position);
        if (hit) result.Add(this, position);
        return hit;
    }

    bool RenderObject::HitTestChildren(HitTestResult& result, const glm::vec2 position)
    {
        std::vector<RenderObject*> children;
        VisitChildren([&](RenderObject& child) { children.push_back(&child); });
        for (auto it = children.rbegin(); it != children.rend(); ++it)
            if ((*it)->HitTest(result, position - ChildOrigin(**it))) return true;
        return false;
    }

    glm::vec2 RenderObject::ToGlobal(const glm::vec2 local) const
    {
        glm::vec2 p = local;
        for (const RenderObject* node = this; node->_parent; node = node->_parent) p += node->_parent->ChildOrigin(*node);
        return p;
    }

    glm::vec2 RenderObject::ToLocal(const glm::vec2 global) const
    {
        return _parent ? _parent->MapToChild(*this, _parent->ToLocal(global)) : global;
    }

    const Theme* RenderObject::InheritedTheme() const
    {
        if (detail::ThemedCount() == 0) return nullptr;    // nothing sets one anywhere: the usual case, and free
        for (const RenderObject* node = this; node; node = node->_parent)
            if (const Theme* theme = node->ProvidedTheme()) return theme;
        return nullptr;
    }

    // ---- the owner -------------------------------------------------------------------------------------

    void Owner::ScheduleLayout(RenderObject& boundary)
    {
        if (std::ranges::find(_layout, &boundary) == _layout.end()) _layout.push_back(&boundary);
    }

    void Owner::SchedulePaint(RenderObject& boundary)
    {
        if (std::ranges::find(_paint, &boundary) == _paint.end()) _paint.push_back(&boundary);
    }

    void Owner::Forget(RenderObject& object)
    {
        std::erase(_layout, &object);
        std::erase(_paint, &object);
        if (_focused == &object) _focused = nullptr;
        if (onForget) onForget(object);
    }

    void Owner::FlushLayout()
    {
        // Shallowest first: laying out a parent may lay out (or drop) the children waiting below it.
        while (!_layout.empty()) {
            auto pending = std::move(_layout);
            _layout.clear();
            std::ranges::sort(pending, [](const RenderObject* a, const RenderObject* b) { return a->Depth() < b->Depth(); });
            for (RenderObject* object : pending) {
                if (!object->_needsLayout || object->_owner != this) continue;
                if (!object->_hasLaidOut) continue;   // its parent lays it out the first time
                // With the theme set over it, where one is: it is laid out from here, not from under what set it.
                const Theme* theme = object->InheritedTheme();
                const std::optional<ThemeScope> scope = theme ? std::optional<ThemeScope>(std::in_place, *theme) : std::nullopt;
                object->PerformLayout();
                object->_needsLayout = false;
                ++layouts;
                object->MarkNeedsPaint();
            }
        }
    }

    void Owner::FlushPaint()
    {
        auto pending = std::move(_paint);
        _paint.clear();
        // Deepest first, so a parent repainting after its child shows the child's new picture either way.
        std::ranges::sort(pending, [](const RenderObject* a, const RenderObject* b) { return a->Depth() > b->Depth(); });
        for (RenderObject* object : pending) {
            if (object->_owner != this || !object->_needsPaint) continue;
            const Theme* theme = object->InheritedTheme();
            const std::optional<ThemeScope> scope = theme ? std::optional<ThemeScope>(std::in_place, *theme) : std::nullopt;
            object->RepaintIfNeeded();
        }
    }

    void Owner::RequestFocus(RenderObject* object)
    {
        if (_focused == object) return;
        RenderObject* previous = _focused;
        _focused = object;
        if (previous) previous->FocusChanged(false);
        if (object) object->FocusChanged(true);
    }

    // ---- containers -----------------------------------------------------------------------------------

    void RenderContainer::VisitChildren(const std::function<void(RenderObject&)>& visit)
    {
        for (auto* child : _children) visit(*child);
    }

    void RenderContainer::SetChildren(const std::vector<RenderObject*>& children)
    {
        if (children == _children) return;
        const std::unordered_set<RenderObject*> next(children.begin(), children.end());
        const std::unordered_set<RenderObject*> previous(_children.begin(), _children.end());
        for (auto* old : _children)
            if (!next.contains(old)) DropChild(*old);
        _children = children;
        for (auto* child : _children)
            if (!previous.contains(child)) AdoptChild(*child);
        MarkNeedsLayout();
        MarkNeedsPaint();
    }

    void RenderContainer::ChildDestroyed(RenderObject& child)
    {
        std::erase(_children, &child);
        MarkNeedsLayout();
    }

    void RenderContainer::PerformLayout()
    {
        if (auto* child = Child()) {
            child->Layout(Constraints());
            child->SetOffset({});
            SetSize(child->Size());
        } else {
            SetSize(Constraints().Smallest());
        }
    }

    float RenderContainer::MinIntrinsicWidth() const
    {
        float width = 0.f;
        for (const RenderObject* child : _children) width = std::max(width, child->MinIntrinsicWidth());
        return width;
    }

    void RenderContainer::Paint(Canvas& canvas, const glm::vec2 offset)
    {
        for (auto* child : _children) PaintChild(*child, canvas, offset);
    }
}
