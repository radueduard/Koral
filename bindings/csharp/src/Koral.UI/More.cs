using System.Numerics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Koral.UI.Native;

namespace Koral.UI;

// The rest of koral-ui's widgets: the controls a tools interface is made of, a list of which only what is in view
// exists, the layouts that ask how big something is, and a theme for part of an interface.

/// <summary>An item of a menu: a label and what choosing it does — or the line between two groups.</summary>
public sealed record MenuItem(string Label, Action? OnSelected = null, bool Enabled = true)
{
    /// <summary>A line between two groups of items.</summary>
    public static MenuItem Separator { get; } = new("") { IsSeparator = true };
    public bool IsSeparator { get; init; }
}

/// <summary>A menu of a menu bar: its title, and its items.</summary>
public sealed record Menu(string Title, IReadOnlyList<MenuItem> Items);

/// <summary>A column of a <see cref="Widgets.Table"/>: its title, and its width — or, with none, its share of what is left.</summary>
public sealed record TableColumn(string Title, float? Width = null, float Flex = 1f);

/// <summary>What a status bar's message is.</summary>
public enum StatusLevel { Info, Warning, Error }

/// <summary>How a <see cref="Widgets.Plot"/> draws its values.</summary>
public enum PlotKind { Line, Bars }

/// <summary>kui::LazyListOptions: what a list of which only the items in view exist is.</summary>
public sealed record LazyListOptions
{
    public Axis Axis { get; set; } = Axis.eVertical;
    /// <summary>How long every item is. Not given: each is as long as it turns out to be.</summary>
    public float? ItemExtent { get; set; }
    /// <summary>How long an item not yet built is taken to be.</summary>
    public float EstimatedExtent { get; set; } = 40f;
    public float Gap { get; set; }
    public float PaddingStart { get; set; }
    public float PaddingEnd { get; set; }
    /// <summary>The items [first, last) the list keeps, as that changes.</summary>
    public Action<int, int>? OnRange { get; set; }
    /// <summary>Where it is: the first item in view, and how far into it the view starts.</summary>
    public Action<int, float>? OnScrolled { get; set; }
    /// <summary>The item to put first in view, <see cref="JumpOffset"/> into it, when <see cref="Jump"/> is not what it last was (and not 0).</summary>
    public int JumpIndex { get; set; }
    public float JumpOffset { get; set; }
    public uint Jump { get; set; }

    public LazyListOptions SetAxis(Axis value) { Axis = value; return this; }
    public LazyListOptions SetItemExtent(float value) { ItemExtent = value; return this; }
    public LazyListOptions SetEstimatedExtent(float value) { EstimatedExtent = value; return this; }
    public LazyListOptions SetGap(float value) { Gap = value; return this; }
    public LazyListOptions SetPadding(float start, float end) { PaddingStart = start; PaddingEnd = end; return this; }
    public LazyListOptions SetOnRange(Action<int, int> value) { OnRange = value; return this; }
    public LazyListOptions SetOnScrolled(Action<int, float> value) { OnScrolled = value; return this; }
    public LazyListOptions JumpTo(int index, float offset, uint token) { JumpIndex = index; JumpOffset = offset; Jump = token; return this; }
}

/// <summary>What a <see cref="Widgets.CustomLayout"/>'s rule measures and places its children through.</summary>
public readonly unsafe ref struct LayoutContext
{
    private readonly IntPtr _native;
    internal LayoutContext(IntPtr native) => _native = native;
    public int Count => (int)KuiNative.kui_layout_count(_native);
    /// <summary>Lays child <paramref name="index"/> out with <paramref name="constraints"/>; its size.</summary>
    public Vector2 Measure(int index, BoxConstraints constraints)
    {
        float w, h;
        KuiNative.kui_layout_measure(_native, (nuint)index, constraints.MinWidth, constraints.MaxWidth, constraints.MinHeight, constraints.MaxHeight, &w, &h);
        return new(w, h);
    }
    public void Place(int index, Vector2 at) => KuiNative.kui_layout_place(_native, (nuint)index, at.X, at.Y);
}

