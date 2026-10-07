//
// koral-ui: the built-in render objects, and the widgets that make them.
//

#include <algorithm>
#include <cmath>

#include "boxes.h"

namespace kui
{
    // ---- padding, alignment, constraints --------------------------------------------------------------

    void RenderPadding::Set(const EdgeInsets& padding)
    {
        if (padding == _padding) return;
        _padding = padding;
        MarkNeedsLayout();
    }

    void RenderPadding::PerformLayout()
    {
        if (auto* child = Child()) {
            child->Layout(Constraints().Deflate(_padding));
            child->SetOffset({ _padding.left, _padding.top });
            SetSize(child->Size() + _padding.Total());
        } else {
            SetSize(_padding.Total());
        }
    }

    void RenderAlign::Set(const Alignment alignment)
    {
        if (alignment == _alignment) return;
        _alignment = alignment;
        MarkNeedsLayout();
    }

    void RenderAlign::PerformLayout()
    {
        const auto& c = Constraints();
        glm::vec2 childSize {};
        if (auto* child = Child()) {
            child->Layout(c.Loosen());
            childSize = child->Size();
        }
        // As large as allowed where that is bounded, the child's size where it is not.
        SetSize({ c.HasBoundedWidth() ? c.maxWidth : childSize.x, c.HasBoundedHeight() ? c.maxHeight : childSize.y });
        if (auto* child = Child()) child->SetOffset(_alignment.Place(childSize, Size()));
    }

    float RenderPadding::MinIntrinsicWidth() const { return RenderContainer::MinIntrinsicWidth() + _padding.left + _padding.right; }

    void RenderConstrained::Set(const BoxConstraints& extra)
    {
        if (extra == _extra) return;
        _extra = extra;
        MarkNeedsLayout();
    }

    void RenderConstrained::PerformLayout()
    {
        const BoxConstraints c = _extra.Enforce(Constraints());
        if (auto* child = Child()) {
            child->Layout(c);
            child->SetOffset({});
            SetSize(child->Size());
        } else {
            SetSize(c.Constrain({ 0.f, 0.f }));
        }
    }

    // ---- decoration -------------------------------------------------------------------------------------

    void PaintDecoration(Canvas& canvas, const Decoration& d, const Rect& box)
    {
        const RRect shape { box, d.radius };
        if (d.shadowColor.Visible()) canvas.DrawShadow(shape, d.shadowColor, std::max(d.shadowBlur, 0.5f), d.shadowOffset);
        if (d.gradient) {
            canvas.Save();
            canvas.Translate(box.TopLeft());
            Paint paint;
            paint.gradient = d.gradient;
            canvas.DrawRRect({ Rect::FromSize(box.Size()), d.radius }, paint);
            canvas.Restore();
        } else if (d.color.Visible()) {
            canvas.DrawRRect(shape, Paint::Fill(d.color));
        }
        if (d.borderWidth > 0.f && d.borderColor.Visible()) {
            // Inside the box: the stroke is centred on the outline it is given.
            const float h = d.borderWidth * 0.5f;
            const Radii r { std::max(0.f, d.radius.topLeft - h), std::max(0.f, d.radius.topRight - h),
                            std::max(0.f, d.radius.bottomRight - h), std::max(0.f, d.radius.bottomLeft - h) };
            canvas.DrawRRect({ box.Deflate(h), r }, Paint::Stroked(d.borderColor, d.borderWidth));
        }
    }

    void RenderDecorated::Set(const Decoration& decoration)
    {
        if (decoration == _decoration) return;
        _decoration = decoration;
        MarkNeedsPaint();
    }

    void RenderDecorated::Paint(Canvas& canvas, const glm::vec2 offset)
    {
        PaintDecoration(canvas, _decoration, Rect::XYWH(offset.x, offset.y, Size().x, Size().y));
        RenderContainer::Paint(canvas, offset);
    }

    // ---- a container, in one ----------------------------------------------------------------------------

    namespace {
        bool same(const EdgeInsets& a, const EdgeInsets& b) { return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom; }
    }

    float RenderConstrained::MinIntrinsicWidth() const
    {
        // Its child's, kept to what it allows: a fixed width is that width, whatever the child.
        return std::clamp(RenderContainer::MinIntrinsicWidth(), _extra.minWidth, std::max(_extra.minWidth, _extra.maxWidth));
    }

    void RenderBox::Set(const Config& c)
    {
        const bool layout = !same(c.margin, _config.margin) || !same(c.padding, _config.padding) || c.width != _config.width || c.height != _config.height
                         || c.alignment != _config.alignment || c.hasMargin != _config.hasMargin || c.hasPadding != _config.hasPadding
                         || c.hasSize != _config.hasSize || c.hasDecoration != _config.hasDecoration;
        const bool paint = !(c.decoration == _config.decoration);
        _config = c;
        if (layout) MarkNeedsLayout();
        if (layout || paint) MarkNeedsPaint();
    }

    void RenderBox::PerformLayout()
    {
        // The constraints each of the boxes this stands for would have been given, outermost first.
        const BoxConstraints outer = Constraints();
        const BoxConstraints sized = _config.hasMargin ? outer.Deflate(_config.margin) : outer;
        const BoxConstraints decorated = _config.hasSize ? BoxConstraints {}.Tighten(_config.width, _config.height).Enforce(sized) : sized;
        const BoxConstraints aligned = _config.hasPadding ? decorated.Deflate(_config.padding) : decorated;

        // And the size each would have come out, innermost first.
        auto* child = Child();
        bool have = false;
        glm::vec2 size {}, at {};
        if (_config.alignment) {
            glm::vec2 childSize {};
            if (child) { child->Layout(aligned.Loosen()); childSize = child->Size(); }
            size = aligned.Constrain({ aligned.HasBoundedWidth() ? aligned.maxWidth : childSize.x, aligned.HasBoundedHeight() ? aligned.maxHeight : childSize.y });
            at = _config.alignment->Place(childSize, size);
            have = true;
        } else if (child) {
            child->Layout(aligned);
            size = child->Size();
            have = true;
        }
        if (_config.hasPadding) {
            size = decorated.Constrain((have ? size : glm::vec2 {}) + _config.padding.Total());
            at += glm::vec2(_config.padding.left, _config.padding.top);
            have = true;
        }
        if (_config.hasDecoration) {
            size = have ? decorated.Constrain(size) : decorated.Smallest();
            have = true;
        }
        if (_config.hasSize) {
            size = sized.Constrain(have ? size : decorated.Smallest());
            have = true;
        }
        const glm::vec2 inside = size;
        glm::vec2 origin {};
        if (_config.hasMargin) {
            size = (have ? size : glm::vec2 {}) + _config.margin.Total();
            origin = { _config.margin.left, _config.margin.top };
        }
        SetSize(size);
        _decorated = Rect::XYWH(origin.x, origin.y, inside.x, inside.y);
        if (child) child->SetOffset(origin + at);
    }

