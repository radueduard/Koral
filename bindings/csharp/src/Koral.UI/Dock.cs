using System.Numerics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Koral.UI.Native;

namespace Koral.UI;

// ---- drag and drop -----------------------------------------------------------------------------------------

/// <summary>kui::DragData: what a drag carries — its kind, which targets go by, and the thing itself.</summary>
public sealed record DragData(string Type, object? Payload = null)
{
    /// <summary>The payload as a <typeparamref name="T"/>, or default when it is something else.</summary>
    public T? As<T>() => Payload is T value ? value : default;
}

/// <summary>kui::DraggableOptions.</summary>
public sealed record DraggableOptions
{
    /// <summary>What follows the pointer; a ghost of the child's size when null.</summary>
    public Widget? Feedback { get; set; }
    public Action? OnDragStart { get; set; }
    /// <summary>Whether a target took it.</summary>
    public Action<bool>? OnDragEnd { get; set; }
    public bool Enabled { get; set; } = true;

    public DraggableOptions SetFeedback(Widget value) { Feedback = value; return this; }
    public DraggableOptions SetEnabled(bool value) { Enabled = value; return this; }
    public DraggableOptions SetOnDragStart(Action value) { OnDragStart = value; return this; }
    public DraggableOptions SetOnDragEnd(Action<bool> value) { OnDragEnd = value; return this; }
}

/// <summary>kui::DropTargetOptions.</summary>
public sealed record DropTargetOptions
{
    /// <summary>The only kind of drag it takes; null: any.</summary>
    public string? AcceptsType { get; set; }
    public Action<DragData, Vector2>? OnDrop { get; set; }
    /// <summary>An accepted drag came over it: show it.</summary>
    public Action<DragData>? OnEnter { get; set; }
    /// <summary>It left, was dropped, or was cancelled.</summary>
    public Action? OnLeave { get; set; }
    public Action<DragData, Vector2>? OnMove { get; set; }

    public DropTargetOptions SetAcceptsType(string value) { AcceptsType = value; return this; }
    public DropTargetOptions SetOnDrop(Action<DragData, Vector2> value) { OnDrop = value; return this; }
    public DropTargetOptions SetOnEnter(Action<DragData> value) { OnEnter = value; return this; }
    public DropTargetOptions SetOnLeave(Action value) { OnLeave = value; return this; }
    public DropTargetOptions SetOnMove(Action<DragData, Vector2> value) { OnMove = value; return this; }
}

// ---- docking -------------------------------------------------------------------------------------------------

/// <summary>kui::DockPanel: what one tab of a dock space shows.</summary>
/// <remarks>Id is what the layout knows it by: the same from build to build.</remarks>
public sealed record DockPanel(string Id, string Title, Widget Content, bool Closable = true);

/// <summary>kui::DockOptions.</summary>
public sealed record DockOptions
{
    /// <summary>Whether a panel dragged out of the window gets an OS window of its own.</summary>
    public bool MultiViewport { get; set; } = true;
    /// <summary>A panel's close button was clicked.</summary>
    public Action<string>? OnClosed { get; set; }
    /// <summary>The arrangement changed: when to save it.</summary>
    public Action? OnChanged { get; set; }

    public DockOptions SetMultiViewport(bool value) { MultiViewport = value; return this; }
    public DockOptions SetOnClosed(Action<string> value) { OnClosed = value; return this; }
    public DockOptions SetOnChanged(Action value) { OnChanged = value; return this; }
}

/// <summary>
/// kui::DockLayout: how a dock space is arranged — which panels are tabbed together, how the space is split,
/// what floats and what is closed. Make one, keep it (a field of the scene or of a stateful widget), and give
/// it to <see cref="Widgets.DockSpace"/> each build; dragging changes it, and Save/Load carry it between runs.
/// </summary>
/// <example><code>
/// readonly DockLayout _layout = new DockLayout()
///     .Dock("scene")
///     .Dock("inspector", DockSide.eRight, "scene", 0.25f)
///     .Dock("log", DockSide.eBottom, "scene", 0.3f);
/// </code></example>
public sealed unsafe class DockLayout
{
    internal readonly IntPtr Native = KuiNative.Check(KuiNative.kui_dock_layout_new());
    ~DockLayout() => KuiNative.kui_dock_layout_release(Native);

    /// <summary>Puts <paramref name="panel"/> on <paramref name="side"/> of <paramref name="relativeTo"/> (of the whole space when null); eCenter makes it a tab of that group.</summary>
    public DockLayout Dock(string panel, DockSide side = DockSide.eCenter, string? relativeTo = null, float fraction = 0.25f)
    {
        KuiNative.kui_dock_layout_dock(Native, panel, (uint)side, relativeTo, fraction);
        return this;
    }
    /// <summary>Floats <paramref name="panel"/> over the dock space.</summary>
    public DockLayout Float(string panel, Rect rect) { KuiNative.kui_dock_layout_float(Native, panel, rect.Native); return this; }
    /// <summary>Gives <paramref name="panel"/> a window of its own, where the dock space can open windows.</summary>
    public DockLayout PopOut(string panel, Vector2? size = null) { KuiNative.kui_dock_layout_pop_out(Native, panel, (size ?? new Vector2(480, 360)).Native()); return this; }
    public void Close(string panel) => KuiNative.kui_dock_layout_close(Native, panel);
    public void Open(string panel) => KuiNative.kui_dock_layout_open(Native, panel);
    public void Activate(string panel) => KuiNative.kui_dock_layout_activate(Native, panel);
    public bool IsOpen(string panel) => KuiNative.kui_dock_layout_is_open(Native, panel) != 0;
    public bool IsFloating(string panel) => KuiNative.kui_dock_layout_is_floating(Native, panel) != 0;
    /// <summary>The arrangement as text, for a settings file.</summary>
    public string Save() => Marshal.PtrToStringUTF8((IntPtr)KuiNative.kui_dock_layout_save(Native)) ?? "";
    /// <summary>Takes an arrangement Save gave. False (and unchanged) when it is not one.</summary>
    public bool Load(string text) => KuiNative.kui_dock_layout_load(Native, text) != 0;
}