/// <summary>A rule of layout: measures and places the children through the context, and says how big the whole is.</summary>
public delegate Vector2 LayoutRule(LayoutContext context, BoxConstraints constraints);

/// <summary>How the system looks: whether it is dark, and its accent. <see cref="Known"/> is false where it has no such setting.</summary>
public readonly record struct SystemAppearance(bool Known, bool Dark, Color Accent)
{
    public static unsafe SystemAppearance Query()
    {
        byte dark;
        KuiColor accent;
        var known = KuiNative.kui_system_appearance(&dark, &accent) != 0;
        return new(known, dark != 0, Color.From(accent));
    }
}

public static unsafe partial class Widgets
{
    private static KuiFloatAction Whole(Action<int>? a) => a is null ? default : Callbacks.Float(v => a((int)MathF.Round(v)));

    // -- text and what is like the controls there already
    /// <summary>Text of <paramref name="maxLines"/> lines at the most (0: any number), the last ending in an ellipsis when asked.</summary>
    public static Widget Text(string text, TextStyle style, TextAlign align, bool wrap, int maxLines, bool ellipsis = false)
    {
        var s = style.Native;
        return Made(KuiNative.kui_text_lines(text, &s, (uint)align, KuiNative.Bool(wrap), maxLines, KuiNative.Bool(ellipsis)));
    }

    /// <summary>A slider that also says when it is let go of. <paramref name="vertical"/>: upright, the value growing upwards.</summary>
    public static Widget Slider(float value, Action<float>? onChanged, float min, float max, Action? onFinished, bool vertical = false) =>
        Made(vertical ? KuiNative.kui_slider_vertical(value, Callbacks.Float(onChanged), min, max, Callbacks.Action(onFinished))
                      : KuiNative.kui_slider_finished(value, Callbacks.Float(onChanged), min, max, Callbacks.Action(onFinished)));

    /// <summary>A line across (or down) between two things, in the theme's border colour.</summary>
    public static Widget Separator(Axis axis = Axis.eHorizontal, float thickness = 1f) =>
        Made(KuiNative.kui_separator(KuiNative.Bool(axis == Axis.eVertical), thickness));
    /// <summary><paramref name="child"/> faded and deaf to the pointer while <paramref name="disabled"/>.</summary>
    public static Widget Disabled(Widget child, bool disabled = true) => WithChild(child, c => KuiNative.kui_disabled(c, KuiNative.Bool(disabled)));
    public static Widget RadioButton(bool selected, Action? onSelected, string label = "") =>
        Made(KuiNative.kui_radio_button(KuiNative.Bool(selected), Callbacks.Action(onSelected), label));
    /// <summary>A line of a list that can be picked.</summary>
    public static Widget Selectable(string label, bool selected, Action? onTap) =>
        Made(KuiNative.kui_selectable(label, KuiNative.Bool(selected), Callbacks.Action(onTap)));
    /// <summary>A header that folds <paramref name="child"/> under it: whoever builds it keeps whether it is open.</summary>
    public static Widget CollapsingHeader(string title, bool open, Action<bool>? onToggled, Widget? child = null) =>
        WithChild(child, c => KuiNative.kui_collapsing_header(title, KuiNative.Bool(open), Callbacks.Bool(onToggled), c));

    /// <summary>A node of a tree, its children further in while it is open.</summary>
    public static Widget TreeNode(string label, bool open, Action<bool>? onToggled, IReadOnlyList<Widget?>? children = null,
                                  bool leaf = false, bool selected = false, Action? onTap = null) =>
        WithChildren(children ?? [], (p, n) => KuiNative.kui_tree_node(label, KuiNative.Bool(open), Callbacks.Bool(onToggled), (IntPtr*)p, n,
                                                                         KuiNative.Bool(leaf), KuiNative.Bool(selected), Callbacks.Action(onTap)));

