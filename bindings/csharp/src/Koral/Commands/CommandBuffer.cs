using System.Numerics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Koral.Native;

namespace Koral;

/// <summary>
/// kor::CommandBuffer: a frame's GPU work, recorded as a chain of commands.
/// </summary>
/// <example>
/// <code>
/// commandBuffer
///     .BeginRendering()
///     .BindGraphicsPipeline(_pipeline)
///     .BindDescriptorSet(0, _set)
///     .PushConstant("tint", new Vector4(1, 0.5f, 0, 1))
///     .BindMesh(_mesh)
///     .DrawIndexed()
///     .EndRendering();
/// </code>
/// </example>
/// <remarks>
/// The one handed to a hook or a pass is borrowed: valid during that call. A command that cannot be
/// recorded is not an exception, as in C++: it is kept (<see cref="Ok"/>, <see cref="Errors"/>) and the
/// rest of the frame goes on.
/// </remarks>
public sealed unsafe partial class CommandBuffer : IDisposable
{
    /// <summary>kor::WholeSize: "all of it", for a count or a size.</summary>
    public const ulong WholeSize = ulong.MaxValue;
    public const uint MaxTimerScopes = 256;

    private IntPtr _native;
    private readonly bool _owned;
    private IResourceOwner? _owner;

    internal CommandBuffer(IntPtr native, bool owned = false)
    {
        _native = native;
        _owned = owned;
    }

    private IntPtr N
    {
        get
        {
            ObjectDisposedException.ThrowIf(_native == IntPtr.Zero, this);
            return _native;
        }
    }

    // ---- one of one's own ----------------------------------------------------------------------------

    /// <summary>Create(usage): a command buffer of one's own, freed by Dispose.</summary>
    public static CommandBuffer Create(Usage usage)
    {
        var made = new CommandBuffer(KoralNative.Check(KoralNative.koral_cmd_create((uint)usage)), owned: true);
        // Freed before the device is, if nobody disposes it: the application's (or its scene's) to clean up.
        if (Scene.Current is { IsManaged: true } scene) (made._owner = scene).Own(made);
        else if (App.Current is { } app) (made._owner = app).Own(made);
        return made;
    }

    /// <summary>
    /// SingleTimeCommand(command, usage): records <paramref name="command"/> now and submits it; the token
    /// is signalled when the GPU has run it — <c>Wait()</c> on it, or <c>await</c> it.
    /// </summary>
    public static Token SingleTimeCommand(Action<CommandBuffer> command, Usage usage = Usage.eGraphics)
    {
        var handle = GCHandle.Alloc(command);
        try
        {
            var token = KoralNative.koral_cmd_single_time_command(&RunCommand, (void*)GCHandle.ToIntPtr(handle), (uint)usage);
            return new Token(KoralNative.Check(token));
        }
        finally
        {
            handle.Free();
        }
    }

    public CommandBuffer Begin() { KoralNative.koral_cmd_begin(N); return this; }
    public void End() => KoralNative.koral_cmd_end(N);
    public bool IsRecording => KoralNative.koral_cmd_is_recording(N).AsBool();

    public void Submit(SubmitInfo? info = null)
    {
        var wait = info?.WaitFor.Select(t => t.Native).ToArray() ?? [];
        var signal = info?.Signal.Select(t => t.Native).ToArray() ?? [];
        fixed (IntPtr* w = wait)
        fixed (IntPtr* s = signal)
            KoralNative.Check(KoralNative.koral_cmd_submit(N, w, (nuint)wait.Length, s, (nuint)signal.Length));
    }

    public void Reset() => KoralNative.koral_cmd_reset(N);
    public void WaitForFence() => KoralNative.koral_cmd_wait_for_fence(N);

    /// <summary>Frees one made with <see cref="Create"/>; does nothing to a borrowed one.</summary>
    public void Dispose()
    {
        if (_native == IntPtr.Zero) return;
        if (_owned) KoralNative.koral_cmd_destroy(_native);
        _native = IntPtr.Zero;
        _owner?.Disown(this);
    }

    // ---- what went wrong ------------------------------------------------------------------------------

    public bool Ok => KoralNative.koral_cmd_ok(N).AsBool();

