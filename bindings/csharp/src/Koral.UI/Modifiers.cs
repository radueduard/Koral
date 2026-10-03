using System.Numerics;

namespace Koral.UI;

/// <summary>
/// The modifier chain: each call wraps the widget in one of the building blocks and returns the result, so a
/// chain reads inside out — <c>Text("Hi").Padding(8).Background(surface, 12).OnTap(Go)</c> is a clickable,
/// rounded, filled box with eight units around the text. Order matters, as nesting does: padding before a
/// background is inside it, padding after is outside (a margin).
/// </summary>
public static class WidgetModifiers
{
    public static Widget Padding(this Widget widget, float all) => Widgets.Padding(EdgeInsets.All(all), widget);
    public static Widget Padding(this Widget widget, float horizontal, float vertical) => Widgets.Padding(EdgeInsets.Symmetric(horizontal, vertical), widget);
    public static Widget Padding(this Widget widget, EdgeInsets insets) => Widgets.Padding(insets, widget);

    /// <summary>A filled box behind it, with rounded corners.</summary>
    public static Widget Background(this Widget widget, Color color, Radii radius = default) =>
        Widgets.DecoratedBox(new Decoration().SetColor(color).SetRadius(radius), widget);

    /// <summary>An outline, drawn inside its box.</summary>
    public static Widget Border(this Widget widget, float width, Color color, Radii radius = default) =>
        Widgets.DecoratedBox(new Decoration().SetBorder(width, color).SetRadius(radius), widget);

    /// <summary>A shadow under its box.</summary>
    public static Widget Shadow(this Widget widget, Color color, float blur, Vector2 offset = default, Radii radius = default) =>
        Widgets.DecoratedBox(new Decoration().SetShadow(color, blur, offset).SetRadius(radius), widget);

    /// <summary>Any decoration behind it.</summary>
    public static Widget Decorated(this Widget widget, Decoration decoration) => Widgets.DecoratedBox(decoration, widget);

    /// <summary>An exact size; a negative dimension is left to the widget.</summary>
    public static Widget Size(this Widget widget, float width, float height) => Widgets.SizedBox(width, height, widget);
    public static Widget Width(this Widget widget, float width) => Widgets.SizedBox(width, -1, widget);
    public static Widget Height(this Widget widget, float height) => Widgets.SizedBox(-1, height, widget);
    public static Widget Constrained(this Widget widget, BoxConstraints constraints) => Widgets.ConstrainedBox(constraints, widget);

    /// <summary>A weighted share of a Row's or Column's leftover space, filled.</summary>
    public static Widget Expanded(this Widget widget, float flex = 1f) => Widgets.Expanded(widget, flex);
    /// <summary>A weighted share it may leave part of.</summary>
    public static Widget Flexible(this Widget widget, float flex = 1f) => Widgets.Flexible(widget, flex);

    /// <summary>Fills what it is given, the widget in the middle.</summary>
    public static Widget Center(this Widget widget) => Widgets.Center(widget);
    public static Widget Align(this Widget widget, Alignment alignment) => Widgets.Align(alignment, widget);
    /// <summary>Its own place in a Stack, or across a Row or Column.</summary>
    public static Widget AlignSelf(this Widget widget, Alignment alignment) => Widgets.StackAlign(alignment, widget);
    public static Widget Positioned(this Widget widget, PositionedOptions options) => Widgets.Positioned(options, widget);

    public static Widget Opacity(this Widget widget, float opacity) => Widgets.Opacity(opacity, widget);
    /// <summary>Cut to its box, with rounded corners.</summary>
    public static Widget Clip(this Widget widget, Radii radius = default) => Widgets.ClipRRect(radius, widget);
    /// <summary>Moved where it is drawn and hit; its layout unchanged.</summary>
    public static Widget Offset(this Widget widget, Vector2 by) => Widgets.Translate(by, widget);
    public static Widget Scrollable(this Widget widget, Axis axis = Axis.eVertical) => Widgets.ScrollView(widget, axis);
    public static Widget RepaintBoundary(this Widget widget) => Widgets.RepaintBoundary(widget);

    /// <summary>Calls <paramref name="onTap"/> when clicked, and nothing else: no hover or press look (see Button for those).</summary>
    public static Widget OnTap(this Widget widget, Action onTap) => Widgets.GestureDetector(new GestureOptions { OnTap = onTap }, widget);
    public static Widget Gestures(this Widget widget, GestureOptions options) => Widgets.GestureDetector(options, widget);
}

/// <summary>Drag and drop, as modifiers.</summary>
public static class DragModifiers
{
    /// <summary>Can be picked up and dragged, carrying <paramref name="data"/>.</summary>
    public static Widget Draggable(this Widget widget, DragData data, DraggableOptions? options = null) => Widgets.Draggable(data, widget, options);
    /// <summary>Takes drags of <paramref name="type"/> dropped on it.</summary>
    public static Widget OnDrop(this Widget widget, string type, Action<DragData> onDrop) =>
        Widgets.DropTarget(new DropTargetOptions { AcceptsType = type, OnDrop = (d, _) => onDrop(d) }, widget);
    public static Widget DropTarget(this Widget widget, DropTargetOptions options) => Widgets.DropTarget(options, widget);
}