    /// <summary>A row of titles, the one in front underlined.</summary>
    public static Widget TabBar(IReadOnlyList<string> tabs, int selected, Action<int>? onSelected) =>
        Made(KuiNative.kui_tab_bar([.. tabs], (nuint)tabs.Count, selected, Whole(onSelected)));
    /// <summary><paramref name="child"/>, with <paramref name="text"/> shown by the pointer while it rests on it.</summary>
    public static Widget Tooltip(string text, Widget child) => WithChild(child, c => KuiNative.kui_tooltip(text, c));
    /// <summary>Tells <paramref name="onChanged"/> how big <paramref name="child"/> is laid out — in the interface's units, and in pixels — when that changes.</summary>
    public static Widget SizeObserver(Action<Vector2, Vector2> onChanged, Widget child) =>
        WithChild(child, c => KuiNative.kui_size_observer(Callbacks.Pan(onChanged), c));

    /// <summary><paramref name="child"/>, and while <paramref name="open"/> a <paramref name="dialog"/> over it and everything else.</summary>
    public static Widget Modal(bool open, Widget child, Widget dialog, Action? onDismiss = null)
    {
        IntPtr c = Lend(child), d = IntPtr.Zero;
        try
        {
            d = Lend(dialog);
            return Made(KuiNative.kui_modal(KuiNative.Bool(open), c, d, Callbacks.Action(onDismiss)));
        }
        finally { Return(c); Return(d); }
    }

    // -- numbers, choices and colours
    /// <summary>
    /// A number changed by dragging across it sideways; the pointer is held where it is, unseen, while it is.
    /// <paramref name="vertical"/>: upright, its label over its value, dragged up for more. <paramref name="typeable"/>:
    /// clicked without being dragged, it turns into a text box for typing the value exactly.
    /// </summary>
    public static Widget DragValue(float value, Action<float>? onChanged, float speed = 1f, float min = float.NegativeInfinity,
                                   float max = float.PositiveInfinity, int decimals = 2, string? label = null, float width = -1f,
                                   bool vertical = false, bool typeable = true, bool wrap = false) =>
        Made(vertical ? KuiNative.kui_drag_value_vertical(value, Callbacks.Float(onChanged), speed, min, max, decimals, label, width, KuiNative.Bool(typeable), KuiNative.Bool(wrap))
                      : KuiNative.kui_drag_value(value, Callbacks.Float(onChanged), speed, min, max, decimals, label, width, KuiNative.Bool(typeable), KuiNative.Bool(wrap)));
    /// <summary>A field that opens a list of <paramref name="items"/> under itself.</summary>
    public static Widget Dropdown(IReadOnlyList<string> items, int selected, Action<int>? onChanged, float width = -1f, string? placeholder = null) =>
        Made(KuiNative.kui_dropdown([.. items], (nuint)items.Count, selected, Whole(onChanged), width, placeholder));
    /// <summary>One of <paramref name="steps"/> places along a bar, each with a label where <paramref name="labels"/> has one.</summary>
    public static Widget StepSlider(int value, int steps, Action<int>? onChanged, IReadOnlyList<string>? labels = null, float width = -1f) =>
        Made(KuiNative.kui_step_slider(value, steps, Whole(onChanged), labels is null ? null : [.. labels], (nuint)(labels?.Count ?? 0), width));
    /// <summary>A square of every saturation and brightness of a hue, a bar of hues, and one of alpha.</summary>
    public static Widget ColorPicker(Color color, Action<Color>? onChanged, bool alpha = true, bool hex = true, float width = 220f) =>
        Made(KuiNative.kui_color_picker(color.Native, Callbacks.Colour(onChanged), KuiNative.Bool(alpha), KuiNative.Bool(hex), width));
    /// <summary>A swatch (and a label) that opens a picker under itself.</summary>
    public static Widget ColorEdit(Color color, Action<Color>? onChanged, string? label = null, bool alpha = true) =>
        Made(KuiNative.kui_color_edit(color.Native, Callbacks.Colour(onChanged), label, KuiNative.Bool(alpha)));

