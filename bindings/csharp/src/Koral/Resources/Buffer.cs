using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Koral.Native;

namespace Koral;

/// <summary>
/// kor::Buffer: memory the GPU reads and writes, typed by what is put in it.
/// </summary>
/// <example>
/// <code>
/// var vertices = new Buffer.Builder&lt;Vertex&gt;()
///     .SetData(mesh.Vertices)
///     .SetUsage(Buffer.Usage.eVertex | Buffer.Usage.eStorage)
///     .SetType(Buffer.Type.eDeviceLocal)
///     .Build();
///
/// using (var mapping = uniforms.Map&lt;Camera&gt;())   // a MutableMapping, released at the end of the block
///     mapping[0] = camera;
/// </code>
/// </example>
/// <remarks>
/// In a file that also imports <c>System</c>, <c>Buffer</c> is ambiguous with <c>System.Buffer</c>; the
/// Koral package and <c>koral-dotnet</c>'s scripts import <c>Buffer = Koral.Buffer</c> for you, and a
/// project of your own does it with <c>&lt;Using Include="Koral.Buffer" Alias="Buffer" /&gt;</c>.
/// </remarks>
public sealed unsafe partial class Buffer : Resource
{
    /// <summary>kor::WholeSize: "the rest of it", for a count.</summary>
    public const ulong WholeSize = ulong.MaxValue;

    internal Buffer(IntPtr native) : base(native) { }

    /// <summary>
    /// kor::Buffer::Builder&lt;T&gt;: a buffer of <typeparamref name="T"/>s — its size is a count of them,
    /// and SetData gives its contents.
    /// </summary>
    public sealed class Builder<T> : Koral.Builder where T : unmanaged
    {
        public Builder() : base(KoralNative.koral_buffer_builder_new())
        {
            KoralNative.koral_buffer_builder_set_instance_count(Native, sizeof(T));
        }

        /// <summary>How many <typeparamref name="T"/>s it holds.</summary>
        public Builder<T> SetInstanceCount(long count)
        {
            KoralNative.koral_buffer_builder_set_instance_count(Native, count < 0 ? count : count * sizeof(T));
            return this;
        }

        /// <summary>Its contents, copied now; its size follows them.</summary>
        // Preferred for a collection expression — SetData([a, b]) — which every overload below could also take.
        [System.Runtime.CompilerServices.OverloadResolutionPriority(1)]
        public Builder<T> SetData(ReadOnlySpan<T> data)
        {
            fixed (T* pointer = data)
                KoralNative.koral_buffer_builder_set_data(Native, pointer, (ulong)(data.Length * sizeof(T)));
            return this;
        }

        public Builder<T> SetData(T[] data) => SetData(new ReadOnlySpan<T>(data));
        public Builder<T> SetData(List<T> data) => SetData(CollectionsMarshal.AsSpan(data));
        public Builder<T> SetData(IEnumerable<T> data) => SetData(data.ToArray());
        public Builder<T> SetData(in T value) => SetData(new ReadOnlySpan<T>(in value));

        /// <summary>
        /// Its contents laid out with <paramref name="packing"/> — std430 or std140 for a shader's blocks (C, the
        /// default, is what the struct already is); its size follows them.
        /// </summary>
        public Builder<T> SetData(IEnumerable<T> data, GpuPacking packing)
        {
            var bytes = Gpu.Bytes(data.ToList(), packing);
            fixed (byte* pointer = bytes)
                KoralNative.koral_buffer_builder_set_data(Native, pointer, (ulong)bytes.Length);
            return this;
        }

        /// <summary>SetDataView: in C#, the same as SetData — the data is copied, as it must outlive nothing.</summary>
        public Builder<T> SetDataView(ReadOnlySpan<T> data) => SetData(data);

        public Builder<T> SetUsage(Usage usage)
        {
            KoralNative.koral_buffer_builder_set_usage(Native, (uint)usage);
            return this;
        }

        public Builder<T> SetType(Type type)
        {
            KoralNative.koral_buffer_builder_set_type(Native, (uint)type);
            return this;
        }

        public Builder<T> SetIsPerFrame(bool value)
        {
            KoralNative.koral_buffer_builder_set_is_per_frame(Native, KoralNative.Bool(value));
            return this;
        }

        public Builder<T> SetSharedAcrossQueues(bool shared)
        {
            KoralNative.koral_buffer_builder_set_shared_across_queues(Native, KoralNative.Bool(shared));
            return this;
        }

