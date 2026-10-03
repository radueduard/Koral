using System.Numerics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Koral.UI.Native;

namespace Koral.UI;

/// <summary>
/// kui::Widget: a description of part of the interface — cheap to make, and compared with the one before
/// it to decide what to build again. Made by the functions in <see cref="Widgets"/> (<c>using static
/// Koral.UI.Widgets;</c> gives <c>Row(...)</c>, <c>Text(...)</c>), or by deriving from
/// <see cref="StatelessWidget"/> or <see cref="StatefulWidget"/>.
/// </summary>
public abstract class Widget
{
    private string? _key;

    /// <summary>
    /// Widget::Key: among its siblings, matched to the one from the last build with the same key, wherever
    /// it moved — so a reordered list keeps each item's state.
    /// </summary>
    public Widget Key(string key)
    {
        _key = key;
        return this;
    }

    /// <summary>A native handle for it that the caller owns (and releases).</summary>
    internal IntPtr NewHandle()
    {
        var handle = MakeHandle();
        if (_key is not null && handle != IntPtr.Zero) KuiNative.kui_widget_set_key(handle, _key);
        return handle;
    }

    private protected abstract IntPtr MakeHandle();
}

/// <summary>A widget the native library made: one of the building blocks.</summary>
internal sealed class NativeWidget : Widget
{
    private readonly IntPtr _native;
    internal NativeWidget(IntPtr native) => _native = KuiNative.Check(native);
    ~NativeWidget() => KuiNative.kui_widget_release(_native);
    private protected override IntPtr MakeHandle() => KuiNative.kui_widget_retain(_native);
}

/// <summary>
/// kui::StatelessWidget: a widget made of other widgets, with no state of its own — <see cref="Build"/>
/// describes it from what it was given.
/// </summary>
public abstract unsafe class StatelessWidget : Widget
{
    public abstract Widget Build();

    private protected override IntPtr MakeHandle()
    {
        var callbacks = new KuiStatelessCallbacks
        {
            build = &OnBuild,
            user = (void*)GCHandle.ToIntPtr(GCHandle.Alloc(this)),
            destroy = &Callbacks.Free,
            type = (void*)GetType().TypeHandle.Value,
        };
        return KuiNative.Check(KuiNative.kui_stateless_widget(&callbacks));
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static IntPtr OnBuild(void* user)
    {
        var widget = (StatelessWidget)GCHandle.FromIntPtr((IntPtr)user).Target!;
        try { return widget.Build()?.NewHandle() ?? IntPtr.Zero; }
        catch (Exception e) { Callbacks.Report(widget, nameof(Build), e); return IntPtr.Zero; }
    }
}

/// <summary>
/// kui::StatefulWidget: a widget whose members are its state. <see cref="Build"/> describes it from them;
/// <see cref="SetState"/> says they changed.
/// </summary>
/// <remarks>
/// The instance first placed somewhere in the tree is the one kept there. When its parent builds again,
/// the new instance it makes is offered to <see cref="DidUpdateWidget"/>, where the widget copies the
/// configuration it takes from its parent. A hot reload builds every widget again, keeping state.
/// <code>
/// class Counter : StatefulWidget
/// {
///     int n;
///     public override Widget Build() => Column([
///         Text($"{n}"),
///         Button("+", () => SetState(() => ++n)),
///     ]);
/// }
/// </code>
/// </remarks>
public abstract unsafe class StatefulWidget : Widget
{
    private IntPtr _state;

    public abstract Widget Build();
    /// <summary>Once, after it is first placed in the tree.</summary>
    public virtual void InitState() { }
    /// <summary>Once, before it leaves the tree.</summary>
    public virtual void Dispose() { }
    /// <summary>The parent built again, describing it as <paramref name="newer"/> (of the same type). Copy what configuration you accept.</summary>
    public virtual void DidUpdateWidget(StatefulWidget newer) { }

    /// <summary>Applies <paramref name="change"/> and builds the widget again before the next frame is drawn.</summary>
    protected void SetState(Action? change = null)
    {
        change?.Invoke();
        if (_state != IntPtr.Zero) KuiNative.kui_state_set_state(_state);
    }

    /// <summary>Calls <paramref name="tick"/> each frame with the seconds since the last, for as long as it returns true.</summary>
    protected void Animate(Func<float, bool> tick)
    {
        if (_state != IntPtr.Zero) KuiNative.kui_state_animate(_state, Callbacks.Ticker(tick));
    }