    /// <summary>Every command that could not be recorded, each with its whole history.</summary>
    public IReadOnlyList<string> Errors
    {
        get
        {
            var count = KoralNative.koral_cmd_error_count(N);
            var errors = new string[count];
            for (uint i = 0; i < count; ++i) errors[i] = KoralNative.Text(KoralNative.koral_cmd_error(N, i));
            return errors;
        }
    }

    public bool HasTouched(Image image) => KoralNative.koral_cmd_has_touched(N, image.Handle).AsBool();

    /// <summary>ScreenImage(): the current window's (or view's) screen.</summary>
    public static Image? ScreenImage() => Resource.Wrap<Image>(KoralNative.koral_cmd_screen_image());

    // ---- timers ----------------------------------------------------------------------------------------

    public CommandBuffer BeginTimer(string label) { KoralNative.koral_cmd_begin_timer(N, label); return this; }
    public CommandBuffer EndTimer() { KoralNative.koral_cmd_end_timer(N); return this; }

    /// <summary>Timer(label, body): a timed scope around what <paramref name="body"/> records.</summary>
    public CommandBuffer Timer(string label, Action<CommandBuffer> body)
    {
        BeginTimer(label);
        body(this);
        return EndTimer();
    }

    /// <summary>CollectTimer(label): its milliseconds, from the last time this command buffer ran.</summary>
    public double CollectTimer(string label)
    {
        double milliseconds;
        KoralNative.Check(KoralNative.koral_cmd_collect_timer(N, label, &milliseconds));
        return milliseconds;
    }

    public IReadOnlyList<TimerResult> CollectTimings()
    {
        KoralNative.koral_cmd_collect_timings(N);
        return Timings;
    }

    public IReadOnlyList<TimerResult> Timings
    {
        get
        {
            var count = KoralNative.koral_cmd_collect_timings(N);
            var timings = new TimerResult[count];
            for (uint i = 0; i < count; ++i)
            {
                double ms;
                uint depth;
                var label = KoralNative.Text(KoralNative.koral_cmd_timing(N, i, &ms, &depth));
                timings[i] = new TimerResult(label, ms, depth);
            }
            return timings;
        }
    }

    public bool SupportsTimers => KoralNative.koral_cmd_supports_timers(N).AsBool();
    public ulong LastFrameCommandCount => KoralNative.koral_cmd_last_frame_command_count(N);

    // ---- rendering and dynamic state ---------------------------------------------------------------------

    /// <summary>BeginRendering(renderInfo): into the window's (or view's) screen, cleared, when none is given.</summary>
    public CommandBuffer BeginRendering(RenderInfo? renderInfo = null)
    {
        if (renderInfo is null)
        {
            KoralNative.koral_cmd_begin_rendering(N, null);
            return this;
        }
        renderInfo.WithNative(native => KoralNative.koral_cmd_begin_rendering(N, native));
        return this;
    }

