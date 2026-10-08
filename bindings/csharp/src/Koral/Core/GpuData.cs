using System.Collections;
using System.Collections.Concurrent;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Koral;

/// <summary>
/// How values are laid out in GPU memory — what a struct, class or record becomes in a buffer.
/// <list type="bullet">
/// <item><see cref="C"/>: as the same struct is in C++ (and as a C# struct is already) — every value at its own
/// alignment, 4 for a float, a vector or a matrix. What vertex buffers want.</item>
/// <item><see cref="Std430"/>: a shader's storage buffers — vec2 aligned to 8, vec3 and vec4 to 16, a mat3's
/// columns padded to vec4s.</item>
/// <item><see cref="Std140"/>: uniform buffers — std430, and every struct aligned to 16.</item>
/// </list>
/// </summary>
public enum GpuPacking { C, Std430, Std140 }

/// <summary>How a value of one type sits in GPU memory: its size and alignment, and how it is written and read.</summary>
public sealed class GpuLayout
{
    public Type Type { get; }
    public int Size { get; }
    public int Alignment { get; }
    /// <summary>How far apart consecutive values sit in an array: the size, rounded up to the alignment.</summary>
    public int Stride => (Size + Alignment - 1) / Alignment * Alignment;

    internal readonly Action<Span<byte>, object> Write;
    internal readonly Func<ReadOnlySpan<byte>, object> Read;

    private GpuLayout(Type type, int size, int alignment, Action<Span<byte>, object> write, Func<ReadOnlySpan<byte>, object> read)
    {
        Type = type; Size = size; Alignment = alignment; Write = write; Read = read;
    }

    private static readonly ConcurrentDictionary<(Type, GpuPacking), GpuLayout> Cache = new();

    /// <summary>
    /// The layout of <typeparamref name="T"/>, derived: a number, vector, quaternion or matrix as Koral's own; an enum
    /// as its value; any other struct, class or record as its fields in declaration order, each at its alignment
    /// under <paramref name="packing"/>. Values are read back without a constructor (fields set directly), so
    /// records and init-only properties work.
    /// </summary>
    public static GpuLayout Of<T>(GpuPacking packing = GpuPacking.C) => Of(typeof(T), packing);
    public static GpuLayout Of(Type type, GpuPacking packing = GpuPacking.C) => Cache.GetOrAdd((type, packing), k => Make(k.Item1, k.Item2));

    /// <summary><paramref name="values"/> as bytes.</summary>
    public byte[] Pack(IEnumerable values)
    {
        var list = values.Cast<object>().ToList();
        var bytes = new byte[Stride * list.Count];
        for (int i = 0; i < list.Count; ++i) Write(bytes.AsSpan(i * Stride, Size), list[i]);
        return bytes;
    }

    /// <summary>The values in <paramref name="bytes"/>.</summary>
    public T[] Unpack<T>(ReadOnlySpan<byte> bytes)
    {
        var values = new T[bytes.Length / Stride];
        for (int i = 0; i < values.Length; ++i) values[i] = (T)Read(bytes.Slice(i * Stride, Size));
        return values;
    }

    private static GpuLayout Scalar<T>(int size) where T : unmanaged =>
        new(typeof(T), size, size, (s, v) => MemoryMarshal.Write(s, (T)v), s => MemoryMarshal.Read<T>(s));

    /// <summary>A Koral value type whose bytes are its C layout, aligned to 4 (C) or as a shader aligns it.</summary>
    private static GpuLayout Blittable<T>(int alignment) where T : unmanaged =>
        new(typeof(T), Unsafe.SizeOf<T>(), alignment, (s, v) => MemoryMarshal.Write(s, (T)v), s => MemoryMarshal.Read<T>(s));

    private static GpuLayout Make(Type type, GpuPacking packing)
    {
        bool std = packing != GpuPacking.C;
        int VecAlign(int n) => !std ? 4 : n == 2 ? 8 : 16;
        if (type == typeof(float)) return Scalar<float>(4);
        if (type == typeof(int)) return Scalar<int>(4);
        if (type == typeof(uint)) return Scalar<uint>(4);
        if (type == typeof(short)) return Scalar<short>(2);
        if (type == typeof(ushort)) return Scalar<ushort>(2);
        if (type == typeof(byte)) return Scalar<byte>(1);
        if (type == typeof(sbyte)) return Scalar<sbyte>(1);
        if (type == typeof(long)) return Scalar<long>(8);
        if (type == typeof(ulong)) return Scalar<ulong>(8);
        if (type == typeof(double)) return Scalar<double>(8);
        // C++'s bool is a byte; a shader's is 4.
        if (type == typeof(bool))
            return std ? new GpuLayout(type, 4, 4, (s, v) => MemoryMarshal.Write(s, (bool)v ? 1u : 0u), s => MemoryMarshal.Read<uint>(s) != 0)
                       : new GpuLayout(type, 1, 1, (s, v) => s[0] = (bool)v ? (byte)1 : (byte)0, s => s[0] != 0);
        if (type == typeof(Vec2)) return Blittable<Vec2>(VecAlign(2));
        if (type == typeof(Vec3)) return Blittable<Vec3>(VecAlign(3));
        if (type == typeof(Vec4)) return Blittable<Vec4>(VecAlign(4));
        if (type == typeof(Quat)) return Blittable<Quat>(VecAlign(4));
        if (type == typeof(IVec2)) return Blittable<IVec2>(VecAlign(2));
        if (type == typeof(IVec3)) return Blittable<IVec3>(VecAlign(3));
        if (type == typeof(IVec4)) return Blittable<IVec4>(VecAlign(4));
        if (type == typeof(UVec2)) return Blittable<UVec2>(VecAlign(2));
        if (type == typeof(UVec3)) return Blittable<UVec3>(VecAlign(3));
        if (type == typeof(UVec4)) return Blittable<UVec4>(VecAlign(4));
        if (type == typeof(Mat4)) return Blittable<Mat4>(VecAlign(4));
        if (type == typeof(Mat3))
            return !std ? Blittable<Mat3>(4) : new GpuLayout(type, 48, 16,
                (s, v) => { var m = (Mat3)v; for (int c = 0; c < 3; ++c) MemoryMarshal.Write(s[(c * 16)..], m[c]); },
                s => new Mat3(MemoryMarshal.Read<Vec3>(s), MemoryMarshal.Read<Vec3>(s[16..]), MemoryMarshal.Read<Vec3>(s[32..])));
        if (type.IsEnum)
        {
            var under = Of(Enum.GetUnderlyingType(type), packing);
            return new GpuLayout(type, under.Size, under.Alignment,
                (s, v) => under.Write(s, Convert.ChangeType(v, Enum.GetUnderlyingType(type))), s => Enum.ToObject(type, under.Read(s)));
        }
        if (type.IsArray || (typeof(IEnumerable).IsAssignableFrom(type) && type != typeof(string)))
            throw new ArgumentException($"{type.Name}: an array or list inside a struct has no fixed size in GPU memory; use separate fields, or a buffer of its own");
        return Struct(type, packing);
    }

