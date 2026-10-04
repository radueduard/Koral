using System.Numerics;
using System.Runtime.CompilerServices;
using Koral.UI.Native;

namespace Koral.UI;

/// <summary>
/// kui::Ui: a widget tree, live — builds it, lays it out, paints it, and feeds it the scene's input.
/// <code>
/// class Menu : Scene
/// {
///     readonly Ui _ui = new(new MainMenu());
///     protected override void Initialize() => Graph.Add(new UiPass(_ui));
///     protected override void Update() => _ui.Update();
/// }
/// </code>
/// Owned by the scene that made it (or the application) and disposed with it. A hot reload builds every
/// widget again, keeping state, so an edited Build shows at once.
/// </summary>
public sealed unsafe class Ui : IDisposable
{
    private IntPtr _native;
    private readonly object? _owner;
    private Theme _theme;

    public Ui(Widget? root = null, Theme? theme = null, float scale = 1f)
    {
        _theme = theme ?? Theme.Dark();
        var handle = root?.NewHandle() ?? IntPtr.Zero;
        try
        {
            var t = _theme.Native;
            _native = KuiNative.Check(KuiNative.kui_ui_new(handle, &t, scale));
        }
        finally
        {
            if (handle != IntPtr.Zero) KuiNative.kui_widget_release(handle);
        }
        _owner = Ownership.Adopt(this);
    }

    internal IntPtr Native
    {
        get
        {
            ObjectDisposedException.ThrowIf(_native == IntPtr.Zero, this);
            return _native;
        }
    }

    public Ui SetRoot(Widget? root)
    {
        var handle = root?.NewHandle() ?? IntPtr.Zero;
        try { KuiNative.kui_ui_set_root(Native, handle); }
        finally { if (handle != IntPtr.Zero) KuiNative.kui_widget_release(handle); }
        return this;
    }

    /// <summary>A new theme: every widget built again with it, keeping its state.</summary>
    public Ui SetTheme(Theme theme)
    {
        _theme = theme;
        var t = theme.Native;
        KuiNative.kui_ui_set_theme(Native, &t);
        return this;
    }

    public Theme Theme => _theme;
    public Ui SetScale(float scale) { KuiNative.kui_ui_set_scale(Native, scale); return this; }

    /// <summary>The frame, with the current scene's input, window and clock.</summary>
    public void Update()
    {
        KuiNative.kui_ui_update(Native);
        KuiNative.Check();
    }

    /// <summary>The frame, with <paramref name="input"/>, over a target of <paramref name="viewport"/> pixels, <paramref name="dt"/> seconds after the last.</summary>
    public void Update(Input input, Vector2 viewport, float dt)
    {
        KuiNative.kui_ui_update_with(Native, input.Native, viewport.X, viewport.Y, dt);
        KuiNative.Check();
    }

    /// <summary>Takes the keyboard from whatever has it: a text field being typed into.</summary>
    public void ClearFocus() => KuiNative.kui_ui_clear_focus(Native);

    /// <summary>Every widget built again, keeping its state.</summary>
    public void Reassemble() => KuiNative.kui_ui_reassemble(Native);
    /// <summary>Every live Ui reassembled: what a hot reload does.</summary>
    public static void ReassembleAll() => KuiNative.kui_ui_reassemble_all();

    /// <summary>
    /// kui::debug::SetPaintBounds: every render object outlined where it was laid out, in every Ui of the
    /// process — containers in one colour, what holds nothing in a fainter one, repaint boundaries in a third.
    /// </summary>
    public static bool DebugPaintBounds
    {
        get => KuiNative.kui_debug_paint_bounds() != 0;
        set => KuiNative.kui_debug_set_paint_bounds(KuiNative.Bool(value));
    }

    /// <summary>Whether the pointer is over a widget that takes it, or one is being dragged.</summary>
    public bool WantsPointer => KuiNative.kui_ui_wants_pointer(Native).AsBool();
    /// <summary>Whether a widget has the keyboard.</summary>
    public bool WantsKeyboard => KuiNative.kui_ui_wants_keyboard(Native).AsBool();

    public readonly record struct Statistics(ulong Builds, ulong Layouts, ulong Paints, double InputMs, double BuildMs, double LayoutMs, double PaintMs);

    public Statistics Stats
    {
        get
        {
            KuiUiStats s;
            KuiNative.kui_ui_stats(Native, &s);
            return new(s.builds, s.layouts, s.paints, s.input_ms, s.build_ms, s.layout_ms, s.paint_ms);
        }
    }

    public Renderer Renderer => new(KuiNative.kui_ui_renderer(Native));

    public void Dispose()
    {
        if (_native == IntPtr.Zero) return;
        KuiNative.kui_ui_destroy(_native);
        _native = IntPtr.Zero;
        Ownership.Release(_owner, this);
    }

    // A hot reload: widget types are built again by the Ui rather than by reopening scenes, and every Ui is
    // reassembled so edited Build methods run.
#pragma warning disable CA2255
    [ModuleInitializer]
#pragma warning restore CA2255
    internal static void RegisterHotReload()
    {
        HotReload.Claim(type => typeof(Widget).IsAssignableFrom(type));
        HotReload.Updated += _ => ReassembleAll();
    }
}
