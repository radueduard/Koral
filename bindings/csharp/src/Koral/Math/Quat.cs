using System.Globalization;
using System.Runtime.InteropServices;

namespace Koral;

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