    private static GpuLayout Struct(Type type, GpuPacking packing)
    {
        // Declaration order: metadata tokens grow in the order fields are declared (records' and auto-properties'
        // backing fields included).
        var fields = type.GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic).OrderBy(f => f.MetadataToken).ToArray();
        if (fields.Length == 0)
            throw new ArgumentException($"{type.Name} has no fields to lay out: a buffer takes numbers, vectors, matrices, and types made of them");
        int offset = 0, align = 1;
        var members = new List<(FieldInfo Field, GpuLayout Layout, int Offset)>();
        foreach (var f in fields)
        {
            GpuLayout layout;
            try { layout = Of(f.FieldType, packing); }
            catch (ArgumentException e) { throw new ArgumentException($"{type.Name}.{f.Name}: {e.Message}", e); }
            offset = (offset + layout.Alignment - 1) / layout.Alignment * layout.Alignment;
            align = Math.Max(align, layout.Alignment);
            members.Add((f, layout, offset));
            offset += layout.Size;
        }
        if (packing == GpuPacking.Std140) align = Math.Max(align, 16);
        int size = (offset + align - 1) / align * align;
        return new GpuLayout(type, size, align,
            (s, v) => { foreach (var m in members) m.Layout.Write(s.Slice(m.Offset, m.Layout.Size), m.Field.GetValue(v)!); },
            s =>
            {
                var o = RuntimeHelpers.GetUninitializedObject(type);   // a boxed struct, or a class: either takes SetValue
                foreach (var m in members) m.Field.SetValue(o, m.Layout.Read(s.Slice(m.Offset, m.Layout.Size)));
                return o;
            });
    }
}

/// <summary>Values as the GPU reads them, and back.</summary>
public static class Gpu
{
    /// <summary>
    /// The bytes of <paramref name="data"/>: a byte array as it is; a primitive array or a list of values — or one
    /// value: a vector, matrix, struct, class, record — laid out by <see cref="GpuLayout.Of(System.Type, GpuPacking)"/>.
    /// </summary>
    public static byte[] Bytes(object data, GpuPacking packing = GpuPacking.C)
    {
        switch (data)
        {
            case byte[] b: return b;
            case IEnumerable values when data is not string:
            {
                var list = values.Cast<object>().ToList();
                if (list.Count == 0) return [];
                var type = list[0].GetType();
                if (list.Any(v => v.GetType() != type))
                    throw new ArgumentException($"buffer data mixes types ({string.Join(", ", list.Select(v => v.GetType().Name).Distinct())}): give values of one type");
                return GpuLayout.Of(type, packing).Pack(list);
            }
            default:
                return GpuLayout.Of(data.GetType(), packing).Pack(new[] { data });
        }
    }

    /// <summary>Values of <typeparamref name="T"/> back from bytes laid out with <paramref name="packing"/>.</summary>
    public static T[] FromBytes<T>(ReadOnlySpan<byte> bytes, GpuPacking packing = GpuPacking.C) => GpuLayout.Of<T>(packing).Unpack<T>(bytes);

    // Vectors to primitive arrays and back.
    public static float[] ToFloatArray(this IEnumerable<Vec2> values) => values.SelectMany(v => new[] { v.X, v.Y }).ToArray();
    public static float[] ToFloatArray(this IEnumerable<Vec3> values) => values.SelectMany(v => new[] { v.X, v.Y, v.Z }).ToArray();
    public static float[] ToFloatArray(this IEnumerable<Vec4> values) => values.SelectMany(v => new[] { v.X, v.Y, v.Z, v.W }).ToArray();
    public static float[] ToFloatArray(this IEnumerable<Mat4> values) => MemoryMarshal.Cast<Mat4, float>(values.ToArray()).ToArray();
    public static int[] ToIntArray(this IEnumerable<IVec3> values) => values.SelectMany(v => new[] { v.X, v.Y, v.Z }).ToArray();
    public static Vec2[] ToVec2Array(this float[] values) => MemoryMarshal.Cast<float, Vec2>(values.AsSpan(0, values.Length / 2 * 2)).ToArray();
    public static Vec3[] ToVec3Array(this float[] values) => MemoryMarshal.Cast<float, Vec3>(values.AsSpan(0, values.Length / 3 * 3)).ToArray();
    public static Vec4[] ToVec4Array(this float[] values) => MemoryMarshal.Cast<float, Vec4>(values.AsSpan(0, values.Length / 4 * 4)).ToArray();
}