    public CommandBuffer EndRendering() { KoralNative.koral_cmd_end_rendering(N); return this; }
    public CommandBuffer SetViewport(uint x, uint y, uint width, uint height) { KoralNative.koral_cmd_set_viewport(N, x, y, width, height); return this; }
    public CommandBuffer SetScissor(uint x, uint y, uint width, uint height) { KoralNative.koral_cmd_set_scissor(N, x, y, width, height); return this; }
    public CommandBuffer SetLineWidth(float lineWidth) { KoralNative.koral_cmd_set_line_width(N, lineWidth); return this; }
    public CommandBuffer SetDepthBias(float constantFactor, float clamp, float slopeFactor) { KoralNative.koral_cmd_set_depth_bias(N, constantFactor, clamp, slopeFactor); return this; }
    public CommandBuffer SetBlendConstants(Vector4 constants) { KoralNative.koral_cmd_set_blend_constants(N, (float*)&constants); return this; }
    public CommandBuffer SetStencilCompareMask(StencilFace face, uint compareMask) { KoralNative.koral_cmd_set_stencil_compare_mask(N, (uint)face, compareMask); return this; }
    public CommandBuffer SetStencilWriteMask(StencilFace face, uint writeMask) { KoralNative.koral_cmd_set_stencil_write_mask(N, (uint)face, writeMask); return this; }
    public CommandBuffer SetStencilReference(StencilFace face, uint reference) { KoralNative.koral_cmd_set_stencil_reference(N, (uint)face, reference); return this; }
    public CommandBuffer SetCullMode(CullMode cullMode) { KoralNative.koral_cmd_set_cull_mode(N, (uint)cullMode); return this; }
    public CommandBuffer SetFrontFace(FrontFace frontFace) { KoralNative.koral_cmd_set_front_face(N, (uint)frontFace); return this; }
    public CommandBuffer SetDepthTestEnable(bool enable) { KoralNative.koral_cmd_set_depth_test_enable(N, KoralNative.Bool(enable)); return this; }
    public CommandBuffer SetDepthWriteEnable(bool enable) { KoralNative.koral_cmd_set_depth_write_enable(N, KoralNative.Bool(enable)); return this; }
    public CommandBuffer SetDepthCompareOp(CompareOp compareOp) { KoralNative.koral_cmd_set_depth_compare_op(N, (uint)compareOp); return this; }
    public CommandBuffer SetStencilTestEnable(bool enable) { KoralNative.koral_cmd_set_stencil_test_enable(N, KoralNative.Bool(enable)); return this; }
    public CommandBuffer SetStencilOp(StencilFace face, StencilOp failOp, StencilOp passOp, StencilOp depthFailOp, CompareOp compareOp)
    {
        KoralNative.koral_cmd_set_stencil_op(N, (uint)face, (uint)failOp, (uint)passOp, (uint)depthFailOp, (uint)compareOp);
        return this;
    }
    public CommandBuffer SetDepthBiasEnable(bool enable) { KoralNative.koral_cmd_set_depth_bias_enable(N, KoralNative.Bool(enable)); return this; }
    public CommandBuffer SetRasterizerDiscardEnable(bool enable) { KoralNative.koral_cmd_set_rasterizer_discard_enable(N, KoralNative.Bool(enable)); return this; }
    public CommandBuffer SetPrimitiveRestartEnable(bool enable) { KoralNative.koral_cmd_set_primitive_restart_enable(N, KoralNative.Bool(enable)); return this; }

    // ---- binding ------------------------------------------------------------------------------------------

    public CommandBuffer BindComputePipeline(ComputePipeline pipeline) { KoralNative.koral_cmd_bind_compute_pipeline(N, pipeline.Handle); return this; }
    public CommandBuffer BindGraphicsPipeline(GraphicsPipeline pipeline) { KoralNative.koral_cmd_bind_graphics_pipeline(N, pipeline.Handle); return this; }
    public CommandBuffer BindRayTracingPipeline(RayTracingPipeline pipeline) { KoralNative.koral_cmd_bind_ray_tracing_pipeline(N, pipeline.Handle); return this; }
    public CommandBuffer BindDescriptorSet(uint index, DescriptorSet descriptorSet) { KoralNative.koral_cmd_bind_descriptor_set(N, index, descriptorSet.Handle); return this; }
    public CommandBuffer BindMesh(Mesh mesh) { KoralNative.koral_cmd_bind_mesh(N, mesh.Handle); return this; }
    /// <summary>Binds <paramref name="buffer"/> to vertex <paramref name="binding"/>: per-instance data beside a mesh's own bindings.</summary>
    public CommandBuffer BindVertexBuffer(uint binding, Buffer buffer, ulong offset = 0) { KoralNative.koral_cmd_bind_vertex_buffer(N, binding, buffer.Handle, offset); return this; }

    /// <summary>PushConstantBlock(data, offset): the bytes of <paramref name="data"/>, at <paramref name="offset"/>.</summary>
    public CommandBuffer PushConstantBlock<T>(in T data, uint offset = 0) where T : unmanaged
    {
        fixed (T* pointer = &data) KoralNative.koral_cmd_push_constant_block(N, pointer, (uint)sizeof(T), offset);
        return this;
    }

    /// <summary>
    /// PushConstant(name, data): the push constant the shaders call <paramref name="name"/> — checked, by
    /// what <typeparamref name="T"/> is, against what they declare it as.
    /// </summary>
    public CommandBuffer PushConstant<T>(string name, in T data) where T : unmanaged
    {
        var shape = ValueShape.Of<T>().Native;
        fixed (T* pointer = &data) KoralNative.koral_cmd_push_constant(N, name, pointer, (uint)sizeof(T), &shape);
        return this;
    }