    void RenderBox::Paint(Canvas& canvas, const glm::vec2 offset)
    {
        if (_config.hasDecoration) PaintDecoration(canvas, _config.decoration, _decorated.Shift(offset));
        RenderContainer::Paint(canvas, offset);
    }

    bool RenderBox::HitTestSelf(const glm::vec2 position) const
    {
        return _config.hasDecoration && _config.decoration.Visible() && _decorated.Contains(position);
    }

    // ---- flex -------------------------------------------------------------------------------------------

    void RenderFlexible::Set(const float flex, const bool tight)
    {
        if (flex == _flex && tight == _tight) return;
        _flex = flex;
        _tight = tight;
        if (Parent()) Parent()->MarkNeedsLayout();
    }

    float RenderBox::MinIntrinsicWidth() const
    {
        // From the inside out, as it is laid out: the child, its padding, a size that overrides both, its margin.
        float width = RenderContainer::MinIntrinsicWidth();
        if (_config.hasPadding) width += _config.padding.left + _config.padding.right;
        if (_config.hasSize && _config.width >= 0.f) width = _config.width;
        if (_config.hasMargin) width += _config.margin.left + _config.margin.right;
        return width;
    }

    void RenderFlex::Set(const Axis axis, const FlexOptions& options)
    {
        if (axis == _axis && options.mainAxisAlignment == _options.mainAxisAlignment && options.crossAxisAlignment == _options.crossAxisAlignment
            && options.mainAxisSize == _options.mainAxisSize && options.gap == _options.gap) return;
        _axis = axis;
        _options = options;
        MarkNeedsLayout();
    }

    float RenderFlex::MinIntrinsicWidth() const
    {
        if (_axis == Axis::eVertical) return RenderContainer::MinIntrinsicWidth();
        float width = 0.f;
        for (const RenderObject* child : _children) width += child->MinIntrinsicWidth();
        return width + _options.gap * static_cast<float>(std::max<std::size_t>(_children.size(), 1) - 1);
    }

    void RenderFlex::PerformLayout()
    {
        const auto& c = Constraints();
        const bool horizontal = _axis == Axis::eHorizontal;
        const auto main = [&](const glm::vec2 v) { return horizontal ? v.x : v.y; };
        const auto cross = [&](const glm::vec2 v) { return horizontal ? v.y : v.x; };
        const auto vec = [&](const float m, const float x) { return horizontal ? glm::vec2(m, x) : glm::vec2(x, m); };
        const float maxMain = horizontal ? c.maxWidth : c.maxHeight;
        const float minMain = horizontal ? c.minWidth : c.minHeight;
        const float maxCross = horizontal ? c.maxHeight : c.maxWidth;
        const bool boundedMain = std::isfinite(maxMain);
        const bool stretch = _options.crossAxisAlignment == CrossAxisAlignment::eStretch && std::isfinite(maxCross);

        const auto constraintsFor = [&](const float mainMin, const float mainMax) {
            const float crossMin = stretch ? maxCross : 0.f;
            return horizontal ? BoxConstraints { mainMin, mainMax, crossMin, maxCross } : BoxConstraints { crossMin, maxCross, mainMin, mainMax };
        };

        // Inflexible children first, at whatever size they want along the main axis.
        float allocated = 0.f, totalFlex = 0.f, crossSize = 0.f;
        for (auto* child : _children) {
            const auto* flexible = dynamic_cast<const RenderFlexible*>(child);
            if (flexible && flexible->Flex() > 0.f && boundedMain) { totalFlex += flexible->Flex(); continue; }
            child->Layout(constraintsFor(0.f, Infinity));
            allocated += main(child->Size());
            crossSize = std::max(crossSize, cross(child->Size()));
        }
        const float gaps = _children.empty() ? 0.f : _options.gap * static_cast<float>(_children.size() - 1);

        // Then the flexible ones share what is left.
        if (totalFlex > 0.f) {
            const float free = std::max(0.f, maxMain - allocated - gaps);
            for (auto* child : _children) {
                const auto* flexible = dynamic_cast<const RenderFlexible*>(child);
                if (!flexible || flexible->Flex() <= 0.f) continue;
                const float share = free * flexible->Flex() / totalFlex;
                child->Layout(constraintsFor(flexible->Tight() ? share : 0.f, share));
                allocated += main(child->Size());
                crossSize = std::max(crossSize, cross(child->Size()));
            }
        }

        const float used = allocated + gaps;
        const float mainSize = (_options.mainAxisSize == MainAxisSize::eMax && boundedMain) ? maxMain : std::max(used, minMain);
        if (stretch) crossSize = maxCross;
        SetSize(vec(mainSize, crossSize));
        const float finalMain = main(Size()), finalCross = cross(Size());

        // Placing them: the leftover main-axis space is spread as the alignment says.
        const float remaining = std::max(0.f, finalMain - used);
        const auto n = static_cast<float>(_children.size());
        float position = 0.f, between = _options.gap;
        switch (_options.mainAxisAlignment) {
        case MainAxisAlignment::eStart: break;
        case MainAxisAlignment::eEnd: position = remaining; break;
        case MainAxisAlignment::eCenter: position = remaining * 0.5f; break;
        case MainAxisAlignment::eSpaceBetween: if (n > 1.f) between += remaining / (n - 1.f); break;
        case MainAxisAlignment::eSpaceAround: if (n > 0.f) { between += remaining / n; position = remaining / n * 0.5f; } break;
        case MainAxisAlignment::eSpaceEvenly: if (n > 0.f) { between += remaining / (n + 1.f); position = remaining / (n + 1.f); } break;
        }
        for (auto* child : _children) {
            float crossPosition = 0.f;
            const float free = finalCross - cross(child->Size());
            switch (_options.crossAxisAlignment) {
            case CrossAxisAlignment::eStart: case CrossAxisAlignment::eStretch: break;
            case CrossAxisAlignment::eEnd: crossPosition = free; break;
            case CrossAxisAlignment::eCenter: crossPosition = free * 0.5f; break;
            }
            // A child with an alignment of its own (StackAlign) is placed across by it instead.
            if (const auto* aligned = dynamic_cast<const RenderStackAligned*>(child)) {
                const float bias = horizontal ? aligned->GetAlignment().y : aligned->GetAlignment().x;
                crossPosition = free * (bias + 1.f) * 0.5f;
            }
            child->SetOffset(vec(position, crossPosition));
            position += main(child->Size()) + between;
        }
    }

