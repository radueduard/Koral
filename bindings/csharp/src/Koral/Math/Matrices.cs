using System.Globalization;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Koral;

/// <summary>
/// kor::Mat4: four column vectors, column after column in memory — the layout of the shaders, so a Mat4
/// uploads as is. <c>m[c]</c> is column c and <c>m[c, r]</c> the element in row r. Vectors are columns:
/// <c>projection * view * model * point</c> applies model first, as in C++ and GLSL. The default value is
/// <c>new Mat4()</c> is the identity, as in C++; <c>default(Mat4)</c> (and a new array's elements) are all zero.
/// </summary>
/// <remarks>
/// A System.Numerics.Matrix4x4 converts to and from it implicitly and keeps the same sixteen floats, which
/// is the same transform: Matrix4x4 uses row vectors, so its products read in the opposite order.
/// </remarks>
[StructLayout(LayoutKind.Sequential)]
public struct Mat4 : IEquatable<Mat4>
{
    public Vec4 C0, C1, C2, C3;

    /// <summary>The identity, as C++'s Mat4() (<c>default(Mat4)</c> is all zero instead).</summary>
    public Mat4() : this(1f) { }
    public Mat4(Vec4 c0, Vec4 c1, Vec4 c2, Vec4 c3) { C0 = c0; C1 = c1; C2 = c2; C3 = c3; }
    /// <summary><paramref name="diagonal"/> on the diagonal, zero elsewhere.</summary>
    public Mat4(float diagonal) : this(new Vec4(diagonal, 0, 0, 0), new Vec4(0, diagonal, 0, 0), new Vec4(0, 0, diagonal, 0), new Vec4(0, 0, 0, diagonal)) { }
    /// <summary>From 16 floats, column after column.</summary>
    public Mat4(ReadOnlySpan<float> columnMajor)
    {
        if (columnMajor.Length != 16) throw new ArgumentException("a Mat4 is 16 floats", nameof(columnMajor));
        this = MemoryMarshal.Read<Mat4>(MemoryMarshal.AsBytes(columnMajor));
    }
    /// <summary>The upper-left 3x3 of a Mat3, the rest the identity's.</summary>
    public Mat4(Mat3 m) : this(new Vec4(m.C0, 0), new Vec4(m.C1, 0), new Vec4(m.C2, 0), Vec4.UnitW) { }

    public static Mat4 Identity => new(1f);
    public static Mat4 Zero => default;

    public Vec4 this[int column]
    {
        readonly get => column switch { 0 => C0, 1 => C1, 2 => C2, 3 => C3, _ => throw new IndexOutOfRangeException() };
        set
        {
            switch (column)
            {
                case 0: C0 = value; break;
                case 1: C1 = value; break;
                case 2: C2 = value; break;
                case 3: C3 = value; break;
                default: throw new IndexOutOfRangeException();
            }
        }
    }
    public float this[int column, int row]
    {
        readonly get => this[column][row];
        set { var c = this[column]; c[row] = value; this[column] = c; }
    }
    public readonly Vec4 Row(int r) => new(C0[r], C1[r], C2[r], C3[r]);
    /// <summary>The sixteen floats, column after column.</summary>
    public readonly float[] ToArray() { var a = new float[16]; MemoryMarshal.Write(MemoryMarshal.AsBytes(a.AsSpan()), in this); return a; }

