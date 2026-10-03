//
// koral-ui: the render tree's bookkeeping — constraints, relayout and repaint boundaries, hit testing.
//

#include <algorithm>
#include <cmath>
#include <unordered_set>

#include <kui/rendering.h>
#include <kui/widgets.h>

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

    void RenderObject::RepaintIfNeeded()
    {
        if (!_needsPaint && _layer && _layer->GetPicture()) return;
        Canvas canvas;
        Paint(canvas, { 0.f, 0.f });
        OwnLayer()->SetPicture(canvas.Finish());
        _needsPaint = false;
        if (_owner) ++_owner->paints;
    }

    void RenderObject::PaintChild(RenderObject& child, Canvas& canvas, const glm::vec2 offset)
    {
        PaintChildAt(child, canvas, offset + ChildOrigin(child));
    }

    void RenderObject::PaintChildAt(RenderObject& child, Canvas& canvas, const glm::vec2 at)
    {
        if (child.IsRepaintBoundary()) {
            child.RepaintIfNeeded();
            canvas.Save();
            canvas.Translate(at);
            canvas.DrawLayer(child.OwnLayer());
            canvas.Restore();
        } else {
            child._needsPaint = false;
            child.Paint(canvas, at);
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
        for (RenderObject* object : pending)
            if (object->_owner == this && object->_needsPaint) object->RepaintIfNeeded();
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

    void RenderContainer::Paint(Canvas& canvas, const glm::vec2 offset)
    {
        for (auto* child : _children) PaintChild(*child, canvas, offset);
    }
}