    // ---- stack ------------------------------------------------------------------------------------------

    void RenderPositioned::Set(const PositionedOptions& options)
    {
        _options = options;
        if (Parent()) Parent()->MarkNeedsLayout();
    }

    void RenderStack::Set(const Alignment alignment)
    {
        if (alignment == _alignment) return;
        _alignment = alignment;
        MarkNeedsLayout();
    }

    void RenderStack::PerformLayout()
    {
        const auto& c = Constraints();
        glm::vec2 size {};
        bool any = false;
        for (auto* child : _children) {
            if (dynamic_cast<const RenderPositioned*>(child)) continue;
            child->Layout(c.Loosen());
            size = glm::max(size, child->Size());
            any = true;
        }
        if (any) SetSize(size);
        else SetSize({ c.HasBoundedWidth() ? c.maxWidth : c.minWidth, c.HasBoundedHeight() ? c.maxHeight : c.minHeight });
        const glm::vec2 box = Size();

        for (auto* child : _children) {
            const auto* positioned = dynamic_cast<const RenderPositioned*>(child);
            if (!positioned) {
                const auto* aligned = dynamic_cast<const RenderStackAligned*>(child);
                child->SetOffset((aligned ? aligned->GetAlignment() : _alignment).Place(child->Size(), box));
                continue;
            }
            const auto& o = positioned->Options();
            float w = -1.f, h = -1.f;
            if (o.left && o.right) w = box.x - *o.left - *o.right;
            else if (o.width) w = *o.width;
            if (o.top && o.bottom) h = box.y - *o.top - *o.bottom;
            else if (o.height) h = *o.height;
            BoxConstraints cc;
            if (w >= 0.f) cc.minWidth = cc.maxWidth = w;
            if (h >= 0.f) cc.minHeight = cc.maxHeight = h;
            child->Layout(cc);
            const glm::vec2 s = child->Size();
            const glm::vec2 aligned = _alignment.Place(s, box);
            const float x = o.left ? *o.left : o.right ? box.x - *o.right - s.x : aligned.x;
            const float y = o.top ? *o.top : o.bottom ? box.y - *o.bottom - s.y : aligned.y;
            child->SetOffset({ x, y });
        }
    }

    // ---- text and images ------------------------------------------------------------------------------

    namespace {
        bool sameStyle(const TextStyle& a, const TextStyle& b)
        {
            return a.font == b.font && a.size == b.size && a.color == b.color && a.lineHeight == b.lineHeight && a.letterSpacing == b.letterSpacing
                && a.weight == b.weight && a.italic == b.italic && a.underline == b.underline && a.lineThrough == b.lineThrough;
        }
    }

    void RenderParagraph::Set(const std::string& text, const TextStyle& style, const TextAlign align, const bool wrap, const int maxLines, const bool ellipsis)
    {
        const bool same = _built && text == _text && sameStyle(style, _style) && align == _align;
        if (same && wrap == _wrap && maxLines == _maxLines && ellipsis == _ellipsis) return;
        _text = text;
        _style = style;
        _align = align;
        _wrap = wrap;
        _maxLines = maxLines;
        _ellipsis = ellipsis;
        _paragraph = Paragraph(text, style, Infinity, align);
        _built = true;
        MarkNeedsLayout();
        MarkNeedsPaint();
    }

    void RenderParagraph::PerformLayout()
    {
        const auto& c = Constraints();
        const float width = _wrap && c.HasBoundedWidth() ? c.maxWidth : Infinity;
        if (_maxLines > 0) {
            // Laid out afresh, from all of the text: what was cut to fit a narrower width is not in the paragraph.
            _paragraph = Paragraph(_text, _style, c.HasBoundedWidth() ? c.maxWidth : Infinity, _align);
            if (!_wrap) _paragraph.Layout(Infinity);
            _paragraph.Truncate(static_cast<std::size_t>(_maxLines), _ellipsis);
            // One line that is too long is cut at the width, too: an ellipsis where there is room for no more.
            if (_ellipsis && !_wrap && c.HasBoundedWidth() && _paragraph.Size().x > c.maxWidth) {
                _paragraph = Paragraph(_text, _style, c.maxWidth, _align);
                _paragraph.Truncate(1, true);
            }
        } else {
            _paragraph.Layout(width);
        }
        SetSize(_paragraph.Size());
    }

    float RenderParagraph::MinIntrinsicWidth() const
    {
        if (_ellipsis) return 0.f;
        // From all of its text: the paragraph may hold only what was left once it was cut to its lines.
        const Paragraph whole(_text, _style, Infinity, _align);
        return std::ceil(_wrap ? whole.MinIntrinsicWidth() : whole.MaxIntrinsicWidth());
    }

