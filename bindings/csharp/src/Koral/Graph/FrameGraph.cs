using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Koral.Native;

namespace Koral;

/// <summary>kor::ImageDesc: an image a pass creates in the graph.</summary>
public record struct ImageDesc()
{
    public Image.Format Format { get; init; } = Image.Format.eRGBA8_UNORM;
    public Image.Usage Usage { get; init; }
    /// <summary>Of the screen's size (or <see cref="SizeOf"/>'s), unless <see cref="Extent"/> says otherwise.</summary>
    public float Scale { get; init; } = 1f;
    public string SizeOf { get; init; } = "";
    public UVec2? Extent { get; init; }
    public uint MipLevels { get; init; } = 1;
}

/// <summary>kor::BufferDesc: a buffer a pass creates in the graph.</summary>
public record struct BufferDesc()
{
    public long Size { get; init; }
    public Buffer.Usage Usage { get; init; } = Buffer.Usage.eStorage;
    public Buffer.Type Type { get; init; } = Buffer.Type.eDeviceLocal;
}

/// <summary>
/// kor::RenderPass: <see cref="Setup"/> says what it uses, <see cref="Initialize"/> looks its resources up
/// (again whenever they change), <see cref="Prepare"/> runs on the main thread before recording, and
/// <see cref="Record"/> records.
/// </summary>
/// <remarks>
/// Record runs on a worker thread alongside other passes, as in C++ (where it is const): it should only
/// read what the pass holds. Copy what it needs from the scene in <see cref="Prepare"/>.
/// </remarks>
public abstract unsafe class RenderPass
{
    private bool _enabled = true;
    internal IntPtr Native;

    protected RenderPass(string name) => Name = name;

    public string Name { get; }

    public abstract void Setup(PassBuilder builder);
    public virtual void Initialize(PassResources resources) { }
    public virtual void Prepare() { }
    public abstract void Record(CommandBuffer commandBuffer);

    public bool Enabled => Native != IntPtr.Zero ? KoralNative.koral_pass_enabled(Native).AsBool() : _enabled;

    public void SetEnabled(bool enabled)
    {
        _enabled = enabled;
        if (Native != IntPtr.Zero) KoralNative.koral_pass_set_enabled(Native, KoralNative.Bool(enabled));
    }

    /// <summary>Whether last frame's <paramref name="name"/> exists, for a pass that reads it (ReadPrevious).</summary>
    protected bool HasPrevious(string name) => Native != IntPtr.Zero && KoralNative.koral_pass_has_previous(Native, name).AsBool();

    /// <summary>Asks for <see cref="Initialize"/> to run again before the next frame.</summary>
    protected void RequestInitialize()
    {
        if (Native != IntPtr.Zero) KoralNative.koral_pass_request_initialize(Native);
    }

    // Every C# pass in a graph, weakly: what a hot reload re-runs Setup and Initialize on when their code changed.
    private static readonly List<WeakReference<RenderPass>> InGraphs = [];

    /// <summary>The C# passes in a graph now, of <paramref name="type"/> (or anything deriving from it).</summary>
    internal static IReadOnlyList<RenderPass> Live(Type type)
    {
        lock (InGraphs)
        {
            InGraphs.RemoveAll(w => !w.TryGetTarget(out var p) || p.Native == IntPtr.Zero);
            return InGraphs.Select(w => w.TryGetTarget(out var p) ? p : null)
                .Where(p => p is not null && type.IsInstanceOfType(p)).ToList()!;
        }
    }

    /// <summary>Its graph set up again and this pass initialized again, before the next frame.</summary>
    internal void Reinitialize() => RequestInitialize();

    /// <summary>Adds this pass to <paramref name="graph"/>; a pass Koral implements natively overrides it.</summary>
    internal virtual IntPtr AddTo(IntPtr graph)
    {
        lock (InGraphs) InGraphs.Add(new WeakReference<RenderPass>(this));
        var callbacks = new KoralPassCallbacks
        {
            user = (void*)GCHandle.ToIntPtr(GCHandle.Alloc(this)),
            setup = &OnSetup,
            initialize = &OnInitialize,
            prepare = &OnPrepare,
            destroy = &OnDestroy,
        };
        if (this is CpuPass) callbacks.run = &OnRun;
        else callbacks.record = &OnRecord;
        var added = KoralNative.koral_graph_add(graph, Name, &callbacks);
        if (added == IntPtr.Zero)
        {
            GCHandle.FromIntPtr((IntPtr)callbacks.user).Free();
            KoralNative.Check();
        }
        return added;
    }