    /// <summary>An array push constant: <c>float weights[4]</c> is four floats.</summary>
    public CommandBuffer PushConstant<T>(string name, ReadOnlySpan<T> data) where T : unmanaged
    {
        var shape = (ValueShape.Of<T>() with { Count = (uint)data.Length * ValueShape.Of<T>().Count }).Native;
        fixed (T* pointer = data) KoralNative.koral_cmd_push_constant(N, name, pointer, (uint)(sizeof(T) * data.Length), &shape);
        return this;
    }

    // ---- synchronisation and labels ----------------------------------------------------------------------------

    public CommandBuffer Barrier(IEnumerable<BufferBarrier>? bufferBarriers = null, IEnumerable<ImageBarrier>? imageBarriers = null)
    {
        var buffers = (bufferBarriers ?? []).Select(b => b.Native).ToArray();
        var images = (imageBarriers ?? []).Select(b => b.Native).ToArray();
        fixed (KoralBufferBarrier* bp = buffers)
        fixed (KoralImageBarrier* ip = images)
            KoralNative.koral_cmd_barrier(N, bp, (nuint)buffers.Length, ip, (nuint)images.Length);
        return this;
    }

    public CommandBuffer BufferBarrier(BufferBarrier barrier) => Barrier([barrier]);
    public CommandBuffer ImageBarrier(ImageBarrier barrier) => Barrier(null, [barrier]);

    public CommandBuffer BeginDebugLabel(string label, Vector4? color = null)
    {
        var c = color ?? Vector4.One;
        KoralNative.koral_cmd_begin_debug_label(N, label, (float*)&c);
        return this;
    }

    public CommandBuffer EndDebugLabel() { KoralNative.koral_cmd_end_debug_label(N); return this; }

    public CommandBuffer InsertDebugLabel(string label, Vector4? color = null)
    {
        var c = color ?? Vector4.One;
        KoralNative.koral_cmd_insert_debug_label(N, label, (float*)&c);
        return this;
    }

    /// <summary>DebugLabel(label, body, color): a labelled scope around what <paramref name="body"/> records.</summary>
    public CommandBuffer DebugLabel(string label, Action<CommandBuffer> body, Vector4? color = null)
    {
        BeginDebugLabel(label, color);
        body(this);
        return EndDebugLabel();
    }

    // ---- work ----------------------------------------------------------------------------------------------