    protected bool Mounted => _state != IntPtr.Zero;

    private protected override IntPtr MakeHandle()
    {
        var callbacks = new KuiStatefulCallbacks
        {
            build = &OnBuild,
            init_state = &OnInitState,
            dispose = &OnDispose,
            did_update_widget = &OnDidUpdateWidget,
            user = (void*)GCHandle.ToIntPtr(GCHandle.Alloc(this)),
            destroy = &Callbacks.Free,
            type = (void*)GetType().TypeHandle.Value,
        };
        return KuiNative.Check(KuiNative.kui_stateful_widget(&callbacks));
    }

    private static StatefulWidget Of(void* user) => (StatefulWidget)GCHandle.FromIntPtr((IntPtr)user).Target!;

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static IntPtr OnBuild(IntPtr state, void* user)
    {
        var widget = Of(user);
        widget._state = state;
        try { return widget.Build()?.NewHandle() ?? IntPtr.Zero; }
        catch (Exception e) { Callbacks.Report(widget, nameof(Build), e); return IntPtr.Zero; }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void OnInitState(IntPtr state, void* user)
    {
        var widget = Of(user);
        widget._state = state;
        try { widget.InitState(); }
        catch (Exception e) { Callbacks.Report(widget, nameof(InitState), e); }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void OnDispose(IntPtr state, void* user)
    {
        var widget = Of(user);
        try { widget.Dispose(); }
        catch (Exception e) { Callbacks.Report(widget, nameof(Dispose), e); }
        widget._state = IntPtr.Zero;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void OnDidUpdateWidget(IntPtr state, void* user, void* newer)
    {
        var widget = Of(user);
        var other = Of(newer);
        if (ReferenceEquals(widget, other)) return;
        try { widget.DidUpdateWidget(other); }
        catch (Exception e) { Callbacks.Report(widget, nameof(DidUpdateWidget), e); }
    }
}

// ---- options -----------------------------------------------------------------------------------------------

/// <summary>kui::FlexOptions. Chainable, as in C++: <c>new FlexOptions().SetGap(8).SetMainAxisAlignment(...)</c>.</summary>
public sealed record FlexOptions
{
    public MainAxisAlignment MainAxisAlignment { get; set; } = MainAxisAlignment.eStart;
    public CrossAxisAlignment CrossAxisAlignment { get; set; } = CrossAxisAlignment.eCenter;
    /// <summary>eMin: as long as the children (the default: everything fits its contents). eMax: as long as allowed.</summary>
    public MainAxisSize MainAxisSize { get; set; } = MainAxisSize.eMin;
    public float Gap { get; set; }

    public FlexOptions SetMainAxisAlignment(MainAxisAlignment value) { MainAxisAlignment = value; return this; }
    public FlexOptions SetCrossAxisAlignment(CrossAxisAlignment value) { CrossAxisAlignment = value; return this; }
    public FlexOptions SetMainAxisSize(MainAxisSize value) { MainAxisSize = value; return this; }
    public FlexOptions SetGap(float value) { Gap = value; return this; }

    internal KuiFlexOptions Native => new() { main_axis_alignment = (uint)MainAxisAlignment, cross_axis_alignment = (uint)CrossAxisAlignment, main_axis_size = (uint)MainAxisSize, gap = Gap };
}

/// <summary>kui::Decoration: how a box looks behind its content.</summary>
public sealed record Decoration
{
    public Color Color { get; set; } = Color.Transparent;
    public Gradient? Gradient { get; set; }
    public float BorderWidth { get; set; }
    public Color BorderColor { get; set; } = Color.Transparent;
    public Radii Radius { get; set; }
    public Color ShadowColor { get; set; } = Color.Transparent;
    public float ShadowBlur { get; set; }
    public Vector2 ShadowOffset { get; set; }

    public Decoration SetColor(Color value) { Color = value; return this; }
    public Decoration SetGradient(Gradient? value) { Gradient = value; return this; }
    public Decoration SetBorder(float width, Color color) { BorderWidth = width; BorderColor = color; return this; }
    public Decoration SetRadius(Radii value) { Radius = value; return this; }
    public Decoration SetShadow(Color color, float blur, Vector2 offset = default) { ShadowColor = color; ShadowBlur = blur; ShadowOffset = offset; return this; }

    internal KuiDecoration Native => new()
    {
        color = Color.Native, gradient = Gradient?.Native ?? IntPtr.Zero, border_width = BorderWidth, border_color = BorderColor.Native,
        radius = Radius.Native, shadow_color = ShadowColor.Native, shadow_blur = ShadowBlur, shadow_offset = ShadowOffset.Native(),
    };
}

/// <summary>kui::ContainerOptions: padding, a decoration, a size and an alignment.</summary>
public sealed record ContainerOptions
{
    /// <summary>Negative: sized by the child (just its padding, without one).</summary>
    public float Width { get; set; } = -1;
    public float Height { get; set; } = -1;
    public EdgeInsets Padding { get; set; }
    public EdgeInsets Margin { get; set; }
    public Decoration Decoration { get; set; } = new();
    public Alignment? Alignment { get; set; }

    public ContainerOptions SetWidth(float value) { Width = value; return this; }
    public ContainerOptions SetHeight(float value) { Height = value; return this; }
    public ContainerOptions SetSize(float width, float height) { Width = width; Height = height; return this; }
    public ContainerOptions SetPadding(EdgeInsets value) { Padding = value; return this; }
    public ContainerOptions SetMargin(EdgeInsets value) { Margin = value; return this; }
    public ContainerOptions SetDecoration(Decoration value) { Decoration = value; return this; }
    public ContainerOptions SetColor(Color value) { Decoration = Decoration with { Color = value }; return this; }
    public ContainerOptions SetRadius(Radii value) { Decoration = Decoration with { Radius = value }; return this; }
    public ContainerOptions SetAlignment(Alignment value) { Alignment = value; return this; }

    internal KuiContainerOptions Native => new()
    {
        width = Width, height = Height, padding = Padding.Native, margin = Margin.Native, decoration = Decoration.Native,
        has_alignment = KuiNative.Bool(Alignment.HasValue), alignment = (Alignment ?? default).Native,
    };
}

/// <summary>kui::PositionedOptions: where a child of a Stack goes. Unset sides are left to its alignment.</summary>
public sealed record PositionedOptions
{
    public float? Left { get; set; }
    public float? Top { get; set; }
    public float? Right { get; set; }
    public float? Bottom { get; set; }
    public float? Width { get; set; }
    public float? Height { get; set; }

    public PositionedOptions SetLeft(float v) { Left = v; return this; }
    public PositionedOptions SetTop(float v) { Top = v; return this; }
    public PositionedOptions SetRight(float v) { Right = v; return this; }
    public PositionedOptions SetBottom(float v) { Bottom = v; return this; }
    public PositionedOptions SetWidth(float v) { Width = v; return this; }
    public PositionedOptions SetHeight(float v) { Height = v; return this; }

    internal KuiPositionedOptions Native => new()
    {
        left = Left ?? float.NaN, top = Top ?? float.NaN, right = Right ?? float.NaN, bottom = Bottom ?? float.NaN,
        width = Width ?? float.NaN, height = Height ?? float.NaN,
    };
}

/// <summary>kui::GestureOptions: pointer events on a child.</summary>
public sealed record GestureOptions
{
    public Action? OnTap { get; set; }
    public Action<Vector2>? OnTapDown { get; set; }
    public Action? OnTapUp { get; set; }
    public Action<Vector2>? OnPanStart { get; set; }
    /// <summary>The movement, and where the pointer is.</summary>
    public Action<Vector2, Vector2>? OnPanUpdate { get; set; }
    public Action? OnPanEnd { get; set; }
    public Action? OnEnter { get; set; }
    public Action? OnExit { get; set; }
    public Action<Vector2>? OnHover { get; set; }
    /// <summary>Return true when used.</summary>
    public Func<Vector2, bool>? OnScroll { get; set; }
    /// <summary>Hit even where nothing it holds is drawn.</summary>
    public bool Opaque { get; set; } = true;

    public GestureOptions SetOnTap(Action value) { OnTap = value; return this; }
    public GestureOptions SetOnTapDown(Action<Vector2> value) { OnTapDown = value; return this; }
    public GestureOptions SetOnTapUp(Action value) { OnTapUp = value; return this; }
    public GestureOptions SetOnPanStart(Action<Vector2> value) { OnPanStart = value; return this; }
    public GestureOptions SetOnPanUpdate(Action<Vector2, Vector2> value) { OnPanUpdate = value; return this; }
    public GestureOptions SetOnPanEnd(Action value) { OnPanEnd = value; return this; }
    public GestureOptions SetOnEnter(Action value) { OnEnter = value; return this; }
    public GestureOptions SetOnExit(Action value) { OnExit = value; return this; }
    public GestureOptions SetOnHover(Action<Vector2> value) { OnHover = value; return this; }
    public GestureOptions SetOnScroll(Func<Vector2, bool> value) { OnScroll = value; return this; }
    public GestureOptions SetOpaque(bool value) { Opaque = value; return this; }

    internal KuiGestureOptions Native => new()
    {
        on_tap = Callbacks.Action(OnTap), on_tap_down = Callbacks.Point(OnTapDown), on_tap_up = Callbacks.Action(OnTapUp),
        on_pan_start = Callbacks.Point(OnPanStart), on_pan_update = Callbacks.Pan(OnPanUpdate), on_pan_end = Callbacks.Action(OnPanEnd),
        on_enter = Callbacks.Action(OnEnter), on_exit = Callbacks.Action(OnExit), on_hover = Callbacks.Point(OnHover),
        on_scroll = Callbacks.Scroll(OnScroll), opaque = KuiNative.Bool(Opaque),
    };
}

/// <summary>kui::ButtonOptions.</summary>
public sealed record ButtonOptions
{
    public ButtonStyle Style { get; set; } = ButtonStyle.ePrimary;
    public float? Width { get; set; }
    /// <summary>Around the child; the style's own when not given (none for ePlain).</summary>
    public EdgeInsets? Padding { get; set; }
    public bool Enabled { get; set; } = true;

    public ButtonOptions SetStyle(ButtonStyle value) { Style = value; return this; }
    public ButtonOptions SetWidth(float value) { Width = value; return this; }
    public ButtonOptions SetPadding(EdgeInsets value) { Padding = value; return this; }
    public ButtonOptions SetEnabled(bool value) { Enabled = value; return this; }

    internal KuiButtonOptions Native => new()
    {
        style = (uint)Style, width = Width ?? float.NaN, has_padding = KuiNative.Bool(Padding.HasValue),
        padding = (Padding ?? default).Native, enabled = KuiNative.Bool(Enabled),
    };
}

/// <summary>kui::TextFieldOptions: one line of editable text.</summary>
public sealed record TextFieldOptions
{
    /// <summary>What it starts with.</summary>
    public string Text { get; set; } = "";
    public string Placeholder { get; set; } = "";
    public Action<string>? OnChanged { get; set; }
    /// <summary>Enter.</summary>
    public Action<string>? OnSubmitted { get; set; }
    /// <summary>Negative: 200 units, or as wide as a stretching parent makes it.</summary>
    public float Width { get; set; } = -1;
    /// <summary>True: it always shows <see cref="Text"/> — what is typed reaches OnChanged, and shows once it
    /// comes back as Text. False: Text is only what it starts with.</summary>
    public bool Controlled { get; set; }

    public TextFieldOptions SetText(string value) { Text = value; return this; }
    public TextFieldOptions SetPlaceholder(string value) { Placeholder = value; return this; }
    public TextFieldOptions SetOnChanged(Action<string> value) { OnChanged = value; return this; }
    public TextFieldOptions SetOnSubmitted(Action<string> value) { OnSubmitted = value; return this; }
    public TextFieldOptions SetWidth(float value) { Width = value; return this; }
    public TextFieldOptions SetControlled(bool value) { Controlled = value; return this; }
}

// ---- the building blocks ----------------------------------------------------------------------------------

/// <summary>
/// The building blocks, as the free functions of kui:: are in C++. <c>using static Koral.UI.Widgets;</c>
/// and they read the same: <c>Column([Text("Hi"), Button("Go", Go)], new() { Gap = 8 })</c>.
/// </summary>
public static unsafe partial class Widgets
{
    internal static Widget Made(IntPtr native) => new NativeWidget(native);

    /// <summary>Calls <paramref name="make"/> with native handles for <paramref name="children"/>, letting them go after.</summary>
    private static Widget WithChildren(IReadOnlyList<Widget?> children, Func<IntPtr, nuint, IntPtr> make)
    {
        var handles = new IntPtr[children.Count];
        try
        {
            var n = 0;
            foreach (var child in children)
                if (child is not null) handles[n++] = child.NewHandle();
            fixed (IntPtr* p = handles) return Made(make((IntPtr)p, (nuint)n));
        }
        finally
        {
            foreach (var h in handles) if (h != IntPtr.Zero) KuiNative.kui_widget_release(h);
        }
    }

    internal static IntPtr Lend(Widget? child) => child?.NewHandle() ?? IntPtr.Zero;
    internal static void Return(IntPtr handle) { if (handle != IntPtr.Zero) KuiNative.kui_widget_release(handle); }

    private static IntPtr[] LendAll(IReadOnlyList<Widget?> children)
    {
        var handles = new List<IntPtr>(children.Count);
        try
        {
            foreach (var child in children) if (child is not null) handles.Add(child.NewHandle());
        }
        catch
        {
            ReturnAll([.. handles]);
            throw;
        }
        return [.. handles];
    }

    private static void ReturnAll(IntPtr[] handles) { foreach (var h in handles) Return(h); }

    private static Widget WithChild(Widget? child, Func<IntPtr, IntPtr> make)
    {
        var handle = child?.NewHandle() ?? IntPtr.Zero;
        try { return Made(make(handle)); }
        finally { if (handle != IntPtr.Zero) KuiNative.kui_widget_release(handle); }
    }

    /// <summary>Text, wrapped to the width it is given. Black text takes the theme's text colour.</summary>
    public static Widget Text(string text, TextStyle? style = null, TextAlign align = TextAlign.eStart, bool wrap = true)
    {
        var s = (style ?? new TextStyle()).Native;
        return Made(KuiNative.kui_text(text, &s, (uint)align, KuiNative.Bool(wrap)));
    }

    public static Widget Row(IReadOnlyList<Widget?> children, FlexOptions? options = null)
    {
        var handles = LendAll(children);
        try
        {
            var o = (options ?? new FlexOptions()).Native;
            fixed (IntPtr* p = handles) return Made(KuiNative.kui_row(p, (nuint)handles.Length, &o));
        }
        finally { ReturnAll(handles); }
    }

    public static Widget Column(IReadOnlyList<Widget?> children, FlexOptions? options = null)
    {
        var handles = LendAll(children);
        try
        {
            var o = (options ?? new FlexOptions()).Native;
            fixed (IntPtr* p = handles) return Made(KuiNative.kui_column(p, (nuint)handles.Length, &o));
        }
        finally { ReturnAll(handles); }
    }

    public static Widget Flex(Axis axis, IReadOnlyList<Widget?> children, FlexOptions? options = null)
    {
        var handles = LendAll(children);
        try
        {
            var o = (options ?? new FlexOptions()).Native;
            fixed (IntPtr* p = handles) return Made(KuiNative.kui_flex((uint)axis, p, (nuint)handles.Length, &o));
        }
        finally { ReturnAll(handles); }
    }

    /// <summary>In a Row or Column: a share of the space left over, filled.</summary>
    public static Widget Expanded(Widget child, float flex = 1f) => WithChild(child, c => KuiNative.kui_expanded(c, flex));
    /// <summary>In a Row or Column: up to a share of the space left over.</summary>
    public static Widget Flexible(Widget child, float flex = 1f) => WithChild(child, c => KuiNative.kui_flexible(c, flex));
    public static Widget Padding(EdgeInsets padding, Widget child) => WithChild(child, c => KuiNative.kui_padding(padding.Native, c));
    public static Widget Align(Alignment alignment, Widget child) => WithChild(child, c => KuiNative.kui_align(alignment.Native, c));
    public static Widget Center(Widget child) => WithChild(child, KuiNative.kui_center);
    /// <summary>A box of a fixed size (negative: whatever the child is), or empty space.</summary>
    public static Widget SizedBox(float width, float height, Widget? child = null) => WithChild(child, c => KuiNative.kui_sized_box(width, height, c));
    public static Widget ConstrainedBox(BoxConstraints constraints, Widget child) => WithChild(child, c => KuiNative.kui_constrained_box(constraints.Native, c));

    public static Widget Container(ContainerOptions options, Widget? child = null)
    {
        var c = Lend(child);
        try
        {
            var o = options.Native;
            var made = Made(KuiNative.kui_container(&o, c));
            GC.KeepAlive(options);
            return made;
        }
        finally { Return(c); }
    }

    public static Widget DecoratedBox(Decoration decoration, Widget? child = null)
    {
        var c = Lend(child);
        try
        {
            var d = decoration.Native;
            var made = Made(KuiNative.kui_decorated_box(&d, c));
            GC.KeepAlive(decoration);
            return made;
        }
        finally { Return(c); }
    }

    /// <summary>Children on top of each other, the first at the back.</summary>
    public static Widget Stack(IReadOnlyList<Widget?> children, Alignment? alignment = null) =>
        WithChildren(children, (p, n) => KuiNative.kui_stack((IntPtr*)p, n, (alignment ?? Alignment.TopLeft).Native));

    public static Widget Positioned(PositionedOptions options, Widget child)
    {
        var c = Lend(child);
        try
        {
            var o = options.Native;
            var made = Made(KuiNative.kui_positioned(&o, c));
            return made;
        }
        finally { Return(c); }
    }

    /// <summary>In a Stack: <paramref name="child"/> placed by its own alignment, still counting towards the Stack's size.</summary>
    public static Widget StackAlign(Alignment alignment, Widget child) => WithChild(child, c => KuiNative.kui_stack_align(alignment.Native, c));

    public static Widget ScrollView(Widget child, Axis axis = Axis.eVertical) => WithChild(child, c => KuiNative.kui_scroll_view(c, (uint)axis));

    /// <summary>A scrolling list whose items each keep a layer of their own.</summary>
    public static Widget ListView(IReadOnlyList<Widget?> children, Axis axis = Axis.eVertical, float gap = 0f) =>
        WithChildren(children, (p, n) => KuiNative.kui_list_view((IntPtr*)p, n, (uint)axis, gap));

    /// <summary>A list of <paramref name="count"/> items, of which only those near the view exist: <paramref name="builder"/> makes item i when it comes into view.</summary>
    public static Widget ListView(int count, float itemExtent, Func<int, Widget> builder) =>
        Made(KuiNative.kui_list_view_builder((nuint)count, itemExtent, Callbacks.Items(builder)));

    /// <summary>As above, also telling <paramref name="onRange"/> which items [first, last) the list keeps, whenever that changes.</summary>
    public static Widget ListView(int count, float itemExtent, Func<int, Widget> builder, Action<int, int> onRange) =>
        Made(KuiNative.kui_list_view_builder_with_range((nuint)count, itemExtent, Callbacks.Items(builder), Callbacks.Range(onRange)));

    public static Widget GestureDetector(GestureOptions options, Widget? child = null)
    {
        var c = Lend(child);
        try
        {
            var o = options.Native;
            var made = Made(KuiNative.kui_gesture_detector(&o, c));
            return made;
        }
        finally { Return(c); }
    }

    /// <summary>Draws with a canvas, in a box of <paramref name="size"/> (negative: the child's size, or nothing without one — unless the parent sets it).</summary>
    public static Widget CustomPaint(Action<Canvas, Vector2> painter, Vector2? size = null, Widget? child = null)
    {
        var s = size ?? new Vector2(-1, -1);
        return WithChild(child, c => KuiNative.kui_custom_paint(Callbacks.Painter(painter), s.Native(), c));
    }

    /// <summary>An element shader filling the box, given <paramref name="parameters"/> (the shader's struct, std430).</summary>
    public static Widget ShaderBox<T>(ElementShader shader, in T parameters, Radii radius = default, Widget? child = null) where T : unmanaged
    {
        var c = Lend(child);
        try
        {
            var copy = parameters;
            var made = Made(KuiNative.kui_shader_box(shader.Native, &copy, (nuint)sizeof(T), radius.Native, c));
            return made;
        }
        finally { Return(c); }
    }

    public static Widget ShaderBox(ElementShader shader, Radii radius = default, Widget? child = null) =>
        WithChild(child, c => KuiNative.kui_shader_box(shader.Native, null, 0, radius.Native, c));

    public static Widget Image(Koral.Image image, ImageFit fit = ImageFit.eContain, Vector2? size = null) =>
        Made(KuiNative.kui_image(Resource.HandleOf(image), (uint)fit, (size ?? new Vector2(-1, -1)).Native()));

    /// <summary>Keeps the child in a layer of its own: repainting it repaints nothing around it.</summary>
    public static Widget RepaintBoundary(Widget child) => WithChild(child, KuiNative.kui_repaint_boundary);
    /// <summary>The child, faded. Changing it re-records nothing.</summary>
    public static Widget Opacity(float opacity, Widget child) => WithChild(child, c => KuiNative.kui_opacity(opacity, c));
    /// <summary>The child, cut to its box with rounded corners.</summary>
    public static Widget ClipRRect(Radii radius, Widget child) => WithChild(child, c => KuiNative.kui_clip_rrect(radius.Native, c));
    /// <summary>The child, moved by <paramref name="offset"/> where it paints and is hit; layout unchanged.</summary>
    public static Widget Translate(Vector2 offset, Widget child) => WithChild(child, c => KuiNative.kui_translate(offset.Native(), c));

    // -- controls
    /// <summary>Any widget, made a button: <paramref name="child"/> in a container that calls <paramref name="onPressed"/> when clicked.</summary>
    public static Widget Button(Widget child, Action? onPressed, ButtonOptions? options = null)
    {
        var c = Lend(child);
        try
        {
            var o = (options ?? new ButtonOptions()).Native;
            return Made(KuiNative.kui_button_with_child(c, Callbacks.Action(onPressed), &o));
        }
        finally { Return(c); }
    }

    /// <summary>A button holding <paramref name="label"/>, in the style's text colour.</summary>
    public static Widget Button(string label, Action? onPressed, ButtonOptions? options = null)
    {
        var o = (options ?? new ButtonOptions()).Native;
        return Made(KuiNative.kui_button(label, Callbacks.Action(onPressed), &o));
    }

    /// <summary>A box with a tick, and a label beside it.</summary>
    public static Widget Checkbox(bool value, Action<bool>? onChanged, string label = "") =>
        Made(KuiNative.kui_checkbox(KuiNative.Bool(value), Callbacks.Bool(onChanged), label));
    public static Widget Switch(bool value, Action<bool>? onChanged) => Made(KuiNative.kui_switch(KuiNative.Bool(value), Callbacks.Bool(onChanged)));
    public static Widget Slider(float value, Action<float>? onChanged, float min = 0f, float max = 1f) =>
        Made(KuiNative.kui_slider(value, Callbacks.Float(onChanged), min, max));
    public static Widget ProgressBar(float value) => Made(KuiNative.kui_progress_bar(value));

    /// <summary>One line of editable text.</summary>
    public static Widget TextField(TextFieldOptions options)
    {
        var text = KoralUtf8(options.Text);
        var placeholder = KoralUtf8(options.Placeholder);
        try
        {
            var o = new KuiTextFieldOptions
            {
                text = text, placeholder = placeholder, on_changed = Callbacks.Text(options.OnChanged),
                on_submitted = Callbacks.Text(options.OnSubmitted), width = options.Width,
                controlled = KuiNative.Bool(options.Controlled),
            };
            return Made(KuiNative.kui_text_field(&o));
        }
        finally
        {
            Marshal.FreeCoTaskMem(text);
            Marshal.FreeCoTaskMem(placeholder);
        }
    }

    private static IntPtr KoralUtf8(string text) => Marshal.StringToCoTaskMemUTF8(text);
}

// ---- callbacks into C# ---------------------------------------------------------------------------------------

/// <summary>C# delegates as the {invoke, user, destroy} triples koralUI_c.h takes: a GCHandle the native side frees.</summary>
internal static unsafe partial class Callbacks
{
    internal static void* Hold(object target) => (void*)GCHandle.ToIntPtr(GCHandle.Alloc(target));
    internal static T Target<T>(void* user) => (T)GCHandle.FromIntPtr((IntPtr)user).Target!;

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    internal static void Free(void* user) => GCHandle.FromIntPtr((IntPtr)user).Free();

    internal static void Report(object where, string what, Exception e) => Log.Error($"[{where.GetType().Name}] {what} threw {e}");

    public static KuiAction Action(Action? a) => a is null ? default : new() { invoke = &InvokeAction, user = Hold(a), destroy = &Free };
    public static KuiBoolAction Bool(Action<bool>? a) => a is null ? default : new() { invoke = &InvokeBool, user = Hold(a), destroy = &Free };
    public static KuiFloatAction Float(Action<float>? a) => a is null ? default : new() { invoke = &InvokeFloat, user = Hold(a), destroy = &Free };
    public static KuiPointAction Point(Action<Vector2>? a) => a is null ? default : new() { invoke = &InvokePoint, user = Hold(a), destroy = &Free };
    public static KuiPanAction Pan(Action<Vector2, Vector2>? a) => a is null ? default : new() { invoke = &InvokePan, user = Hold(a), destroy = &Free };
    public static KuiScrollAction Scroll(Func<Vector2, bool>? a) => a is null ? default : new() { invoke = &InvokeScroll, user = Hold(a), destroy = &Free };
    public static KuiTextAction Text(Action<string>? a) => a is null ? default : new() { invoke = &InvokeText, user = Hold(a), destroy = &Free };
    public static KuiTicker Ticker(Func<float, bool> a) => new() { invoke = &InvokeTicker, user = Hold(a), destroy = &Free };
    public static KuiPainter Painter(Action<Canvas, Vector2> a) => new() { paint = &InvokePainter, user = Hold(a), destroy = &Free };
    public static KuiItemBuilder Items(Func<int, Widget> a) => new() { build = &InvokeItems, user = Hold(a), destroy = &Free };
    public static KuiRangeAction Range(Action<int, int> a) => new() { invoke = &InvokeRange, user = Hold(a), destroy = &Free };

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void InvokeRange(nuint first, nuint last, void* user)
    {
        var a = Target<Action<int, int>>(user);
        try { a((int)first, (int)last); } catch (Exception e) { Report(a.Target ?? a, a.Method.Name, e); }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void InvokeAction(void* user)
    {
        var a = Target<Action>(user);
        try { a(); } catch (Exception e) { Report(a.Target ?? a, a.Method.Name, e); }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void InvokeBool(byte value, void* user)
    {
        var a = Target<Action<bool>>(user);
        try { a(value != 0); } catch (Exception e) { Report(a.Target ?? a, a.Method.Name, e); }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void InvokeFloat(float value, void* user)
    {
        var a = Target<Action<float>>(user);
        try { a(value); } catch (Exception e) { Report(a.Target ?? a, a.Method.Name, e); }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void InvokePoint(float x, float y, void* user)
    {
        var a = Target<Action<Vector2>>(user);
        try { a(new Vector2(x, y)); } catch (Exception e) { Report(a.Target ?? a, a.Method.Name, e); }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void InvokePan(float dx, float dy, float x, float y, void* user)
    {
        var a = Target<Action<Vector2, Vector2>>(user);
        try { a(new Vector2(dx, dy), new Vector2(x, y)); } catch (Exception e) { Report(a.Target ?? a, a.Method.Name, e); }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static byte InvokeScroll(float dx, float dy, void* user)
    {
        var a = Target<Func<Vector2, bool>>(user);
        try { return a(new Vector2(dx, dy)) ? (byte)1 : (byte)0; }
        catch (Exception e) { Report(a.Target ?? a, a.Method.Name, e); return 0; }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void InvokeText(byte* text, void* user)
    {
        var a = Target<Action<string>>(user);
        try { a(Marshal.PtrToStringUTF8((IntPtr)text) ?? ""); } catch (Exception e) { Report(a.Target ?? a, a.Method.Name, e); }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static byte InvokeTicker(float dt, void* user)
    {
        var a = Target<Func<float, bool>>(user);
        try { return a(dt) ? (byte)1 : (byte)0; }
        catch (Exception e) { Report(a.Target ?? a, a.Method.Name, e); return 0; }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void InvokePainter(IntPtr canvas, float width, float height, void* user)
    {
        var a = Target<Action<Canvas, Vector2>>(user);
        var borrowed = new Canvas(canvas);
        try { a(borrowed, new Vector2(width, height)); }
        catch (Exception e) { Report(a.Target ?? a, a.Method.Name, e); }
        finally { borrowed.Expire(); }   // lent for the call only
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static IntPtr InvokeItems(nuint index, void* user)
    {
        var a = Target<Func<int, Widget>>(user);
        try { return a((int)index)?.NewHandle() ?? IntPtr.Zero; }
        catch (Exception e) { Report(a.Target ?? a, a.Method.Name, e); return IntPtr.Zero; }
    }
}