    void RenderParagraph::Paint(Canvas& canvas, const glm::vec2 offset) { canvas.DrawParagraph(_paragraph, offset); }

    void RenderImage::Set(const kor::ResourceRef<const kor::Image>& image, const ImageFit fit, const glm::vec2 size)
    {
        // An image resized in place is the same handle with another extent: laid out and painted again.
        const glm::u64 generation = image.Valid() ? image->Generation() : 0;
        if (image.Get() == _image.Get() && generation == _generation && fit == _fit && size == _preferred) return;
        _image = image;
        _generation = generation;
        _fit = fit;
        _preferred = size;
        MarkNeedsLayout();
        MarkNeedsPaint();
    }

    void RenderImage::PerformLayout()
    {
        glm::vec2 intrinsic {};
        if (_image.Valid()) intrinsic = { static_cast<float>(_image->Extent().x), static_cast<float>(_image->Extent().y) };
        glm::vec2 want = intrinsic;
        if (_preferred.x >= 0.f) want.x = _preferred.x;
        if (_preferred.y >= 0.f) want.y = _preferred.y;
        // Only one side given: the other follows the image's proportions.
        if (_preferred.x >= 0.f && _preferred.y < 0.f && intrinsic.x > 0.f) want.y = intrinsic.y * want.x / intrinsic.x;
        if (_preferred.y >= 0.f && _preferred.x < 0.f && intrinsic.y > 0.f) want.x = intrinsic.x * want.y / intrinsic.y;
        SetSize(want);
    }

    void RenderImage::Paint(Canvas& canvas, const glm::vec2 offset)
    {
        if (!_image.Valid()) return;
        const glm::vec2 box = Size();
        const glm::vec2 extent { static_cast<float>(_image->Extent().x), static_cast<float>(_image->Extent().y) };
        if (extent.x <= 0.f || extent.y <= 0.f) return;
        Rect dst = Rect::XYWH(offset.x, offset.y, box.x, box.y);
        Rect src = Rect::FromSize(extent);
        switch (_fit) {
        case ImageFit::eFill: break;
        case ImageFit::eContain: {
            const float s = std::min(box.x / extent.x, box.y / extent.y);
            const glm::vec2 size = extent * s;
            dst = Rect::XYWH(offset.x + (box.x - size.x) * 0.5f, offset.y + (box.y - size.y) * 0.5f, size.x, size.y);
            break;
        }
        case ImageFit::eCover: {
            const float s = std::max(box.x / extent.x, box.y / extent.y);
            const glm::vec2 visible = box / s;
            src = Rect::XYWH((extent.x - visible.x) * 0.5f, (extent.y - visible.y) * 0.5f, visible.x, visible.y);
            break;
        }
        case ImageFit::eNone: {
            const glm::vec2 size = glm::min(extent, box);
            dst = Rect::XYWH(offset.x + (box.x - size.x) * 0.5f, offset.y + (box.y - size.y) * 0.5f, size.x, size.y);
            src = Rect::XYWH((extent.x - size.x) * 0.5f, (extent.y - size.y) * 0.5f, size.x, size.y);
            break;
        }
        }
        canvas.DrawImage(_image, dst, src);
    }

    // ---- painting your own ----------------------------------------------------------------------------

    void RenderCustomPaint::Set(std::function<void(Canvas&, glm::vec2)> painter, const glm::vec2 size)
    {
        _painter = std::move(painter);
        if (size != _preferred) { _preferred = size; MarkNeedsLayout(); }
        // A painter is code: there is no telling whether it would draw the same. Rebuilt means repainted.
        MarkNeedsPaint();
    }

    void RenderCustomPaint::PerformLayout()
    {
        const auto& c = Constraints();
        if (auto* child = Child()) {
            child->Layout(c);
            child->SetOffset({});
            SetSize(child->Size());
            return;
        }
        // Nothing of its own to fit: no size, unless it was given one (or its parent sets it).
        glm::vec2 want { 0.f, 0.f };
        if (_preferred.x >= 0.f) want.x = _preferred.x;
        if (_preferred.y >= 0.f) want.y = _preferred.y;
        SetSize(want);
    }

    void RenderCustomPaint::Paint(Canvas& canvas, const glm::vec2 offset)
    {
        if (_painter) {
            canvas.Save();
            canvas.Translate(offset);
            _painter(canvas, Size());
            canvas.Restore();
        }
        RenderContainer::Paint(canvas, offset);
    }

    void RenderShaderBox::Set(std::shared_ptr<ElementShader> shader, std::vector<std::byte> parameters, const Radii radius)
    {
        if (shader == _shader && parameters == _parameters && radius == _radius) return;
        _shader = std::move(shader);
        _parameters = std::move(parameters);
        _radius = radius;
        MarkNeedsPaint();
    }

    void RenderShaderBox::PerformLayout()
    {
        const auto& c = Constraints();
        if (auto* child = Child()) {
            child->Layout(c);
            child->SetOffset({});
            SetSize(child->Size());
        } else {
            SetSize(c.Smallest());   // no content of its own: whatever size its parent sets
        }
    }

    void RenderShaderBox::Paint(Canvas& canvas, const glm::vec2 offset)
    {
        canvas.DrawElement(_shader, Rect::XYWH(offset.x, offset.y, Size().x, Size().y), _parameters, _radius);
        RenderContainer::Paint(canvas, offset);
    }

    void RenderOpacity::Set(const float opacity) { OwnLayer()->SetOpacity(std::clamp(opacity, 0.f, 1.f)); }

    void RenderReveal::Set(const float share)
    {
        const float s = std::clamp(share, 0.f, 1.f);
        if (s == _share) return;
        _share = s;
        MarkNeedsLayout();
        MarkNeedsPaint();
    }

    void RenderReveal::PerformLayout()
    {
        if (auto* child = Child()) {
            child->Layout(Constraints(), true);
            child->SetOffset({});
            SetSize(Constraints().Constrain({ child->Size().x, std::round(child->Size().y * _share) }));
        } else {
            SetSize(Constraints().Smallest());
        }
    }