        public Buffer Build() => Built<Buffer>(KoralNative.koral_buffer_builder_build(Native));
    }

    /// <summary>kor::Buffer::RawBuilder: a buffer sized in bytes.</summary>
    public sealed class RawBuilder : Koral.Builder
    {
        public RawBuilder() : base(KoralNative.koral_buffer_builder_new()) { }

        public RawBuilder SetRawSize(long bytes)
        {
            KoralNative.koral_buffer_builder_set_instance_count(Native, bytes);
            return this;
        }

        /// <summary>Room for <paramref name="count"/> values of <typeparamref name="T"/> laid out with <paramref name="packing"/>.</summary>
        public RawBuilder SetInstanceCount<T>(long count, GpuPacking packing = GpuPacking.C) => SetRawSize(count * GpuLayout.Of<T>(packing).Stride);

        /// <summary>
        /// Its contents, copied now; its size follows them: a byte array as it is, or values of any type — a primitive
        /// array, a list of vectors, of structs, classes or records, or one of them — laid out with <paramref name="packing"/>.
        /// </summary>
        public RawBuilder SetData(object data, GpuPacking packing = GpuPacking.C)
        {
            var bytes = Gpu.Bytes(data, packing);
            fixed (byte* pointer = bytes)
                KoralNative.koral_buffer_builder_set_data(Native, pointer, (ulong)bytes.Length);
            return this;
        }

        public RawBuilder SetUsage(Usage usage)
        {
            KoralNative.koral_buffer_builder_set_usage(Native, (uint)usage);
            return this;
        }

        public RawBuilder SetType(Type type)
        {
            KoralNative.koral_buffer_builder_set_type(Native, (uint)type);
            return this;
        }

        public RawBuilder SetIsPerFrame(bool value)
        {
            KoralNative.koral_buffer_builder_set_is_per_frame(Native, KoralNative.Bool(value));
            return this;
        }

        public RawBuilder SetSharedAcrossQueues(bool shared)
        {
            KoralNative.koral_buffer_builder_set_shared_across_queues(Native, KoralNative.Bool(shared));
            return this;
        }

        public Buffer Build() => Built<Buffer>(KoralNative.koral_buffer_builder_build(Native));
    }

    /// <summary>size(): in bytes.</summary>
    public ulong Size => Checked(KoralNative.koral_buffer_size(Handle));
    public Usage UsageFlags => (Usage)Checked(KoralNative.koral_buffer_usage_flags(Handle));
    public Type MemoryType => (Type)Checked(KoralNative.koral_buffer_memory_type(Handle));
    public bool IsHostVisible => Checked(KoralNative.koral_buffer_is_host_visible(Handle)).AsBool();
    public bool IsPerFrame => Checked(KoralNative.koral_buffer_is_per_frame(Handle)).AsBool();
    public bool IsSharedAcrossQueues => Checked(KoralNative.koral_buffer_is_shared_across_queues(Handle)).AsBool();
    public ulong DeviceAddress => Checked(KoralNative.koral_buffer_device_address(Handle));
    public uint CopyCount => Checked(KoralNative.koral_buffer_copy_count(Handle));

    private static TValue Checked<TValue>(TValue value)
    {
        KoralNative.Check();
        return value;
    }

    /// <summary>Read&lt;T&gt;(count, offset): <paramref name="count"/> Ts from the <paramref name="offset"/>th, or the rest.</summary>
    public T[] Read<T>(ulong count = WholeSize, ulong offset = 0) where T : unmanaged
    {
        if (count == WholeSize) count = Size / (ulong)sizeof(T) - offset;
        var values = new T[count];
        Read<T>(values, offset);
        return values;
    }

    /// <summary>Reads into <paramref name="into"/>, from the <paramref name="offset"/>th T.</summary>
    public void Read<T>(Span<T> into, ulong offset = 0) where T : unmanaged
    {
        fixed (T* pointer = into)
            KoralNative.Check(KoralNative.koral_buffer_read(Handle, pointer, (ulong)(into.Length * sizeof(T)), offset * (ulong)sizeof(T)));
    }

