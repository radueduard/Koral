using Koral.UI.Native;

namespace Koral.UI;

/// <summary>
/// kui::Renderer: draws a tree of layers, once a frame — doing nothing at all when nothing changed. Owned
/// by the scene that made it (or the application), and disposed with it; draw it with a <see cref="UiPass"/>.
/// </summary>
public sealed unsafe class Renderer : IDisposable
{
    private IntPtr _native;
    private readonly bool _borrowed;
    private readonly object? _owner;
    private Layer? _root;

    public Renderer()
    {
        _native = KuiNative.kui_renderer_new();
        _owner = Ownership.Adopt(this);
    }

    internal Renderer(IntPtr borrowed)
    {
        _native = borrowed;
        _borrowed = true;
    }

    internal IntPtr Native
    {
        get
        {
            ObjectDisposedException.ThrowIf(_native == IntPtr.Zero, this);
            return _native;
        }
    }

    public Layer? Root
    {
        get => _root;
        set => SetRoot(value);
    }

    public Renderer SetRoot(Layer? root)
    {
        KuiNative.kui_renderer_set_root(Native, root?.Native ?? IntPtr.Zero);
        _root = root;
        return this;
    }

    /// <summary>Pixels per logical unit: the display's scale factor.</summary>
    public Renderer SetScale(float scale) { KuiNative.kui_renderer_set_scale(Native, scale); return this; }

    public readonly record struct Statistics(ulong Instances, ulong Vertices, ulong Layers, ulong Clips, ulong Draws,
                                             ulong UploadedBytes, double ComposeMs, bool Recomposed);

    public Statistics Stats
    {
        get
        {
            KuiRendererStats s;
            KuiNative.kui_renderer_stats(Native, &s);
            return new(s.instances, s.vertices, s.layers, s.clips, s.draws, s.uploaded_bytes, s.compose_ms, s.recomposed != 0);
        }
    }

    public void Dispose()
    {
        if (_native == IntPtr.Zero || _borrowed) return;
        KuiNative.kui_renderer_destroy(_native);
        _native = IntPtr.Zero;
        Ownership.Release(_owner, this);
    }
}

/// <summary>
/// kui::UiPass: a frame-graph pass drawing a <see cref="Ui"/> — or a bare <see cref="Renderer"/>'s
/// layers — over <c>target</c>, the screen by default.
/// <code>Graph.Add(new UiPass(_ui));</code>
/// </summary>
public sealed class UiPass : RenderPass
{
    private readonly Ui? _ui;
    private readonly Renderer? _renderer;
    private readonly string _target;

    public UiPass(Ui ui, string target = FrameGraph.Screen) : base("UI")
    {
        _ui = ui;
        _target = target;
    }

    public UiPass(Renderer renderer, string target = FrameGraph.Screen) : base("UI")
    {
        _renderer = renderer;
        _target = target;
    }

    // Koral's own pass: nothing of it runs in C#.
    public override void Setup(PassBuilder builder) { }
    public override void Record(CommandBuffer commandBuffer) { }

    internal override IntPtr AddTo(IntPtr graph)
    {
        var added = _ui is not null
            ? KuiNative.kui_graph_add_ui_pass(graph, _ui.Native, _target)
            : KuiNative.kui_graph_add_ui_pass_with_renderer(graph, _renderer!.Native, _target);
        KuiNative.Check(added);
        Native = added;
        return added;
    }
}