public static unsafe partial class Widgets
{
    /// <summary>The child, which the pointer goes through as if it were not there.</summary>
    public static Widget IgnorePointer(Widget child)
    {
        var c = Lend(child);
        try { return Made(KuiNative.kui_ignore_pointer(c)); } finally { Return(c); }
    }

    /// <summary>
    /// The child, which can be picked up and dragged: carrying <paramref name="data"/> until it is dropped on a
    /// <see cref="DropTarget"/> that accepts it, or let go anywhere else.
    /// </summary>
    public static Widget Draggable(DragData data, Widget child, DraggableOptions? options = null)
    {
        var c = Lend(child);
        var feedback = Lend(options?.Feedback);
        var type = Marshal.StringToCoTaskMemUTF8(data.Type);
        var text = Marshal.StringToCoTaskMemUTF8(data.Payload as string ?? "");
        try
        {
            var d = new KuiDragData { type = type, text = text, payload = Callbacks.Hold(data), destroy = &Callbacks.Free };
            var o = new KuiDraggableOptions
            {
                feedback = feedback,
                on_drag_start = Callbacks.Action(options?.OnDragStart),
                on_drag_end = Callbacks.Bool(options?.OnDragEnd),
                disabled = KuiNative.Bool(!(options?.Enabled ?? true)),
            };
            return Made(KuiNative.kui_draggable(&d, c, &o));
        }
        finally
        {
            Marshal.FreeCoTaskMem(type);
            Marshal.FreeCoTaskMem(text);
            Return(c);
            Return(feedback);
        }
    }

    /// <summary>The child, as somewhere drags can be dropped. Of nested targets, the deepest that accepts gets it.</summary>
    public static Widget DropTarget(DropTargetOptions options, Widget child)
    {
        var c = Lend(child);
        var type = options.AcceptsType is null ? IntPtr.Zero : Marshal.StringToCoTaskMemUTF8(options.AcceptsType);
        try
        {
            var enter = options.OnEnter;
            var o = new KuiDropTargetOptions
            {
                accepts_type = type,
                on_drop = Callbacks.Drop(options.OnDrop),
                on_enter = Callbacks.Drop(enter is null ? null : (d, _) => enter(d)),
                on_leave = Callbacks.Action(options.OnLeave),
                on_move = Callbacks.Drop(options.OnMove),
            };
            return Made(KuiNative.kui_drop_target(&o, c));
        }
        finally
        {
            if (type != IntPtr.Zero) Marshal.FreeCoTaskMem(type);
            Return(c);
        }
    }

    /// <summary>
    /// A space of docked panels, filling what it is given: tabs dragged between groups, onto edges (a split),
    /// into the middle (a float) and out of the window (a window of their own). A panel keeps its state wherever it goes.
    /// </summary>
    public static Widget DockSpace(DockLayout layout, IReadOnlyList<DockPanel> panels, DockOptions? options = null)
    {
        var native = new KuiDockPanel[panels.Count];
        try
        {
            for (var i = 0; i < native.Length; ++i)
                native[i] = new KuiDockPanel
                {
                    id = Marshal.StringToCoTaskMemUTF8(panels[i].Id),
                    title = Marshal.StringToCoTaskMemUTF8(panels[i].Title),
                    content = Lend(panels[i].Content),
                    @fixed = KuiNative.Bool(!panels[i].Closable),
                };
            var o = new KuiDockOptions
            {
                single_viewport = KuiNative.Bool(!(options?.MultiViewport ?? true)),
                on_closed = Callbacks.Text(options?.OnClosed),
                on_changed = Callbacks.Action(options?.OnChanged),
            };
            fixed (KuiDockPanel* p = native) return Made(KuiNative.kui_dock_space(layout.Native, p, (nuint)native.Length, &o));
        }
        finally
        {
            foreach (var p in native)
            {
                Marshal.FreeCoTaskMem(p.id);
                Marshal.FreeCoTaskMem(p.title);
                Return(p.content);
            }
            GC.KeepAlive(layout);
        }
    }
}

internal static unsafe partial class Callbacks
{
    public static KuiDropAction Drop(Action<DragData, Vector2>? a) => a is null ? default : new() { invoke = &InvokeDrop, user = Hold(a), destroy = &Free };

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void InvokeDrop(byte* type, byte* text, void* payload, float x, float y, void* user)
    {
        var a = Target<Action<DragData, Vector2>>(user);
        try
        {
            // A drag begun in C#: the DragData itself. One from elsewhere: its kind and text.
            var data = payload != null && GCHandle.FromIntPtr((IntPtr)payload).Target is DragData own
                ? own
                : new DragData(Marshal.PtrToStringUTF8((IntPtr)type) ?? "", Marshal.PtrToStringUTF8((IntPtr)text));
            a(data, new Vector2(x, y));
        }
        catch (Exception e) { Report(a.Target ?? a, a.Method.Name, e); }
    }
}
