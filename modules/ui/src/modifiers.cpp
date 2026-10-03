//
// The modifier chain: each call wraps the widget in a building block.
//

#include <kui/widgets.h>

namespace kui
{
    Widget Widget::Padding(const float all) const { return kui::Padding(EdgeInsets::All(all), *this); }
    Widget Widget::Padding(const float horizontal, const float vertical) const { return kui::Padding(EdgeInsets::Symmetric(horizontal, vertical), *this); }
    Widget Widget::Padding(const EdgeInsets insets) const { return kui::Padding(insets, *this); }
    Widget Widget::Background(const Color color, const Radii radius) const { return kui::DecoratedBox({ .color = color, .radius = radius }, *this); }
    Widget Widget::Border(const float width, const Color color, const Radii radius) const
    {
        return kui::DecoratedBox({ .borderWidth = width, .borderColor = color, .radius = radius }, *this);
    }
    Widget Widget::Shadow(const Color color, const float blur, const glm::vec2 offset, const Radii radius) const
    {
        return kui::DecoratedBox({ .radius = radius, .shadowColor = color, .shadowBlur = blur, .shadowOffset = offset }, *this);
    }
    Widget Widget::Decorated(const Decoration& decoration) const { return kui::DecoratedBox(decoration, *this); }
    Widget Widget::Size(const float width, const float height) const { return kui::SizedBox(width, height, *this); }
    Widget Widget::Width(const float width) const { return kui::SizedBox(width, -1.f, *this); }
    Widget Widget::Height(const float height) const { return kui::SizedBox(-1.f, height, *this); }
    Widget Widget::Constrained(const BoxConstraints constraints) const { return kui::ConstrainedBox(constraints, *this); }
    Widget Widget::Expanded(const float flex) const { return kui::Expanded(*this, flex); }
    Widget Widget::Flexible(const float flex) const { return kui::Flexible(*this, flex); }
    Widget Widget::Center() const { return kui::Center(*this); }
    Widget Widget::Align(const Alignment& alignment) const { return kui::Align(alignment, *this); }
    Widget Widget::AlignSelf(const Alignment& alignment) const { return kui::StackAlign(alignment, *this); }
    Widget Widget::Positioned(const PositionedOptions& options) const { return kui::Positioned(options, *this); }
    Widget Widget::Opacity(const float opacity) const { return kui::Opacity(opacity, *this); }
    Widget Widget::Clip(const Radii radius) const { return kui::ClipRRect(radius, *this); }
    Widget Widget::Offset(const glm::vec2 by) const { return kui::Translate(by, *this); }
    Widget Widget::Scrollable(const Axis axis) const { return kui::ScrollView(*this, axis); }
    Widget Widget::RepaintBoundary() const { return kui::RepaintBoundary(*this); }
    Widget Widget::OnTap(std::function<void()> onTap) const
    {
        GestureOptions options;
        options.onTap = std::move(onTap);
        return kui::GestureDetector(std::move(options), *this);
    }
    Widget Widget::Gestures(GestureOptions options) const { return kui::GestureDetector(std::move(options), *this); }
    Widget Widget::Draggable(DragData data) const { return kui::Draggable(std::move(data), *this); }
    Widget Widget::Draggable(DragData data, DraggableOptions options) const { return kui::Draggable(std::move(data), *this, std::move(options)); }
    Widget Widget::OnDrop(std::string type, std::function<void(const DragData&)> onDrop) const
    {
        DropTargetOptions options;
        options.AcceptsType(std::move(type));
        options.onDrop = [onDrop = std::move(onDrop)](const DragData& data, glm::vec2) { if (onDrop) onDrop(data); };
        return kui::DropTarget(std::move(options), *this);
    }
    Widget Widget::DropTarget(DropTargetOptions options) const { return kui::DropTarget(std::move(options), *this); }
}