    /// <summary>a * b: b applied first.</summary>
    public static Mat4 operator *(Mat4 a, Mat4 b) => new(a * b.C0, a * b.C1, a * b.C2, a * b.C3);
    /// <summary>Matrix times column vector.</summary>
    public static Vec4 operator *(Mat4 m, Vec4 v) => m.C0 * v.X + m.C1 * v.Y + m.C2 * v.Z + m.C3 * v.W;
    /// <summary>Row vector times matrix: the same as Transpose(m) * v.</summary>
    public static Vec4 operator *(Vec4 v, Mat4 m) => new(KMath.Dot(v, m.C0), KMath.Dot(v, m.C1), KMath.Dot(v, m.C2), KMath.Dot(v, m.C3));
    public static Mat4 operator *(Mat4 m, float s) => new(m.C0 * s, m.C1 * s, m.C2 * s, m.C3 * s);
    public static Mat4 operator +(Mat4 a, Mat4 b) => new(a.C0 + b.C0, a.C1 + b.C1, a.C2 + b.C2, a.C3 + b.C3);
    public static Mat4 operator -(Mat4 a, Mat4 b) => new(a.C0 - b.C0, a.C1 - b.C1, a.C2 - b.C2, a.C3 - b.C3);
    public static bool operator ==(Mat4 a, Mat4 b) => a.C0 == b.C0 && a.C1 == b.C1 && a.C2 == b.C2 && a.C3 == b.C3;
    public static bool operator !=(Mat4 a, Mat4 b) => !(a == b);

    public static implicit operator System.Numerics.Matrix4x4(Mat4 m) => Unsafe.BitCast<Mat4, System.Numerics.Matrix4x4>(m);
    public static implicit operator Mat4(System.Numerics.Matrix4x4 m) => Unsafe.BitCast<System.Numerics.Matrix4x4, Mat4>(m);

    public readonly bool Equals(Mat4 o) => this == o;
    public override readonly bool Equals(object? o) => o is Mat4 m && Equals(m);
    public override readonly int GetHashCode() => HashCode.Combine(C0, C1, C2, C3);
    public override readonly string ToString() => $"[{Row(0)}; {Row(1)}; {Row(2)}; {Row(3)}]";
}

/// <summary>kor::Mat3: three columns, as <see cref="Mat4"/>.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct Mat3 : IEquatable<Mat3>
{
    public Vec3 C0, C1, C2;

    /// <summary>The identity, as C++'s Mat3().</summary>
    public Mat3() : this(1f) { }
    public Mat3(Vec3 c0, Vec3 c1, Vec3 c2) { C0 = c0; C1 = c1; C2 = c2; }
    public Mat3(float diagonal) : this(new Vec3(diagonal, 0, 0), new Vec3(0, diagonal, 0), new Vec3(0, 0, diagonal)) { }
    /// <summary>The upper-left 3x3 of a Mat4.</summary>
    public Mat3(Mat4 m) : this((Vec3)m.C0, (Vec3)m.C1, (Vec3)m.C2) { }

    public static Mat3 Identity => new(1f);
    public static Mat3 Zero => default;

    public Vec3 this[int column]
    {
        readonly get => column switch { 0 => C0, 1 => C1, 2 => C2, _ => throw new IndexOutOfRangeException() };
        set
        {
            switch (column)
            {
                case 0: C0 = value; break;
                case 1: C1 = value; break;
                case 2: C2 = value; break;
                default: throw new IndexOutOfRangeException();
            }
        }
    }
    public float this[int column, int row]
    {
        readonly get => this[column][row];
        set { var c = this[column]; c[row] = value; this[column] = c; }
    }
    public readonly Vec3 Row(int r) => new(C0[r], C1[r], C2[r]);

    public static Mat3 operator *(Mat3 a, Mat3 b) => new(a * b.C0, a * b.C1, a * b.C2);
    public static Vec3 operator *(Mat3 m, Vec3 v) => m.C0 * v.X + m.C1 * v.Y + m.C2 * v.Z;
    public static Vec3 operator *(Vec3 v, Mat3 m) => new(KMath.Dot(v, m.C0), KMath.Dot(v, m.C1), KMath.Dot(v, m.C2));
    public static Mat3 operator *(Mat3 m, float s) => new(m.C0 * s, m.C1 * s, m.C2 * s);
    public static bool operator ==(Mat3 a, Mat3 b) => a.C0 == b.C0 && a.C1 == b.C1 && a.C2 == b.C2;
    public static bool operator !=(Mat3 a, Mat3 b) => !(a == b);

    public readonly bool Equals(Mat3 o) => this == o;
    public override readonly bool Equals(object? o) => o is Mat3 m && Equals(m);
    public override readonly int GetHashCode() => HashCode.Combine(C0, C1, C2);
    public override readonly string ToString() => $"[{Row(0)}; {Row(1)}; {Row(2)}]";
}