    private static RenderPass Of(void* user) => (RenderPass)GCHandle.FromIntPtr((IntPtr)user).Target!;

    private static void Failed(RenderPass pass, string hook, Exception e) => Log.Error($"[{pass.Name}] {hook} threw {e}");

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void OnSetup(IntPtr native, IntPtr builder, void* user)
    {
        var pass = Of(user);
        pass.Native = native;
        if (!pass._enabled) KoralNative.koral_pass_set_enabled(native, 0);
        try { pass.Setup(new PassBuilder(builder)); } catch (Exception e) { Failed(pass, nameof(Setup), e); }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void OnInitialize(IntPtr native, IntPtr resources, void* user)
    {
        var pass = Of(user);
        try { pass.Initialize(new PassResources(resources)); } catch (Exception e) { Failed(pass, nameof(Initialize), e); }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void OnPrepare(IntPtr native, void* user)
    {
        var pass = Of(user);
        try { pass.Prepare(); } catch (Exception e) { Failed(pass, nameof(Prepare), e); }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void OnRecord(IntPtr native, IntPtr commands, void* user)
    {
        var pass = Of(user);
        try { pass.Record(new CommandBuffer(commands)); } catch (Exception e) { Failed(pass, nameof(Record), e); }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void OnRun(IntPtr native, void* user)
    {
        var pass = (CpuPass)Of(user);
        try { pass.Run(); } catch (Exception e) { Failed(pass, nameof(CpuPass.Run), e); }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void OnDestroy(void* user)
    {
        var handle = GCHandle.FromIntPtr((IntPtr)user);
        if (handle.Target is RenderPass pass) pass.Native = IntPtr.Zero;
        (handle.Target as IDisposable)?.Dispose();
        handle.Free();
    }
}

/// <summary>kor::CpuPass: work on the CPU, ordered in the graph like any pass — with <see cref="Run"/> instead of Record.</summary>
public abstract class CpuPass(string name) : RenderPass(name)
{
    public abstract void Run();
    public sealed override void Record(CommandBuffer commandBuffer) { }
}

/// <summary>
/// kor::DebugDrawPass: a <see cref="DebugDraw"/>'s lines, drawn into <paramref name="target"/> with the
/// camera <paramref name="viewProjection"/> gives, read on the main thread each frame.
/// </summary>
public sealed unsafe class DebugDrawPass(DebugDraw draw, Func<Mat4> viewProjection, string target = FrameGraph.Screen, string? depth = null)
    : RenderPass("DebugDraw")
{
    public override void Setup(PassBuilder builder) { }
    public override void Record(CommandBuffer commandBuffer) { }

    internal override IntPtr AddTo(IntPtr graph)
    {
        var user = GCHandle.ToIntPtr(GCHandle.Alloc(viewProjection));
        var added = KoralNative.koral_graph_add_debug_draw_pass(graph, draw.Native, &Camera, (void*)user, &Free, target, depth);
        KoralNative.Check(added);
        Native = added;
        return added;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Camera(float* matrix, void* user)
    {
        var camera = (Func<Mat4>)GCHandle.FromIntPtr((IntPtr)user).Target!;
        try { *(Mat4*)matrix = camera(); }
        catch (Exception e)
        {
            Log.Error($"[DebugDraw] the camera threw {e}");
            *(Mat4*)matrix = Mat4.Identity;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Free(void* user) => GCHandle.FromIntPtr((IntPtr)user).Free();
}

/// <summary>kor::PassBuilder: what a pass reads, writes and creates. Borrowed, during Setup.</summary>
public sealed class PassBuilder
{
    private readonly IntPtr _native;
    internal PassBuilder(IntPtr native) => _native = native;

    public PassBuilder Read(string name) { KoralNative.koral_pass_builder_read(_native, name, 0, 0); return this; }
    public PassBuilder Read(string name, Image.Usage usage) { KoralNative.koral_pass_builder_read(_native, name, 1, (uint)usage); return this; }
    public PassBuilder Read(string name, Buffer.Usage usage) { KoralNative.koral_pass_builder_read(_native, name, 2, (uint)usage); return this; }
    public PassBuilder Write(string name) { KoralNative.koral_pass_builder_write(_native, name, 0, 0); return this; }
    public PassBuilder Write(string name, Image.Usage usage) { KoralNative.koral_pass_builder_write(_native, name, 1, (uint)usage); return this; }
    public PassBuilder Write(string name, Buffer.Usage usage) { KoralNative.koral_pass_builder_write(_native, name, 2, (uint)usage); return this; }

    public unsafe PassBuilder Create(string name, ImageDesc desc)
    {
        var d = new KoralImageDesc
        {
            format = (uint)desc.Format, usage = (uint)desc.Usage, scale = desc.Scale,
            size_of = KoralNative.Utf8(desc.SizeOf), has_extent = KoralNative.Bool(desc.Extent.HasValue),
            mip_levels = desc.MipLevels,
        };
        if (desc.Extent is { } e) { d.extent[0] = e.X; d.extent[1] = e.Y; }
        try { KoralNative.koral_pass_builder_create_image(_native, name, &d); }
        finally { KoralNative.Free(d.size_of); }
        return this;
    }

    public unsafe PassBuilder Create(string name, BufferDesc desc)
    {
        var d = new KoralBufferDesc { size = desc.Size, usage = (uint)desc.Usage, type = (uint)desc.Type };
        KoralNative.koral_pass_builder_create_buffer(_native, name, &d);
        return this;
    }

    public PassBuilder Consume(string name, string @as) { KoralNative.koral_pass_builder_consume(_native, name, @as, 0, 0); return this; }
    public PassBuilder Consume(string name, string @as, Image.Usage usage) { KoralNative.koral_pass_builder_consume(_native, name, @as, 1, (uint)usage); return this; }
    public PassBuilder Consume(string name, string @as, Buffer.Usage usage) { KoralNative.koral_pass_builder_consume(_native, name, @as, 2, (uint)usage); return this; }
    public PassBuilder SideEffect() { KoralNative.koral_pass_builder_side_effect(_native); return this; }
    public PassBuilder ReadPrevious(string name) { KoralNative.koral_pass_builder_read_previous(_native, name, 0, 0); return this; }
    public PassBuilder ReadPrevious(string name, Image.Usage usage) { KoralNative.koral_pass_builder_read_previous(_native, name, 1, (uint)usage); return this; }
    public PassBuilder ReadPrevious(string name, Buffer.Usage usage) { KoralNative.koral_pass_builder_read_previous(_native, name, 2, (uint)usage); return this; }
    public PassBuilder AsyncCompute() { KoralNative.koral_pass_builder_async_compute(_native); return this; }
}

/// <summary>kor::PassResources: a pass's resources, by name. Borrowed, during Initialize; what it returns stays valid until the next.</summary>
public sealed unsafe class PassResources
{
    private readonly IntPtr _native;
    internal PassResources(IntPtr native) => _native = native;

    public Image? ImageNamed(string name) => Resource.Wrap<Image>(KoralNative.koral_pass_resources_image_named(_native, name));
    public Buffer? BufferNamed(string name) => Resource.Wrap<Buffer>(KoralNative.koral_pass_resources_buffer_named(_native, name));
    public Buffer? WritableBufferNamed(string name) => Resource.Wrap<Buffer>(KoralNative.koral_pass_resources_writable_buffer_named(_native, name));

    public UVec2 Extent(string name)
    {
        uint x, y;
        KoralNative.koral_pass_resources_extent(_native, name, &x, &y);
        return new UVec2(x, y);
    }

    public Image? PreviousImageNamed(string name) => Resource.Wrap<Image>(KoralNative.koral_pass_resources_previous_image_named(_native, name));
    public Buffer? PreviousBufferNamed(string name) => Resource.Wrap<Buffer>(KoralNative.koral_pass_resources_previous_buffer_named(_native, name));
}

/// <summary>
/// kor::FrameGraph: a scene's (or a view's) passes, ordered and synchronised by what they use. Resources
/// are created, aliased and cleaned up by the graph.
/// </summary>
public sealed unsafe class FrameGraph
{
    /// <summary>The name the screen goes by in a graph: the scene's (or view's) window.</summary>
    public const string Screen = "screen";

    internal readonly IntPtr Native;
    internal FrameGraph(IntPtr native) => Native = native;

    /// <summary>Add(pass): the graph has it from here, and it is returned for keeping a reference, as in C++.</summary>
    public TPass Add<TPass>(TPass pass) where TPass : RenderPass
    {
        if (pass.Native != IntPtr.Zero) throw new InvalidOperationException($"'{pass.Name}' is already in a graph.");
        pass.Native = pass.AddTo(Native);
        return pass;
    }

    /// <summary>Add&lt;P&gt;(): a new <typeparamref name="TPass"/>, made with its parameterless constructor.</summary>
    public TPass Add<TPass>() where TPass : RenderPass, new() => Add(new TPass());

    public void Import(string name, Image image) { KoralNative.koral_graph_import_image(Native, name, image.Handle); KoralNative.Check(); }
    public void Import(string name, Buffer buffer) { KoralNative.koral_graph_import_buffer(Native, name, buffer.Handle); KoralNative.Check(); }
    public void Invalidate() => KoralNative.koral_graph_invalidate(Native);
    /// <summary>empty().</summary>
    public bool IsEmpty => KoralNative.koral_graph_empty(Native).AsBool();

    /// <summary>kor::FrameGraph::Scheduled.</summary>
    public readonly record struct Scheduled(string Name, uint Level, bool Async);

    /// <summary>kor::FrameGraph::Skipped: a pass left out, for a resource nothing gave it.</summary>
    public readonly record struct Skipped(string Name, string Resource, string Source);

    /// <summary>kor::FrameGraph::GraphTiming.</summary>
    public readonly record struct GraphTiming(double PrepareMs, double RecordWallMs, double RecordWorkMs, double GpuMs);

    /// <summary>kor::FrameGraph::MemoryUse.</summary>
    public readonly record struct MemoryUse(ulong Bytes, ulong UnsharedBytes, uint Resources, uint Allocations);

    public IReadOnlyList<Scheduled> Schedule
    {
        get
        {
            var count = KoralNative.koral_graph_schedule_count(Native);
            var list = new Scheduled[count];
            for (uint i = 0; i < count; ++i)
            {
                uint level;
                byte async;
                var name = KoralNative.Text(KoralNative.koral_graph_schedule(Native, i, &level, &async));
                list[i] = new Scheduled(name, level, async != 0);
            }
            return list;
        }
    }

    public IReadOnlyList<string> CulledPasses
    {
        get
        {
            var count = KoralNative.koral_graph_culled_pass_count(Native);
            var list = new string[count];
            for (uint i = 0; i < count; ++i) list[i] = KoralNative.Text(KoralNative.koral_graph_culled_pass(Native, i));
            return list;
        }
    }

    public IReadOnlyList<Skipped> SkippedPasses
    {
        get
        {
            var count = KoralNative.koral_graph_skipped_pass_count(Native);
            var list = new Skipped[count];
            for (uint i = 0; i < count; ++i)
            {
                byte* resource, source;
                var name = KoralNative.Text(KoralNative.koral_graph_skipped_pass(Native, i, &resource, &source));
                list[i] = new Skipped(name, KoralNative.Text(resource), KoralNative.Text(source));
            }
            return list;
        }
    }

    public GraphTiming Timing
    {
        get
        {
            double a, b, c, d;
            KoralNative.koral_graph_timing(Native, &a, &b, &c, &d);
            return new GraphTiming(a, b, c, d);
        }
    }

    public Image? ImageNamed(string name, Image.Usage usage = 0) => Resource.Wrap<Image>(KoralNative.koral_graph_image_named(Native, name, (uint)usage));
    public void SetAliasing(bool enabled) => KoralNative.koral_graph_set_aliasing(Native, KoralNative.Bool(enabled));
    public bool Aliasing => KoralNative.koral_graph_aliasing(Native).AsBool();

    public MemoryUse Memory
    {
        get
        {
            ulong bytes, unshared;
            uint resources, allocations;
            KoralNative.koral_graph_memory(Native, &bytes, &unshared, &resources, &allocations);
            return new MemoryUse(bytes, unshared, resources, allocations);
        }
    }

    public bool HasPrevious(string name) => KoralNative.koral_graph_has_previous(Native, name).AsBool();
}