    void RenderReveal::Paint(Canvas& canvas, const glm::vec2 offset)
    {
        if (_share >= 1.f) { RenderContainer::Paint(canvas, offset); return; }
        if (Size().y <= 0.f) return;
        canvas.Save();
        canvas.ClipRect(Rect::XYWH(offset.x, offset.y, Size().x, Size().y));
        RenderContainer::Paint(canvas, offset);
        canvas.Restore();
    }

    void RenderClip::Set(const Radii& radius)
    {
        if (radius == _radius) return;
        _radius = radius;
        MarkNeedsPaint();
    }

    void RenderClip::Paint(Canvas& canvas, const glm::vec2 offset)
    {
        canvas.Save();
        canvas.ClipRRect({ Rect::XYWH(offset.x, offset.y, Size().x, Size().y), _radius });
        RenderContainer::Paint(canvas, offset);
        canvas.Restore();
    }

    void RenderTranslate::Set(const glm::vec2 by)
    {
        if (by == _by) return;
        _by = by;
        MarkNeedsPaint();
    }

    bool RenderTranslate::HitTest(HitTestResult& result, const glm::vec2 position)
    {
        const bool hit = HitTestChildren(result, position);
        if (hit) result.Add(this, position);
        return hit;
    }

    // ---- scrolling --------------------------------------------------------------------------------------

    float RenderScroll::MinIntrinsicWidth() const { return _axis == Axis::eHorizontal ? 0.f : RenderContainer::MinIntrinsicWidth(); }

    void RenderScroll::Set(const Axis axis)
    {
        if (axis == _axis) return;
        _axis = axis;
        _scroll = 0.f;
        MarkNeedsLayout();
    }

    void RenderScroll::PerformLayout()
    {
        const auto& c = Constraints();
        auto* child = Child();
        if (!child) { SetSize(c.Smallest()); return; }
        // The content has all of the box while it fits. Once there is more of it than shows, the thumb has
        // a strip of its own along the edge, and the content what is left: the one is never over the other.
        constexpr float Gutter = 8.f;
        const bool vertical = _axis == Axis::eVertical;
        if (vertical) child->Layout({ c.minWidth, c.maxWidth, 0.f, Infinity });
        else child->Layout({ 0.f, Infinity, c.minHeight, c.maxHeight });
        glm::vec2 size = c.Constrain(child->Size());
        if (vertical ? child->Size().y > size.y : child->Size().x > size.x) {
            if (vertical) child->Layout({ std::max(c.minWidth - Gutter, 0.f), std::max(c.maxWidth - Gutter, 0.f), 0.f, Infinity });
            else child->Layout({ 0.f, Infinity, std::max(c.minHeight - Gutter, 0.f), std::max(c.maxHeight - Gutter, 0.f) });
            size = c.Constrain(child->Size() + (vertical ? glm::vec2(Gutter, 0.f) : glm::vec2(0.f, Gutter)));
        }
        child->SetOffset({});
        SetSize(size);
        if (_jumping) { _scroll = _jumpTo; _jumping = false; }
        Scroll(0.f);   // kept inside the content, which may have shrunk
    }

    float RenderScroll::MaxScroll() const
    {
        const auto* child = Child();
        if (!child) return 0.f;
        return std::max(0.f, _axis == Axis::eVertical ? child->Size().y - Size().y : child->Size().x - Size().x);
    }

    bool RenderScroll::Scroll(const float by)
    {
        const float next = std::clamp(_scroll + by, 0.f, MaxScroll());
        const bool moved = next != _scroll;
        _scroll = next;
        // The content keeps its picture; only its layer — and the thumb's — move.
        if (auto* child = Child(); child && child->IsRepaintBoundary())
            child->OwnLayer()->SetTransform(Transform::Translation(-ScrollVector()));
        Cull();
        PlaceThumb();
        if (_onScrolled) {
            const float most = MaxScroll();
            if (_scroll != _toldAt || most != _toldMost) {
                _toldAt = _scroll;
                _toldMost = most;
                const auto tell = _onScrolled;
                tell(_scroll, most);
            }
        }
        return moved;
    }

    void RenderScroll::Observe(std::function<void(float, float)> onScrolled, const float jumpTo, const std::uint32_t jump)
    {
        _onScrolled = std::move(onScrolled);
        if (jump != _jump) {
            _jump = jump;
            _jumpTo = jumpTo;
            _jumping = true;
            MarkNeedsLayout();
            MarkNeedsPaint();
        }
    }

    void RenderScroll::Cull()
    {
        auto* child = Child();
        if (!child || !child->IsRepaintBoundary()) return;
        const bool vertical = _axis == Axis::eVertical;
        const float view = vertical ? Size().y : Size().x, content = vertical ? child->Size().y : child->Size().x;
        // Content of a few views is painted whole, and scrolling it paints nothing. More than that, and only
        // what shows and a view either side of it is in the picture: scrolling repaints once the pointer has
        // gone a view's worth, and what is far off costs nothing to paint or to draw.
        if (view <= 0.f || content <= 3.f * view) { child->SetPaintCull(std::nullopt); return; }
        const Rect visible = vertical ? Rect::XYWH(0.f, _scroll, Size().x, view) : Rect::XYWH(_scroll, 0.f, view, Size().y);
        if (const auto& has = child->PaintCull();
            has && has->left <= visible.left && has->top <= visible.top && has->right >= visible.right && has->bottom >= visible.bottom) return;
        child->SetPaintCull(vertical ? Rect::LTRB(-1.e6f, visible.top - view, 1.e6f, visible.bottom + view)
                                     : Rect::LTRB(visible.left - view, -1.e6f, visible.right + view, 1.e6f));
    }

    glm::vec2 RenderScroll::ScrollVector() const { return _axis == Axis::eVertical ? glm::vec2(0.f, _scroll) : glm::vec2(_scroll, 0.f); }