    /// <summary>
    /// ReadAsync&lt;T&gt;(count, offset): <see cref="Read{T}(ulong, ulong)"/> without making the CPU wait for the GPU:
    /// the copy out is started, and the await resumes, next frame or later, with what it read.
    /// </summary>
    /// <example><code>var contacts = await _contacts.ReadAsync&lt;Contact&gt;();   // no stall</code></example>
    public Task<T[]> ReadAsync<T>(ulong count = WholeSize, ulong offset = 0) where T : unmanaged
    {
        if (count == WholeSize) count = Size / (ulong)sizeof(T) - offset;
        var done = IntPtr.Zero;
        var readback = KoralNative.Check(KoralNative.koral_buffer_read_async(Handle, count * (ulong)sizeof(T), offset * (ulong)sizeof(T), &done));
        return Readback.Finish<T>(readback, new Token(done), count);
    }

    /// <summary>Copies what a readback holds into <paramref name="values"/>.</summary>
    internal static void ReadInto<T>(IntPtr readback, T[] values) where T : unmanaged
    {
        fixed (T* pointer = values) KoralNative.Check(KoralNative.koral_readback_read(readback, pointer));
    }

    /// <summary>Write(elements, offset): from the <paramref name="offset"/>th T.</summary>
    public void Write<T>(ReadOnlySpan<T> elements, ulong offset = 0) where T : unmanaged
    {
        fixed (T* pointer = elements)
            KoralNative.Check(KoralNative.koral_buffer_write(Handle, pointer, (ulong)(elements.Length * sizeof(T)), offset * (ulong)sizeof(T)));
    }

    public void Write<T>(T[] elements, ulong offset = 0) where T : unmanaged => Write(new ReadOnlySpan<T>(elements), offset);

    /// <summary>Writes values of any type (what <see cref="RawBuilder.SetData"/> takes) at byte <paramref name="byteOffset"/>.</summary>
    public void WriteValues(object data, ulong byteOffset = 0, GpuPacking packing = GpuPacking.C)
    {
        var bytes = Gpu.Bytes(data, packing);
        fixed (byte* pointer = bytes)
            KoralNative.Check(KoralNative.koral_buffer_write(Handle, pointer, (ulong)bytes.Length, byteOffset));
    }

    /// <summary>Values of any <typeparamref name="T"/> laid out with <paramref name="packing"/>, from the <paramref name="offset"/>th (or the rest).</summary>
    public T[] ReadAs<T>(GpuPacking packing = GpuPacking.C, ulong count = WholeSize, ulong offset = 0)
    {
        var stride = (ulong)GpuLayout.Of<T>(packing).Stride;
        if (count == WholeSize) count = Size / stride - offset;
        var bytes = new byte[count * stride];
        fixed (byte* pointer = bytes)
            KoralNative.Check(KoralNative.koral_buffer_read(Handle, pointer, (ulong)bytes.Length, offset * stride));
        return Gpu.FromBytes<T>(bytes, packing);
    }

    /// <summary><see cref="ReadAs{T}"/> without making the CPU wait for the GPU.</summary>
    public Task<T[]> ReadAsAsync<T>(GpuPacking packing = GpuPacking.C, ulong count = WholeSize, ulong offset = 0)
    {
        var stride = (ulong)GpuLayout.Of<T>(packing).Stride;
        if (count == WholeSize) count = Size / stride - offset;
        return ReadAsync<byte>(count * stride, offset * stride).ContinueWith(t => Gpu.FromBytes<T>(t.Result, packing),
            TaskContinuationOptions.ExecuteSynchronously);
    }

    public T ReadAt<T>(ulong index = 0) where T : unmanaged
    {
        T value = default;
        Read(new Span<T>(ref value), index);
        return value;
    }

    public void WriteAt<T>(ulong index, in T value) where T : unmanaged => Write(new ReadOnlySpan<T>(in value), index);

    /// <summary>
    /// Map&lt;T&gt;(count, offset): the buffer's memory, as Ts, until the mapping is disposed — a
    /// MutableMapping, whose writes reach a per-frame buffer's other copies. For reading only, see <see cref="MapConst{T}"/>.
    /// </summary>
    public MutableMapping<T> Map<T>(ulong count = WholeSize, ulong offset = 0) where T : unmanaged
    {
        if (count == WholeSize) count = Size / (ulong)sizeof(T) - offset;
        var mapping = KoralNative.koral_buffer_map(Handle, count * (ulong)sizeof(T), offset * (ulong)sizeof(T), 1);
        return new MutableMapping<T>(KoralNative.Check(mapping));
    }