    /// <summary>The stops of a gradient, dragged along a bar of it; a stop picked can be given another colour.</summary>
    public static Widget GradientEditor(IReadOnlyList<GradientStop> stops, Action<IReadOnlyList<GradientStop>>? onChanged, float width = -1f, bool picker = true)
    {
        var flat = new float[stops.Count * 5];
        for (var i = 0; i < stops.Count; ++i)
        {
            var (offset, c) = stops[i];
            flat[i * 5] = offset; flat[i * 5 + 1] = c.R; flat[i * 5 + 2] = c.G; flat[i * 5 + 3] = c.B; flat[i * 5 + 4] = c.A;
        }
        fixed (float* p = flat) return Made(KuiNative.kui_gradient_editor(p, (nuint)stops.Count, Callbacks.Stops(onChanged), width, KuiNative.Bool(picker)));
    }

    /// <summary>Values as a line or as bars. With no <paramref name="min"/> or <paramref name="max"/>, the values' own.</summary>
    public static Widget Plot(ReadOnlySpan<float> values, PlotKind kind = PlotKind.Line, float? min = null, float? max = null,
                              Vector2? size = null, string? overlay = null, Color? color = null)
    {
        fixed (float* p = values)
            return Made(KuiNative.kui_plot(p, (nuint)values.Length, (uint)kind, min ?? float.NaN, max ?? float.NaN,
                                           (size ?? new Vector2(-1, 60)).Native(), overlay, (color ?? Color.Transparent).Native));
    }

    /// <summary>Rows of cells under titled columns. <paramref name="rows"/> are its rows, each a cell a column (null: empty).</summary>
    public static Widget Table(IReadOnlyList<TableColumn> columns, IReadOnlyList<IReadOnlyList<Widget?>> rows, bool header = true,
                               bool striped = true, bool borders = true, float rowHeight = -1f)
    {
        var native = new KuiTableColumn[columns.Count];
        var cells = new IntPtr[rows.Count * columns.Count];
        try
        {
            for (var i = 0; i < native.Length; ++i)
                native[i] = new() { title = Marshal.StringToCoTaskMemUTF8(columns[i].Title), width = columns[i].Width ?? -1f, flex = columns[i].Flex };
            for (var r = 0; r < rows.Count; ++r)
                for (var c = 0; c < columns.Count && c < rows[r].Count; ++c)
                    cells[r * columns.Count + c] = Lend(rows[r][c]);
            fixed (KuiTableColumn* cp = native)
            fixed (IntPtr* p = cells)
                return Made(KuiNative.kui_table(cp, (nuint)native.Length, p, (nuint)rows.Count, KuiNative.Bool(header), KuiNative.Bool(striped),
                                                KuiNative.Bool(borders), rowHeight));
        }
        finally
        {
            foreach (var c in native) Marshal.FreeCoTaskMem(c.title);
            ReturnAll(cells);
        }
    }

    // -- menus
    private static Widget WithMenuItems(IReadOnlyList<MenuItem> items, Func<IntPtr, nuint, IntPtr> make)
    {
        var native = new KuiMenuItem[items.Count];
        try
        {
            for (var i = 0; i < native.Length; ++i) native[i] = Item(items[i]);
            fixed (KuiMenuItem* p = native) return Made(make((IntPtr)p, (nuint)native.Length));
        }
        finally { foreach (var item in native) Marshal.FreeCoTaskMem(item.label); }
    }

    private static KuiMenuItem Item(MenuItem item) => new()
    {
        label = Marshal.StringToCoTaskMemUTF8(item.Label), on_selected = Callbacks.Action(item.OnSelected),
        disabled = KuiNative.Bool(!item.Enabled), separator = KuiNative.Bool(item.IsSeparator),
    };

    /// <summary><paramref name="child"/>, with a menu of <paramref name="items"/> where the right button is pressed on it.</summary>
    public static Widget ContextMenu(IReadOnlyList<MenuItem> items, Widget child)
    {
        var c = Lend(child);
        try { return WithMenuItems(items, (p, n) => KuiNative.kui_context_menu((KuiMenuItem*)p, n, c)); }
        finally { Return(c); }
    }