    glm::vec2 RenderScroll::ChildOrigin(const RenderObject& child) const { return child.Offset() - ScrollVector(); }

    void RenderScroll::PlaceThumb()
    {
        if (!_thumb) return;
        const float max = MaxScroll();
        const float view = _axis == Axis::eVertical ? Size().y : Size().x;
        const float content = view + max;
        if (max <= 0.f || content <= 0.f) { _thumb->SetOpacity(0.f); return; }
        const float length = std::max(view * view / content, 16.f);
        const float travel = view - length;
        const float at = travel * (_scroll / max);
        _thumb->SetOpacity(1.f);
        _thumb->SetTransform(Transform::Translation(_axis == Axis::eVertical ? glm::vec2(0.f, at) : glm::vec2(at, 0.f)));
    }

    void RenderScroll::Paint(Canvas& canvas, const glm::vec2 offset)
    {
        auto* child = Child();
        if (!child) return;
        canvas.Save();
        canvas.ClipRect(Rect::XYWH(offset.x, offset.y, Size().x, Size().y));
        if (child->IsRepaintBoundary()) {
            child->RepaintIfNeeded();
            child->OwnLayer()->SetTransform(Transform::Translation(-ScrollVector()));
            canvas.Save();
            canvas.Translate(offset + child->Offset());
            canvas.DrawLayer(child->OwnLayer());
            canvas.Restore();
        } else {
            PaintChild(*child, canvas, offset);
        }
        canvas.Restore();

        // The thumb: a picture of its own, drawn once per size, moved by its layer as the content scrolls.
        const float max = MaxScroll();
        if (max > 0.f) {
            const bool vertical = _axis == Axis::eVertical;
            const float view = vertical ? Size().y : Size().x;
            const float length = std::max(view * view / (view + max), 16.f);
            Canvas thumb;
            const Rect r = vertical ? Rect::XYWH(Size().x - 5.f, 0.f, 3.f, length) : Rect::XYWH(0.f, Size().y - 5.f, length, 3.f);
            thumb.DrawRRect({ r, 1.5f }, Paint::Fill(Theme::Current().textMuted.WithAlpha(0.6f)));
            if (!_thumb) _thumb = Layer::Create();
            _thumb->SetPicture(thumb.Finish());
            canvas.Save();
            canvas.Translate(offset);
            canvas.DrawLayer(_thumb);
            canvas.Restore();
            PlaceThumb();
        }
    }

    bool RenderScroll::HandleEvent(const PointerEvent& event)
    {
        if (event.type != PointerEvent::Type::eScroll) return false;
        const float step = 48.f;
        const float amount = _axis == Axis::eVertical ? -event.delta.y : -(event.delta.x != 0.f ? event.delta.x : event.delta.y);
        return Scroll(amount * step);
    }

    // ---- gestures ---------------------------------------------------------------------------------------

    bool RenderPointerListener::HandleEvent(const PointerEvent& event)
    {
        const auto& o = _options;
        constexpr float slop = 4.f;
        switch (event.type) {
        case PointerEvent::Type::eDown:
            if (event.button != kor::MouseButton::eLeft) return false;
            _pressed = true;
            _panning = false;
            _down = event.local;
            if (o.onTapDown) o.onTapDown(event.local);
            return o.onTap || o.onTapDown || o.onTapUp || o.onPanStart || o.onPanUpdate || o.onPanEnd;
        case PointerEvent::Type::eMove:
            if (!_pressed) return false;
            if (!_panning && (o.onPanStart || o.onPanUpdate || o.onPanEnd) && glm::length(event.local - _down) > slop) {
                _panning = true;
                if (o.onPanStart) o.onPanStart(_down);
                if (o.onPanUpdate) o.onPanUpdate(event.local - _down, event.local);
                return true;   // claims the pointer: the others it was going to stop hearing of it
            }
            if (_panning && o.onPanUpdate) o.onPanUpdate(event.delta, event.local);
            return _panning;
        case PointerEvent::Type::eUp:
            if (!_pressed) return false;
            _pressed = false;
            if (_panning) { if (o.onPanEnd) o.onPanEnd(); }
            else {
                if (o.onTapUp) o.onTapUp();
                const glm::vec2 s = Size();
                if (o.onTap && event.local.x >= 0.f && event.local.y >= 0.f && event.local.x < s.x && event.local.y < s.y) o.onTap();
            }
            _panning = false;
            return true;
        case PointerEvent::Type::eCancel:
            if (_pressed && o.onTapUp && !_panning) o.onTapUp();
            if (_panning && o.onPanEnd) o.onPanEnd();
            _pressed = _panning = false;
            return false;
        case PointerEvent::Type::eEnter: if (o.onEnter) o.onEnter(); return false;
        case PointerEvent::Type::eExit: if (o.onExit) o.onExit(); return false;
        case PointerEvent::Type::eHover: if (o.onHover) o.onHover(event.local); return false;
        case PointerEvent::Type::eScroll: return o.onScroll ? o.onScroll(event.delta) : false;
        }
        return false;
    }