/// <summary>
/// kor::Quat: a rotation, stored X, Y, Z, W. <c>a * b</c> rotates by b, then by a; <c>q * v</c> rotates a
/// vector. <c>new Quat()</c> is the identity; <c>default(Quat)</c> is all zero, which is no rotation at all.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct Quat : IEquatable<Quat>
{
    public float X, Y, Z, W;

    /// <summary>The identity, as C++'s Quat() (<c>default(Quat)</c> is all zero instead).</summary>
    public Quat() { W = 1f; }
    public Quat(float x, float y, float z, float w) { X = x; Y = y; Z = z; W = w; }
    public Quat(Vec3 xyz, float w) { X = xyz.X; Y = xyz.Y; Z = xyz.Z; W = w; }

    public readonly Vec3 XYZ => new(X, Y, Z);
    public static Quat Identity => new(0f, 0f, 0f, 1f);

    /// <summary><paramref name="angle"/> radians counter-clockwise about <paramref name="axis"/> (normalised for you).</summary>
    public static Quat AngleAxis(float angle, Vec3 axis)
    {
        float s = MathF.Sin(angle * 0.5f);
        return new Quat(KMath.Normalize(axis) * s, MathF.Cos(angle * 0.5f));
    }

    /// <summary>From Euler angles (pitch, yaw, roll) in radians.</summary>
    public static Quat FromEuler(Vec3 euler)
    {
        Vec3 h = euler * 0.5f;
        Vec3 c = new(MathF.Cos(h.X), MathF.Cos(h.Y), MathF.Cos(h.Z)), s = new(MathF.Sin(h.X), MathF.Sin(h.Y), MathF.Sin(h.Z));
        return new Quat(s.X * c.Y * c.Z - c.X * s.Y * s.Z,
                        c.X * s.Y * c.Z + s.X * c.Y * s.Z,
                        c.X * c.Y * s.Z - s.X * s.Y * c.Z,
                        c.X * c.Y * c.Z + s.X * s.Y * s.Z);
    }

    /// <summary>The rotation of a rotation matrix (orthonormal columns).</summary>
    public static Quat FromMatrix(Mat3 m)
    {
        float fourXSq = m[0, 0] - m[1, 1] - m[2, 2], fourYSq = m[1, 1] - m[0, 0] - m[2, 2];
        float fourZSq = m[2, 2] - m[0, 0] - m[1, 1], fourWSq = m[0, 0] + m[1, 1] + m[2, 2];
        int biggest = 0;
        float fourBiggestSq = fourWSq;
        if (fourXSq > fourBiggestSq) { fourBiggestSq = fourXSq; biggest = 1; }
        if (fourYSq > fourBiggestSq) { fourBiggestSq = fourYSq; biggest = 2; }
        if (fourZSq > fourBiggestSq) { fourBiggestSq = fourZSq; biggest = 3; }
        float big = MathF.Sqrt(fourBiggestSq + 1f) * 0.5f;
        float mult = 0.25f / big;
        return biggest switch
        {
            0 => new Quat((m[1, 2] - m[2, 1]) * mult, (m[2, 0] - m[0, 2]) * mult, (m[0, 1] - m[1, 0]) * mult, big),
            1 => new Quat(big, (m[0, 1] + m[1, 0]) * mult, (m[2, 0] + m[0, 2]) * mult, (m[1, 2] - m[2, 1]) * mult),
            2 => new Quat((m[0, 1] + m[1, 0]) * mult, big, (m[1, 2] + m[2, 1]) * mult, (m[2, 0] - m[0, 2]) * mult),
            _ => new Quat((m[2, 0] + m[0, 2]) * mult, (m[1, 2] + m[2, 1]) * mult, big, (m[0, 1] - m[1, 0]) * mult),
        };
    }
    public static Quat FromMatrix(Mat4 m) => FromMatrix(new Mat3(m));

    /// <summary>The rotation that turns -Z (forward) toward <paramref name="direction"/> with +Y toward <paramref name="up"/>.</summary>
    public static Quat LookRotation(Vec3 direction, Vec3? up = null)
    {
        Vec3 u = up ?? Vec3.Up;
        var m = Mat3.Identity;
        m.C2 = -KMath.Normalize(direction);
        Vec3 right = KMath.Cross(u, m.C2);
        m.C0 = right * (1f / MathF.Sqrt(MathF.Max(1e-5f, KMath.Dot(right, right))));
        m.C1 = KMath.Cross(m.C2, m.C0);
        return FromMatrix(m);
    }

    /// <summary>The shortest rotation turning direction <paramref name="from"/> onto <paramref name="to"/>.</summary>
    public static Quat FromTo(Vec3 from, Vec3 to)
    {
        Vec3 f = KMath.Normalize(from), t = KMath.Normalize(to);
        float d = KMath.Dot(f, t);
        if (d >= 1f - 1e-6f) return Identity;
        if (d <= -1f + 1e-6f) return new Quat(KMath.AnyPerpendicular(f), 0f);
        float s = MathF.Sqrt((1f + d) * 2f);
        return new Quat(KMath.Cross(f, t) * (1f / s), s * 0.5f);
    }

    public static Quat operator *(Quat p, Quat q) => new(
        p.W * q.X + p.X * q.W + p.Y * q.Z - p.Z * q.Y,
        p.W * q.Y + p.Y * q.W + p.Z * q.X - p.X * q.Z,
        p.W * q.Z + p.Z * q.W + p.X * q.Y - p.Y * q.X,
        p.W * q.W - p.X * q.X - p.Y * q.Y - p.Z * q.Z);
    /// <summary><paramref name="v"/> rotated by <paramref name="q"/> (unit length).</summary>
    public static Vec3 operator *(Quat q, Vec3 v)
    {
        Vec3 u = q.XYZ;
        Vec3 uv = KMath.Cross(u, v), uuv = KMath.Cross(u, uv);
        return v + (uv * q.W + uuv) * 2f;
    }
    public static Quat operator *(Quat q, float s) => new(q.X * s, q.Y * s, q.Z * s, q.W * s);
    public static Quat operator *(float s, Quat q) => q * s;
    public static Quat operator +(Quat a, Quat b) => new(a.X + b.X, a.Y + b.Y, a.Z + b.Z, a.W + b.W);
    public static Quat operator -(Quat a, Quat b) => new(a.X - b.X, a.Y - b.Y, a.Z - b.Z, a.W - b.W);
    public static Quat operator -(Quat q) => new(-q.X, -q.Y, -q.Z, -q.W);
    public static bool operator ==(Quat a, Quat b) => a.X == b.X && a.Y == b.Y && a.Z == b.Z && a.W == b.W;
    public static bool operator !=(Quat a, Quat b) => !(a == b);

    public static implicit operator System.Numerics.Quaternion(Quat q) => new(q.X, q.Y, q.Z, q.W);
    public static implicit operator Quat(System.Numerics.Quaternion q) => new(q.X, q.Y, q.Z, q.W);

    public readonly bool Equals(Quat o) => this == o;
    public override readonly bool Equals(object? o) => o is Quat q && Equals(q);
    public override readonly int GetHashCode() => HashCode.Combine(X, Y, Z, W);
    public override readonly string ToString() => string.Create(CultureInfo.InvariantCulture, $"Quat({X}, {Y}, {Z}, {W})");
}