    /// <summary>A row of titles, each opening its menu under itself.</summary>
    public static Widget MenuBar(IReadOnlyList<Menu> menus)
    {
        var native = new KuiMenu[menus.Count];
        var items = new KuiMenuItem[menus.Count][];
        var pins = new GCHandle[menus.Count];
        try
        {
            for (var m = 0; m < native.Length; ++m)
            {
                items[m] = [.. menus[m].Items.Select(Item)];
                pins[m] = GCHandle.Alloc(items[m], GCHandleType.Pinned);
                native[m] = new() { title = Marshal.StringToCoTaskMemUTF8(menus[m].Title), items = (KuiMenuItem*)pins[m].AddrOfPinnedObject(), count = (nuint)items[m].Length };
            }
            fixed (KuiMenu* p = native) return Made(KuiNative.kui_menu_bar(p, (nuint)native.Length));
        }
        finally
        {
            for (var m = 0; m < native.Length; ++m)
            {
                Marshal.FreeCoTaskMem(native[m].title);
                if (items[m] is { } list) foreach (var item in list) Marshal.FreeCoTaskMem(item.label);
                if (pins[m].IsAllocated) pins[m].Free();
            }
        }
    }

    // -- the window's own bars
    /// <summary>The window's title bar, drawn by the interface in place of the system's: what moves the window, and its buttons.</summary>
    public static Widget TitleBar(string title, Widget? leading = null, Widget? trailing = null, float height = 34f, bool buttons = true)
    {
        IntPtr l = Lend(leading), t = IntPtr.Zero;
        try
        {
            t = Lend(trailing);
            return Made(KuiNative.kui_title_bar(title, l, t, height, KuiNative.Bool(buttons)));
        }
        finally { Return(l); Return(t); }
    }

    /// <summary>A bar along the foot for the last thing said, as an editor's.</summary>
    public static Widget StatusBar(string message, StatusLevel level = StatusLevel.Info, Widget? trailing = null, float height = 26f) =>
        WithChild(trailing, t => KuiNative.kui_status_bar(message, (uint)level, t, height));

    // -- layout
    /// <summary>A ScrollView that says where it is — and how far it can go — and goes to <paramref name="jumpTo"/> when <paramref name="jump"/> changes.</summary>
    public static Widget ScrollView(Widget child, Axis axis, Action<float, float>? onScrolled, float jumpTo = 0f, uint jump = 0) =>
        WithChild(child, c => KuiNative.kui_scroll_view_observed(c, (uint)axis, onScrolled is null ? default : Callbacks.Point(v => onScrolled(v.X, v.Y)), jumpTo, jump));
    /// <summary><paramref name="child"/> drawn, and hit, through <paramref name="transform"/> about the point of its box <paramref name="origin"/> names.</summary>
    public static Widget TransformBox(Transform transform, Widget child, Alignment? origin = null) =>
        WithChild(child, c => KuiNative.kui_transform_box(transform.Native, (origin ?? Alignment.Center).Native, c));
    /// <summary>As wide as it may be, and as tall as that makes it at <paramref name="ratio"/> (width over height).</summary>
    public static Widget AspectRatio(float ratio, Widget child) => WithChild(child, c => KuiNative.kui_aspect_ratio(ratio, c));
    /// <summary><paramref name="child"/> made that share of the width, and of the height, it is allowed. A share of 0 leaves that way alone.</summary>
    public static Widget FractionallySizedBox(float widthShare, float heightShare, Widget child) =>
        WithChild(child, c => KuiNative.kui_fractionally_sized_box(widthShare, heightShare, c));
    /// <summary><paramref name="child"/> as wide, and as tall, as it is with all the room there is that way: a column as wide as its widest child.</summary>
    public static Widget Intrinsic(bool width, bool height, Widget child) =>
        WithChild(child, c => KuiNative.kui_intrinsic(KuiNative.Bool(width), KuiNative.Bool(height), c));
    /// <summary><paramref name="children"/> laid out by <paramref name="rule"/>.</summary>
    public static Widget CustomLayout(LayoutRule rule, IReadOnlyList<Widget?> children) =>
        WithChildren(children, (p, n) => KuiNative.kui_custom_layout(Callbacks.Layout(rule), (IntPtr*)p, n));