    public CommandBuffer Dispatch(uint groupCountX = 1, uint groupCountY = 1, uint groupCountZ = 1) { KoralNative.koral_cmd_dispatch(N, groupCountX, groupCountY, groupCountZ); return this; }
    public CommandBuffer DispatchIndirect(Buffer indirectBuffer, ulong offset = 0) { KoralNative.koral_cmd_dispatch_indirect(N, indirectBuffer.Handle, offset); return this; }
    public CommandBuffer TraceRays(uint width = 1, uint height = 1, uint depth = 1) { KoralNative.koral_cmd_trace_rays(N, width, height, depth); return this; }
    public CommandBuffer Draw(ulong vertexCount = WholeSize, uint instanceCount = 1, uint firstVertex = 0, uint firstInstance = 0)
    {
        KoralNative.koral_cmd_draw(N, vertexCount, instanceCount, firstVertex, firstInstance);
        return this;
    }
    public CommandBuffer DrawIndexed(ulong indexCount = WholeSize, uint instanceCount = 1, uint firstIndex = 0, int vertexOffset = 0, uint firstInstance = 0)
    {
        KoralNative.koral_cmd_draw_indexed(N, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
        return this;
    }
    public CommandBuffer DrawMesh(Mesh mesh, uint instanceCount, uint baseInstance) { KoralNative.koral_cmd_draw_mesh(N, mesh.Handle, instanceCount, baseInstance); return this; }
    public CommandBuffer DrawSubMesh(Mesh mesh, uint baseIndex, uint indexCount) { KoralNative.koral_cmd_draw_sub_mesh(N, mesh.Handle, baseIndex, indexCount); return this; }
    public CommandBuffer DrawMeshTasks(uint taskCountX = 1, uint taskCountY = 1, uint taskCountZ = 1) { KoralNative.koral_cmd_draw_mesh_tasks(N, taskCountX, taskCountY, taskCountZ); return this; }
    public CommandBuffer DrawIndirect(Buffer indirectBuffer, ulong offset = 0, uint drawCount = 1, uint stride = 0) { KoralNative.koral_cmd_draw_indirect(N, indirectBuffer.Handle, offset, drawCount, stride); return this; }
    public CommandBuffer DrawIndexedIndirect(Buffer indirectBuffer, ulong offset = 0, uint drawCount = 1, uint stride = 0) { KoralNative.koral_cmd_draw_indexed_indirect(N, indirectBuffer.Handle, offset, drawCount, stride); return this; }
    public CommandBuffer DrawMeshTasksIndirect(Buffer indirectBuffer, ulong offset = 0, uint drawCount = 1, uint stride = 0) { KoralNative.koral_cmd_draw_mesh_tasks_indirect(N, indirectBuffer.Handle, offset, drawCount, stride); return this; }

    // ---- transfers --------------------------------------------------------------------------------------------

    public CommandBuffer ClearBuffer(Buffer buffer, ulong offset = 0, ulong size = WholeSize) { KoralNative.koral_cmd_clear_buffer(N, buffer.Handle, offset, size); return this; }

    public CommandBuffer ClearColorImage(Image image, Vector4? color = null)
    {
        var c = color ?? new Vector4(0, 0, 0, 1);
        KoralNative.koral_cmd_clear_color_image(N, image.Handle, (float*)&c);
        return this;
    }

    /// <summary>FillBuffer(buffer, elements, offset): <paramref name="elements"/>, from byte <paramref name="offset"/>.</summary>
    public CommandBuffer FillBuffer<T>(Buffer buffer, ReadOnlySpan<T> elements, ulong offset = 0) where T : unmanaged
    {
        fixed (T* pointer = elements) KoralNative.koral_cmd_fill_buffer(N, buffer.Handle, pointer, offset, (ulong)(elements.Length * sizeof(T)));
        return this;
    }

    public CommandBuffer CopyBuffer(Buffer srcBuffer, Buffer dstBuffer, ulong size = WholeSize, ulong srcOffset = 0, ulong dstOffset = 0)
    {
        KoralNative.koral_cmd_copy_buffer(N, srcBuffer.Handle, dstBuffer.Handle, size, srcOffset, dstOffset);
        return this;
    }

    public CommandBuffer BlitToScreen(Image srcImage, Blit? blitInfo = null)
    {
        var b = (blitInfo ?? new Blit()).Native;
        KoralNative.koral_cmd_blit_to_screen(N, srcImage.Handle, &b);
        return this;
    }

    public CommandBuffer Blit(Image srcImage, Image dstImage, Blit? blitInfo = null)
    {
        var b = (blitInfo ?? new Blit()).Native;
        KoralNative.koral_cmd_blit(N, srcImage.Handle, dstImage.Handle, &b);
        return this;
    }

    public CommandBuffer CopyImage(Image srcImage, Image dstImage) { KoralNative.koral_cmd_copy_image(N, srcImage.Handle, dstImage.Handle); return this; }

    public CommandBuffer ResolveToScreen(Image srcImage, Resolve? resolveInfo = null)
    {
        var r = (resolveInfo ?? new Resolve()).Native;
        KoralNative.koral_cmd_resolve_to_screen(N, srcImage.Handle, &r);
        return this;
    }

    public CommandBuffer Resolve(Image srcImage, Image dstImage, Resolve? resolveInfo = null)
    {
        var r = (resolveInfo ?? new Resolve()).Native;
        KoralNative.koral_cmd_resolve(N, srcImage.Handle, dstImage.Handle, &r);
        return this;
    }

    public CommandBuffer GenerateMipmaps(Image image) { KoralNative.koral_cmd_generate_mipmaps(N, image.Handle); return this; }

    public CommandBuffer CopyBufferToImage(Buffer buffer, Image image, Copy? copyInfo = null)
    {
        var c = (copyInfo ?? new Copy()).Native;
        KoralNative.koral_cmd_copy_buffer_to_image(N, buffer.Handle, image.Handle, &c);
        return this;
    }

    public CommandBuffer CopyImageToBuffer(Image image, Buffer buffer, Copy? copyInfo = null)
    {
        var c = (copyInfo ?? new Copy()).Native;
        KoralNative.koral_cmd_copy_image_to_buffer(N, image.Handle, buffer.Handle, &c);
        return this;
    }

    // ---- control -----------------------------------------------------------------------------------------------

    /// <summary>Run(command): <paramref name="command"/> with this command buffer, when the backend runs it.</summary>
    public CommandBuffer Run(Action<CommandBuffer> command)
    {
        var handle = GCHandle.Alloc(command);
        try
        {
            KoralNative.koral_cmd_run(N, &RunCommand, (void*)GCHandle.ToIntPtr(handle));
        }
        finally
        {
            handle.Free();
        }
        return this;
    }

    /// <summary>If(condition, then, otherwise): records one or the other.</summary>
    public CommandBuffer If(Func<bool> condition, Action<CommandBuffer> trueCommands, Action<CommandBuffer>? falseCommands = null)
    {
        if (condition()) trueCommands(this);
        else falseCommands?.Invoke(this);
        return this;
    }

    /// <summary>Condition(select, commands...): records the one <paramref name="condition"/> picks.</summary>
    public CommandBuffer Condition(Func<byte> condition, params ReadOnlySpan<Action<CommandBuffer>> commands)
    {
        commands[condition()](this);
        return this;
    }

    /// <summary>ForEach(range, func): records <paramref name="func"/> for each item.</summary>
    public CommandBuffer ForEach<T>(IEnumerable<T> range, Action<CommandBuffer, T> func)
    {
        foreach (var item in range) func(this, item);
        return this;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void RunCommand(IntPtr commands, void* user)
    {
        var command = (Action<CommandBuffer>)GCHandle.FromIntPtr((IntPtr)user).Target!;
        try
        {
            command(new CommandBuffer(commands));
        }
        catch (Exception e)
        {
            Log.Error($"[koral] a command buffer's callback threw {e}");
        }
    }
}

/// <summary>kor::TimerResult.</summary>
public readonly record struct TimerResult(string Label, double Milliseconds, uint Depth);

/// <summary>kor::SubmitInfo: what a submission waits for, and signals.</summary>
public sealed class SubmitInfo
{
    public List<Token> WaitFor { get; init; } = [];
    public List<Token> Signal { get; init; } = [];
}

/// <summary>kor::ValueShape: what a push constant's value is — float, int, uint..., rows, columns, count.</summary>
public readonly record struct ValueShape(ValueScalar Scalar, byte Rows = 1, byte Columns = 1, uint Count = 1, bool Known = true)
{
    /// <summary>kor::ShapeOf&lt;T&gt;: what <typeparamref name="T"/> is to a shader, when Koral knows.</summary>
    public static ValueShape Of<T>() where T : unmanaged
    {
        var t = typeof(T);
        if (t == typeof(float)) return new(ValueScalar.eFloat);
        if (t == typeof(double)) return new(ValueScalar.eDouble);
        if (t == typeof(int)) return new(ValueScalar.eInt);
        if (t == typeof(uint)) return new(ValueScalar.eUInt);
        if (t == typeof(Vector2)) return new(ValueScalar.eFloat, 2);
        if (t == typeof(Vector3)) return new(ValueScalar.eFloat, 3);
        if (t == typeof(Vector4) || t == typeof(Quaternion)) return new(ValueScalar.eFloat, 4);
        if (t == typeof(Matrix4x4)) return new(ValueScalar.eFloat, 4, 4);
        if (t == typeof(IVec2)) return new(ValueScalar.eInt, 2);
        if (t == typeof(IVec3)) return new(ValueScalar.eInt, 3);
        if (t == typeof(IVec4)) return new(ValueScalar.eInt, 4);
        if (t == typeof(UVec2)) return new(ValueScalar.eUInt, 2);
        if (t == typeof(UVec3)) return new(ValueScalar.eUInt, 3);
        if (t == typeof(UVec4)) return new(ValueScalar.eUInt, 4);
        return new(ValueScalar.eOther, Known: false);
    }

    internal KoralValueShape Native => new()
    {
        scalar = (uint)Scalar, rows = Rows, columns = Columns, count = Count, known = KoralNative.Bool(Known),
    };
}

/// <summary>kor::BufferBarrier.</summary>
public sealed record BufferBarrier(Buffer Buffer, ResourceAccess DstAccess, ulong Offset = 0, ulong Size = CommandBuffer.WholeSize)
{
    internal KoralBufferBarrier Native => new() { buffer = Buffer.Handle, dst_access = (uint)DstAccess, offset = Offset, size = Size };
}

/// <summary>kor::ImageBarrier.</summary>
public sealed record ImageBarrier(Image Image, ResourceAccess DstAccess, uint? BaseMipLevel = null, uint? LevelCount = null,
                                  uint? BaseArrayLayer = null, uint? LayerCount = null)
{
    internal KoralImageBarrier Native => new()
    {
        image = Image.Handle, dst_access = (uint)DstAccess,
        base_mip_level = BaseMipLevel is { } a ? a : -1, level_count = LevelCount is { } b ? b : -1,
        base_array_layer = BaseArrayLayer is { } c ? c : -1, layer_count = LayerCount is { } d ? d : -1,
    };
}

/// <summary>kor::Blit: an extent of -1 is the whole image.</summary>
public sealed record Blit
{
    public IVec3 SrcOffset { get; init; }
    public IVec3 SrcExtent { get; init; } = new(-1, -1, -1);
    public IVec3 DstOffset { get; init; }
    public IVec3 DstExtent { get; init; } = new(-1, -1, -1);
    public uint SrcBaseArrayLayer { get; init; }
    public uint DstBaseArrayLayer { get; init; }
    public uint LayerCount { get; init; } = 1;
    public uint SrcMipLevel { get; init; }
    public uint DstMipLevel { get; init; }
    public Filter Filtering { get; init; } = Filter.eNearest;

    internal unsafe KoralBlit Native
    {
        get
        {
            var b = new KoralBlit
            {
                src_base_array_layer = SrcBaseArrayLayer, dst_base_array_layer = DstBaseArrayLayer, layer_count = LayerCount,
                src_mip_level = SrcMipLevel, dst_mip_level = DstMipLevel, filtering = (uint)Filtering,
            };
            Set(b.src_offset, SrcOffset); Set(b.src_extent, SrcExtent); Set(b.dst_offset, DstOffset); Set(b.dst_extent, DstExtent);
            return b;
        }
    }

    internal static unsafe void Set(int* to, IVec3 v)
    {
        to[0] = v.X;
        to[1] = v.Y;
        to[2] = v.Z;
    }
}

/// <summary>kor::Resolve: an extent of -1 is the whole image.</summary>
public sealed record Resolve
{
    public IVec3 SrcOffset { get; init; }
    public IVec3 SrcExtent { get; init; } = new(-1, -1, -1);
    public IVec3 DstOffset { get; init; }
    public IVec3 DstExtent { get; init; } = new(-1, -1, -1);
    public uint SrcBaseArrayLayer { get; init; }
    public uint DstBaseArrayLayer { get; init; }
    public uint LayerCount { get; init; } = 1;
    public uint SrcMipLevel { get; init; }
    public uint DstMipLevel { get; init; }

    internal unsafe KoralResolveInfo Native
    {
        get
        {
            var r = new KoralResolveInfo
            {
                src_base_array_layer = SrcBaseArrayLayer, dst_base_array_layer = DstBaseArrayLayer, layer_count = LayerCount,
                src_mip_level = SrcMipLevel, dst_mip_level = DstMipLevel,
            };
            Blit.Set(r.src_offset, SrcOffset); Blit.Set(r.src_extent, SrcExtent); Blit.Set(r.dst_offset, DstOffset); Blit.Set(r.dst_extent, DstExtent);
            return r;
        }
    }
}

/// <summary>kor::Copy: between a buffer and an image; an extent of -1 is the whole image.</summary>
public sealed record Copy
{
    public ulong BufferOffset { get; init; }
    public ulong BufferRowLength { get; init; }
    public ulong BufferImageHeight { get; init; }
    public IVec3 ImageOffset { get; init; }
    public IVec3 ImageExtent { get; init; } = new(-1, -1, -1);
    public uint ImageBaseArrayLayer { get; init; }
    public uint ImageLayerCount { get; init; } = 1;
    public uint ImageMipLevel { get; init; }

    internal unsafe KoralCopy Native
    {
        get
        {
            var c = new KoralCopy
            {
                buffer_offset = BufferOffset, buffer_row_length = BufferRowLength, buffer_image_height = BufferImageHeight,
                image_base_array_layer = ImageBaseArrayLayer, image_layer_count = ImageLayerCount, image_mip_level = ImageMipLevel,
            };
            Blit.Set(c.image_offset, ImageOffset);
            Blit.Set(c.image_extent, ImageExtent);
            return c;
        }
    }
}

/// <summary>
/// kor::RenderInfo: where BeginRendering draws, and what happens to it — made from a framebuffer (or none,
/// for the screen) and changed by chained setters, as in C++.
/// </summary>
public sealed class RenderInfo
{
    private readonly Framebuffer? _framebuffer;
    private LoadOperation _colorLoad = LoadOperation.eClear, _depthLoad = LoadOperation.eClear, _stencilLoad = LoadOperation.eClear;
    private StoreOperation _colorStore = StoreOperation.eStore, _depthStore = StoreOperation.eStore, _stencilStore = StoreOperation.eStore;
    private readonly List<ClearColor?> _clearColors = [];
    private float? _clearDepth;
    private int? _clearStencil;

    public RenderInfo() { }
    public RenderInfo(Framebuffer framebuffer) => _framebuffer = framebuffer;

    /// <summary>A framebuffer is a RenderInfo onto it, as the C++ constructor is implicit.</summary>
    public static implicit operator RenderInfo(Framebuffer framebuffer) => new(framebuffer);

    public Framebuffer? Target => _framebuffer;
    public RenderInfo SetColorLoadOperation(LoadOperation op) { _colorLoad = op; return this; }
    public RenderInfo SetDepthLoadOperation(LoadOperation op) { _depthLoad = op; return this; }
    public RenderInfo SetStencilLoadOperation(LoadOperation op) { _stencilLoad = op; return this; }
    public RenderInfo SetColorStoreOperation(StoreOperation op) { _colorStore = op; return this; }
    public RenderInfo SetDepthStoreOperation(StoreOperation op) { _depthStore = op; return this; }
    public RenderInfo SetStencilStoreOperation(StoreOperation op) { _stencilStore = op; return this; }

    public RenderInfo SetClearColor(uint index, ClearColor color)
    {
        while (_clearColors.Count <= index) _clearColors.Add(null);
        _clearColors[(int)index] = color;
        return this;
    }

    public RenderInfo SetClearDepth(float depth) { _clearDepth = depth; return this; }
    public RenderInfo SetClearStencil(int stencil) { _clearStencil = stencil; return this; }

    public LoadOperation ColorLoadOperation => _colorLoad;
    public LoadOperation DepthLoadOperation => _depthLoad;
    public LoadOperation StencilLoadOperation => _stencilLoad;
    public StoreOperation ColorStoreOperation => _colorStore;
    public StoreOperation DepthStoreOperation => _depthStore;
    public StoreOperation StencilStoreOperation => _stencilStore;
    public float ClearDepth => _clearDepth ?? 1f;
    public int ClearStencil => _clearStencil ?? 0;

    internal unsafe void WithNative(NativeUse use)
    {
        var colors = new KoralClearColor[_clearColors.Count];
        for (var i = 0; i < colors.Length; ++i) colors[i] = _clearColors[i]?.Native ?? default;
        fixed (KoralClearColor* c = colors)
        {
            var info = new KoralRenderInfo
            {
                framebuffer = Resource.HandleOf(_framebuffer),
                color_load = (uint)_colorLoad, depth_load = (uint)_depthLoad, stencil_load = (uint)_stencilLoad,
                color_store = (uint)_colorStore, depth_store = (uint)_depthStore, stencil_store = (uint)_stencilStore,
                clear_colors = c, clear_color_count = (nuint)colors.Length,
                has_clear_depth = KoralNative.Bool(_clearDepth.HasValue), clear_depth = _clearDepth ?? 1f,
                has_clear_stencil = KoralNative.Bool(_clearStencil.HasValue), clear_stencil = _clearStencil ?? 0,
            };
            use(&info);
        }
    }

    internal unsafe delegate void NativeUse(KoralRenderInfo* info);
}