    bool RenderDraggable::HandleEvent(const PointerEvent& event)
    {
        switch (event.type) {
        case PointerEvent::Type::eDown:
            if (event.button != kor::MouseButton::eLeft || !_config.options.enabled) return false;
            _pressed = true;
            _down = event.local;
            return false;
        case PointerEvent::Type::eMove: {
            if (!_pressed || glm::length(event.local - _down) <= 4.f) return false;
            _pressed = false;
            Owner* owner = GetOwner();
            if (!owner || !owner->beginDrag) return false;
            Widget feedback = _config.options.feedback;
            glm::vec2 hotspot = _down;
            if (!feedback) {
                // The thing itself, as big as it is here, a little seen through: held where it was taken hold of.
                // (Built again for the purpose: what is still in its place stays there.) With no child, a ghost of its size.
                const Theme& t = Theme::Current();
                feedback = _config.child ? Opacity(0.8f, SizedBox(Size().x, Size().y, _config.child))
                                         : SizedBox(Size().x, Size().y).Background(t.primary.WithAlpha(0.35f), t.radius).Border(1.f, t.primary, t.radius);
            } else if (!_config.options.feedbackInPlace) {
                hotspot = { 8.f, 8.f };   // a feedback of another size is held by its corner, clear of the pointer
            }
            if (!_config.options.feedback || _config.options.feedbackInPlace) {
                // The thing itself: outlined in the accent, so that it is seen to be in hand — round it, not over it.
                const Theme& t = Theme::Current();
                constexpr float Line = 1.5f;
                const float radius = _config.options.feedbackRadius >= 0.f ? _config.options.feedbackRadius : t.radius;
                feedback = Container({ .padding = EdgeInsets::All(Line),
                                       .decoration = { .borderWidth = Line, .borderColor = t.primary, .radius = radius + Line } }, std::move(feedback));
                hotspot += glm::vec2(Line);
            }
            if (!owner->beginDrag(*this, _config.data, feedback, hotspot, _config.options.onDragEnd)) return false;
            if (_config.options.onDragStart) _config.options.onDragStart();
            return true;   // the drag is the view's now; nothing else under the press gets the gesture
        }
        case PointerEvent::Type::eUp:
        case PointerEvent::Type::eCancel:
            _pressed = false;
            return false;
        default:
            return false;
        }
    }

    // ---- the widgets --------------------------------------------------------------------------------------

    namespace {
        /** @brief A widget whose render object is @p R, made and updated by @p update. */
        template <typename R, typename Config>
        struct BoxWidget final : RenderObjectWidget {
            Config config;
            std::vector<Widget> children;
            void (*update)(R&, const Config&);
            BoxWidget(Config c, std::vector<Widget> kids, void (*u)(R&, const Config&)) : config(std::move(c)), children(std::move(kids)), update(u) {}
            [[nodiscard]] std::unique_ptr<RenderObject> CreateRenderObject() const override { return std::make_unique<R>(); }
            void UpdateRenderObject(RenderObject& object) const override { update(static_cast<R&>(object), config); }
            [[nodiscard]] const std::vector<Widget>& Children() const override { return children; }
        };

        template <typename R, typename Config>
        Widget box(Config config, std::vector<Widget> children, void (*update)(R&, const Config&))
        {
            std::erase_if(children, [](const Widget& w) { return !w; });
            return Widget(std::make_shared<BoxWidget<R, Config>>(std::move(config), std::move(children), update));
        }

        std::vector<Widget> one(Widget child) { return child ? std::vector<Widget> { std::move(child) } : std::vector<Widget> {}; }
    }

    Widget Text(std::string text, TextStyle style, const TextAlign align, const bool wrap, const int maxLines, const bool ellipsis)
    {
        if (!style.font) style.font = Theme::Current().textStyle.font;
        struct Config { std::string text; TextStyle style; TextAlign align; bool wrap; int maxLines = 0; bool ellipsis = false; };
        return box<RenderParagraph, Config>({ std::move(text), std::move(style), align, wrap, maxLines, ellipsis }, {},
            [](RenderParagraph& r, const Config& c) { r.Set(c.text, c.style, c.align, c.wrap, c.maxLines, c.ellipsis); });
    }

    Widget Flex(const Axis axis, std::vector<Widget> children, FlexOptions options)
    {
        struct Config { Axis axis; FlexOptions options; };
        return box<RenderFlex, Config>({ axis, options }, std::move(children),
            [](RenderFlex& r, const Config& c) { r.Set(c.axis, c.options); });
    }

    Widget Row(std::vector<Widget> children, const FlexOptions options) { return Flex(Axis::eHorizontal, std::move(children), options); }
    Widget Column(std::vector<Widget> children, const FlexOptions options) { return Flex(Axis::eVertical, std::move(children), options); }

    Widget Expanded(Widget child, const float flex)
    {
        struct Config { float flex; };
        return box<RenderFlexible, Config>({ flex }, one(std::move(child)), [](RenderFlexible& r, const Config& c) { r.Set(c.flex, true); });
    }

    Widget Flexible(Widget child, const float flex)
    {
        struct Config { float flex; };
        return box<RenderFlexible, Config>({ flex }, one(std::move(child)), [](RenderFlexible& r, const Config& c) { r.Set(c.flex, false); });
    }

    Widget Padding(const EdgeInsets padding, Widget child)
    {
        return box<RenderPadding, EdgeInsets>(padding, one(std::move(child)), [](RenderPadding& r, const EdgeInsets& c) { r.Set(c); });
    }

    Widget Align(const Alignment alignment, Widget child)
    {
        return box<RenderAlign, Alignment>(alignment, one(std::move(child)), [](RenderAlign& r, const Alignment& c) { r.Set(c); });
    }

    Widget Center(Widget child) { return Align(Alignment::Center(), std::move(child)); }

    Widget ConstrainedBox(const BoxConstraints constraints, Widget child)
    {
        return box<RenderConstrained, BoxConstraints>(constraints, one(std::move(child)), [](RenderConstrained& r, const BoxConstraints& c) { r.Set(c); });
    }

    Widget SizedBox(const float width, const float height, Widget child)
    {
        return ConstrainedBox(BoxConstraints {}.Tighten(width, height), std::move(child));
    }

    Widget DecoratedBox(Decoration decoration, Widget child)
    {
        return box<RenderDecorated, Decoration>(std::move(decoration), one(std::move(child)), [](RenderDecorated& r, const Decoration& c) { r.Set(c); });
    }