    /// <summary>Takes no room; while <paramref name="open"/>, <paramref name="popup"/> is shown over everything, under (or over) what this is in.</summary>
    public static Widget PopupAnchor(bool open, Widget popup, Action? onDismiss = null, Vector2 offset = default, bool below = true) =>
        WithChild(popup, p => KuiNative.kui_popup_anchor(KuiNative.Bool(open), p, Callbacks.Action(onDismiss), offset.Native(), KuiNative.Bool(below)));

    /// <summary><paramref name="child"/> in <paramref name="theme"/>, whatever the Ui's is.</summary>
    public static Widget Themed(Theme theme, Widget child)
    {
        var c = Lend(child);
        try
        {
            var t = theme.Native;
            var made = Made(KuiNative.kui_themed(&t, c));
            GC.KeepAlive(theme);
            return made;
        }
        finally { Return(c); }
    }

    /// <summary>
    /// A list down or across of which only the items in view (and a screen either side) exist, each as long as it
    /// likes: <paramref name="builder"/> makes item i when it comes into view.
    /// </summary>
    public static Widget LazyList(int count, Func<int, Widget> builder, LazyListOptions? options = null)
    {
        options ??= new();
        var o = new KuiLazyListOptions
        {
            count = (nuint)count, axis = (uint)options.Axis, item_extent = options.ItemExtent ?? 0f, estimated_extent = options.EstimatedExtent,
            gap = options.Gap, padding_start = options.PaddingStart, padding_end = options.PaddingEnd,
            builder = Callbacks.Items(builder),
            on_range = options.OnRange is null ? default : Callbacks.Range(options.OnRange),
            on_scrolled = Callbacks.Index(options.OnScrolled),
            jump_index = (nuint)Math.Max(options.JumpIndex, 0), jump_offset = options.JumpOffset, jump = options.Jump,
        };
        return Made(KuiNative.kui_lazy_list(&o));
    }
}

internal static unsafe partial class Callbacks
{
    public static KuiColorAction Colour(Action<Color>? a) => a is null ? default : new() { invoke = &InvokeColour, user = Hold(a), destroy = &Free };
    public static KuiStopsAction Stops(Action<IReadOnlyList<GradientStop>>? a) => a is null ? default : new() { invoke = &InvokeStops, user = Hold(a), destroy = &Free };
    public static KuiIndexAction Index(Action<int, float>? a) => a is null ? default : new() { invoke = &InvokeIndex, user = Hold(a), destroy = &Free };
    public static KuiLayoutRule Layout(LayoutRule rule) => new() { layout = &InvokeLayout, user = Hold(rule), destroy = &Free };

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void InvokeColour(float r, float g, float b, float alpha, void* user)
    {
        var a = Target<Action<Color>>(user);
        try { a(new Color(r, g, b, alpha)); } catch (Exception e) { Report(a.Target ?? a, a.Method.Name, e); }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void InvokeStops(float* stops, nuint count, void* user)
    {
        var a = Target<Action<IReadOnlyList<GradientStop>>>(user);
        try
        {
            var list = new GradientStop[(int)count];
            for (var i = 0; i < list.Length; ++i) list[i] = new(stops[i * 5], new Color(stops[i * 5 + 1], stops[i * 5 + 2], stops[i * 5 + 3], stops[i * 5 + 4]));
            a(list);
        }
        catch (Exception e) { Report(a.Target ?? a, a.Method.Name, e); }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void InvokeIndex(nuint index, float offset, void* user)
    {
        var a = Target<Action<int, float>>(user);
        try { a((int)index, offset); } catch (Exception e) { Report(a.Target ?? a, a.Method.Name, e); }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void InvokeLayout(IntPtr context, float minWidth, float maxWidth, float minHeight, float maxHeight, float* outWidth, float* outHeight, void* user)
    {
        var rule = Target<LayoutRule>(user);
        try
        {
            var size = rule(new LayoutContext(context), new BoxConstraints(minWidth, maxWidth, minHeight, maxHeight));
            *outWidth = size.X;
            *outHeight = size.Y;
        }
        catch (Exception e)
        {
            Report(rule.Target ?? rule, rule.Method.Name, e);
            *outWidth = 0f;
            *outHeight = 0f;
        }
    }
}