    /// <summary>The const Map: a ConstMapping, several of which may be open at once.</summary>
    public ConstMapping<T> MapConst<T>(ulong count = WholeSize, ulong offset = 0) where T : unmanaged
    {
        if (count == WholeSize) count = Size / (ulong)sizeof(T) - offset;
        var mapping = KoralNative.koral_buffer_map(Handle, count * (ulong)sizeof(T), offset * (ulong)sizeof(T), 0);
        return new ConstMapping<T>(KoralNative.Check(mapping));
    }

    /// <summary>kor::Buffer::Slice: a range of a buffer, for a descriptor.</summary>
    public readonly record struct Slice(Buffer Buffer, long Offset = 0, long Size = 0);

    public static Slice SliceOf(Buffer buffer, long offset, long size = 0) => new(buffer, offset, size);

    /// <summary>kor::Buffer::ConstMapping&lt;T&gt;: released by <c>using</c>.</summary>
    public ref struct ConstMapping<T> where T : unmanaged
    {
        private IntPtr _native;
        private readonly T* _data;

        internal ConstMapping(IntPtr native)
        {
            _native = native;
            _data = (T*)KoralNative.koral_mapping_data(native);
            Count = (int)(KoralNative.koral_mapping_size(native) / (ulong)sizeof(T));
        }

        public int Count { get; }
        public readonly ref readonly T this[int index] => ref AsSpan()[index];
        public readonly ReadOnlySpan<T> AsSpan() => new(_data, Count);
        public readonly T[] Read(int offset = 0, int count = -1) => AsSpan().Slice(offset, count < 0 ? Count - offset : count).ToArray();

        public readonly void Invalidate(int offset = 0, int count = -1) =>
            KoralNative.Check(KoralNative.koral_mapping_invalidate(_native, (ulong)(offset * sizeof(T)), (ulong)((count < 0 ? Count - offset : count) * sizeof(T))));

        public void Dispose()
        {
            if (_native == IntPtr.Zero) return;
            KoralNative.koral_mapping_release(_native);
            _native = IntPtr.Zero;
        }
    }

    /// <summary>
    /// kor::Buffer::MutableMapping&lt;T&gt;: released by <c>using</c>. Setting an element, or Write, reaches
    /// every copy of a per-frame buffer; AsSpan is the memory itself, as in C++.
    /// </summary>
    public ref struct MutableMapping<T> where T : unmanaged
    {
        private IntPtr _native;
        private readonly T* _data;

        internal MutableMapping(IntPtr native)
        {
            _native = native;
            _data = (T*)KoralNative.koral_mapping_data(native);
            Count = (int)(KoralNative.koral_mapping_size(native) / (ulong)sizeof(T));
        }

        public int Count { get; }

        public readonly T this[int index]
        {
            get
            {
                if ((uint)index >= (uint)Count) throw new IndexOutOfRangeException();
                return _data[index];
            }
            set => Write(new ReadOnlySpan<T>(in value), index);
        }

        public readonly Span<T> AsSpan() => new(_data, Count);
        public readonly T[] Read(int offset = 0, int count = -1) => AsSpan().Slice(offset, count < 0 ? Count - offset : count).ToArray();

        public readonly void Write(ReadOnlySpan<T> elements, int offset = 0)
        {
            fixed (T* pointer = elements)
                KoralNative.Check(KoralNative.koral_mapping_write(_native, pointer, (ulong)(elements.Length * sizeof(T)), (ulong)(offset * sizeof(T))));
        }

        public readonly void Flush(int offset = 0, int count = -1) =>
            KoralNative.Check(KoralNative.koral_mapping_flush(_native, (ulong)(offset * sizeof(T)), (ulong)((count < 0 ? Count - offset : count) * sizeof(T))));

        public readonly void Invalidate(int offset = 0, int count = -1) =>
            KoralNative.Check(KoralNative.koral_mapping_invalidate(_native, (ulong)(offset * sizeof(T)), (ulong)((count < 0 ? Count - offset : count) * sizeof(T))));

        public void Dispose()
        {
            if (_native == IntPtr.Zero) return;
            KoralNative.koral_mapping_release(_native);
            _native = IntPtr.Zero;
        }
    }
}

/// <summary>The awaiting half of <see cref="Buffer.ReadAsync{T}"/>: C# can't await in an unsafe context.</summary>
internal static class Readback
{
    public static async Task<T[]> Finish<T>(IntPtr readback, Token done, ulong count) where T : unmanaged
    {
        try
        {
            await done;
            var values = new T[count];
            Buffer.ReadInto(readback, values);
            return values;
        }
        finally
        {
            KoralNative.koral_readback_destroy(readback);
        }
    }
}