    Widget Container(ContainerOptions o, Widget child)
    {
        // More than one thing at once: one box that is all of them. (One alone is the widget for it, below.)
        const bool margin = o.margin.Horizontal() > 0.f || o.margin.Vertical() > 0.f;
        const bool padding = o.padding.Horizontal() > 0.f || o.padding.Vertical() > 0.f;
        const bool size = o.width >= 0.f || o.height >= 0.f;
        const bool decoration = o.decoration.Visible();
        if (static_cast<int>(margin) + static_cast<int>(padding) + static_cast<int>(size) + static_cast<int>(decoration) + static_cast<int>(o.alignment.has_value()) > 1) {
            RenderBox::Config config { o.margin, o.padding, o.width, o.height, std::move(o.decoration), o.alignment, margin, padding, size, decoration };
            return box<RenderBox, RenderBox::Config>(std::move(config), one(std::move(child)), [](RenderBox& r, const RenderBox::Config& c) { r.Set(c); });
        }
        Widget w = std::move(child);
        if (o.alignment) w = Align(*o.alignment, std::move(w));
        if (o.padding.Horizontal() > 0.f || o.padding.Vertical() > 0.f) w = Padding(o.padding, std::move(w));
        if (o.decoration.Visible()) w = DecoratedBox(o.decoration, std::move(w));
        if (o.width >= 0.f || o.height >= 0.f) w = SizedBox(o.width, o.height, std::move(w));
        if (o.margin.Horizontal() > 0.f || o.margin.Vertical() > 0.f) w = Padding(o.margin, std::move(w));
        return w ? w : SizedBox(0.f, 0.f);
    }

    Widget Stack(std::vector<Widget> children, const Alignment alignment)
    {
        return box<RenderStack, Alignment>(alignment, std::move(children), [](RenderStack& r, const Alignment& c) { r.Set(c); });
    }

    Widget Positioned(PositionedOptions options, Widget child)
    {
        return box<RenderPositioned, PositionedOptions>(options, one(std::move(child)), [](RenderPositioned& r, const PositionedOptions& c) { r.Set(c); });
    }

    Widget StackAlign(const Alignment alignment, Widget child)
    {
        return box<RenderStackAligned, Alignment>(alignment, one(std::move(child)), [](RenderStackAligned& r, const Alignment& c) { r.Set(c); });
    }

    Widget RepaintBoundary(Widget child)
    {
        struct Config {};
        return box<RenderRepaintBoundary, Config>({}, one(std::move(child)), [](RenderRepaintBoundary&, const Config&) {});
    }

    Widget Opacity(const float opacity, Widget child)
    {
        return box<RenderOpacity, float>(opacity, one(std::move(child)), [](RenderOpacity& r, const float& c) { r.Set(c); });
    }

    Widget detail::RevealBox(const float share, Widget child)
    {
        return box<RenderReveal, float>(share, one(std::move(child)), [](RenderReveal& r, const float& c) { r.Set(c); });
    }

    Widget ClipRRect(const Radii radius, Widget child)
    {
        return box<RenderClip, Radii>(radius, one(std::move(child)), [](RenderClip& r, const Radii& c) { r.Set(c); });
    }

    Widget Translate(const glm::vec2 offset, Widget child)
    {
        return box<RenderTranslate, glm::vec2>(offset, one(std::move(child)), [](RenderTranslate& r, const glm::vec2& c) { r.Set(c); });
    }

    Widget IgnorePointer(Widget child)
    {
        struct None { bool operator==(const None&) const = default; };
        return box<RenderIgnorePointer, None>({}, one(std::move(child)), [](RenderIgnorePointer&, const None&) {});
    }

    Widget Draggable(DragData data, Widget child, DraggableOptions options)
    {
        Widget shown = child;
        return box<RenderDraggable, RenderDraggable::Config>({ std::move(data), std::move(options), std::move(shown) }, one(std::move(child)),
            [](RenderDraggable& r, const RenderDraggable::Config& c) { r.Set(c); });
    }

    Widget DropTarget(DropTargetOptions options, Widget child)
    {
        return box<RenderDropTarget, DropTargetOptions>(std::move(options), one(std::move(child)),
            [](RenderDropTarget& r, const DropTargetOptions& c) { r.Set(c); });
    }

    Widget ScrollView(Widget child, const Axis axis)
    {
        // The content gets a layer of its own, which is what scrolling moves.
        return box<RenderScroll, Axis>(axis, one(RepaintBoundary(std::move(child))), [](RenderScroll& r, const Axis& c) { r.Set(c); });
    }

    Widget ScrollView(Widget child, ScrollOptions options)
    {
        return box<RenderScroll, ScrollOptions>(std::move(options), one(RepaintBoundary(std::move(child))),
            [](RenderScroll& r, const ScrollOptions& c) { r.Set(c.axis); r.Observe(c.onScrolled, c.jumpTo, c.jump); });
    }

    Widget GestureDetector(GestureOptions options, Widget child)
    {
        return box<RenderPointerListener, GestureOptions>(std::move(options), one(std::move(child)),
            [](RenderPointerListener& r, const GestureOptions& c) { r.Set(c); });
    }

    Widget CustomPaint(std::function<void(Canvas&, glm::vec2)> painter, const glm::vec2 size, Widget child)
    {
        struct Config { std::function<void(Canvas&, glm::vec2)> painter; glm::vec2 size; };
        return box<RenderCustomPaint, Config>({ std::move(painter), size }, one(std::move(child)),
            [](RenderCustomPaint& r, const Config& c) { r.Set(c.painter, c.size); });
    }

    Widget ShaderBox(std::shared_ptr<ElementShader> shader, std::vector<std::byte> parameters, const Radii radius, Widget child)
    {
        struct Config { std::shared_ptr<ElementShader> shader; std::vector<std::byte> parameters; Radii radius; };
        return box<RenderShaderBox, Config>({ std::move(shader), std::move(parameters), radius }, one(std::move(child)),
            [](RenderShaderBox& r, const Config& c) { r.Set(c.shader, c.parameters, c.radius); });
    }

    Widget Image(kor::ResourceRef<const kor::Image> image, const ImageFit fit, const glm::vec2 size)
    {
        struct Config { kor::ResourceRef<const kor::Image> image; ImageFit fit; glm::vec2 size; };
        return box<RenderImage, Config>({ std::move(image), fit, size }, {},
            [](RenderImage& r, const Config& c) { r.Set(c.image, c.fit, c.size); });
    }
}
