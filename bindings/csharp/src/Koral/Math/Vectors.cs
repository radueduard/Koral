using System.Globalization;
using System.Runtime.InteropServices;

namespace Koral;

// kor::Vec2/3/4 and their integer forms (kmath/vector.h): the same names, layouts and meanings. Arithmetic
// is component-wise; the functions over vectors (Dot, Cross, Normalize, Lerp, ...) are KMath's, as they are
// free functions in C++ — `using static Koral.KMath;` and they read the same. Each float vector converts
// implicitly to and from System.Numerics' type of the same size.

/// <summary>kor::Vec2.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct Vec2 : IEquatable<Vec2>
{
    public float X, Y;

    public Vec2(float x, float y) { X = x; Y = y; }
    public Vec2(float s) { X = s; Y = s; }

    public float this[int i]
    {
        readonly get => i switch { 0 => X, 1 => Y, _ => throw new IndexOutOfRangeException() };
        set { if (i == 0) X = value; else if (i == 1) Y = value; else throw new IndexOutOfRangeException(); }
    }

    public static Vec2 Zero => default;
    public static Vec2 One => new(1f);
    public static Vec2 UnitX => new(1f, 0f);
    public static Vec2 UnitY => new(0f, 1f);

    public static Vec2 operator +(Vec2 a, Vec2 b) => new(a.X + b.X, a.Y + b.Y);
    public static Vec2 operator -(Vec2 a, Vec2 b) => new(a.X - b.X, a.Y - b.Y);
    public static Vec2 operator *(Vec2 a, Vec2 b) => new(a.X * b.X, a.Y * b.Y);
    public static Vec2 operator /(Vec2 a, Vec2 b) => new(a.X / b.X, a.Y / b.Y);
    public static Vec2 operator *(Vec2 a, float s) => new(a.X * s, a.Y * s);
    public static Vec2 operator *(float s, Vec2 a) => new(s * a.X, s * a.Y);
    public static Vec2 operator /(Vec2 a, float s) => new(a.X / s, a.Y / s);
    public static Vec2 operator +(Vec2 a, float s) => new(a.X + s, a.Y + s);
    public static Vec2 operator -(Vec2 a, float s) => new(a.X - s, a.Y - s);
    public static Vec2 operator -(Vec2 a) => new(-a.X, -a.Y);
    public static bool operator ==(Vec2 a, Vec2 b) => a.X == b.X && a.Y == b.Y;
    public static bool operator !=(Vec2 a, Vec2 b) => !(a == b);

    public static implicit operator System.Numerics.Vector2(Vec2 v) => new(v.X, v.Y);
    public static implicit operator Vec2(System.Numerics.Vector2 v) => new(v.X, v.Y);
    public static implicit operator Vec2((float X, float Y) v) => new(v.X, v.Y);
    public static explicit operator Vec2(Vec3 v) => new(v.X, v.Y);
    public static explicit operator Vec2(Vec4 v) => new(v.X, v.Y);
    public static explicit operator Vec2(IVec2 v) => new(v.X, v.Y);
    public static explicit operator Vec2(UVec2 v) => new(v.X, v.Y);

    public readonly void Deconstruct(out float x, out float y) { x = X; y = Y; }
    public readonly bool Equals(Vec2 o) => this == o;
    public override readonly bool Equals(object? o) => o is Vec2 v && Equals(v);
    public override readonly int GetHashCode() => HashCode.Combine(X, Y);
    public override readonly string ToString() => string.Create(CultureInfo.InvariantCulture, $"({X}, {Y})");
}

/// <summary>kor::Vec3. Koral's world is right-handed, +Y up, and a camera looks down -Z.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct Vec3 : IEquatable<Vec3>
{
    public float X, Y, Z;

    public Vec3(float x, float y, float z) { X = x; Y = y; Z = z; }
    public Vec3(float s) { X = s; Y = s; Z = s; }
    public Vec3(Vec2 xy, float z) { X = xy.X; Y = xy.Y; Z = z; }

    public float this[int i]
    {
        readonly get => i switch { 0 => X, 1 => Y, 2 => Z, _ => throw new IndexOutOfRangeException() };
        set
        {
            switch (i)
            {
                case 0: X = value; break;
                case 1: Y = value; break;
                case 2: Z = value; break;
                default: throw new IndexOutOfRangeException();
            }
        }
    }

    public readonly Vec2 XY => new(X, Y);

    public static Vec3 Zero => default;
    public static Vec3 One => new(1f);
    public static Vec3 UnitX => new(1f, 0f, 0f);
    public static Vec3 UnitY => new(0f, 1f, 0f);
    public static Vec3 UnitZ => new(0f, 0f, 1f);
    public static Vec3 Up => new(0f, 1f, 0f);
    public static Vec3 Down => new(0f, -1f, 0f);
    public static Vec3 Right => new(1f, 0f, 0f);
    public static Vec3 Left => new(-1f, 0f, 0f);
    public static Vec3 Forward => new(0f, 0f, -1f);
    public static Vec3 Back => new(0f, 0f, 1f);

    public static Vec3 operator +(Vec3 a, Vec3 b) => new(a.X + b.X, a.Y + b.Y, a.Z + b.Z);
    public static Vec3 operator -(Vec3 a, Vec3 b) => new(a.X - b.X, a.Y - b.Y, a.Z - b.Z);
    public static Vec3 operator *(Vec3 a, Vec3 b) => new(a.X * b.X, a.Y * b.Y, a.Z * b.Z);
    public static Vec3 operator /(Vec3 a, Vec3 b) => new(a.X / b.X, a.Y / b.Y, a.Z / b.Z);
    public static Vec3 operator *(Vec3 a, float s) => new(a.X * s, a.Y * s, a.Z * s);
    public static Vec3 operator *(float s, Vec3 a) => new(s * a.X, s * a.Y, s * a.Z);
    public static Vec3 operator /(Vec3 a, float s) => new(a.X / s, a.Y / s, a.Z / s);
    public static Vec3 operator +(Vec3 a, float s) => new(a.X + s, a.Y + s, a.Z + s);
    public static Vec3 operator -(Vec3 a, float s) => new(a.X - s, a.Y - s, a.Z - s);
    public static Vec3 operator -(Vec3 a) => new(-a.X, -a.Y, -a.Z);
    public static bool operator ==(Vec3 a, Vec3 b) => a.X == b.X && a.Y == b.Y && a.Z == b.Z;
    public static bool operator !=(Vec3 a, Vec3 b) => !(a == b);

    public static implicit operator System.Numerics.Vector3(Vec3 v) => new(v.X, v.Y, v.Z);
    public static implicit operator Vec3(System.Numerics.Vector3 v) => new(v.X, v.Y, v.Z);
    public static implicit operator Vec3((float X, float Y, float Z) v) => new(v.X, v.Y, v.Z);
    public static explicit operator Vec3(Vec4 v) => new(v.X, v.Y, v.Z);
    public static explicit operator Vec3(IVec3 v) => new(v.X, v.Y, v.Z);
    public static explicit operator Vec3(UVec3 v) => new(v.X, v.Y, v.Z);

    public readonly void Deconstruct(out float x, out float y, out float z) { x = X; y = Y; z = Z; }
    public readonly bool Equals(Vec3 o) => this == o;
    public override readonly bool Equals(object? o) => o is Vec3 v && Equals(v);
    public override readonly int GetHashCode() => HashCode.Combine(X, Y, Z);
    public override readonly string ToString() => string.Create(CultureInfo.InvariantCulture, $"({X}, {Y}, {Z})");
}

/// <summary>kor::Vec4.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct Vec4 : IEquatable<Vec4>
{
    public float X, Y, Z, W;

    public Vec4(float x, float y, float z, float w) { X = x; Y = y; Z = z; W = w; }
    public Vec4(float s) { X = s; Y = s; Z = s; W = s; }
    public Vec4(Vec3 xyz, float w) { X = xyz.X; Y = xyz.Y; Z = xyz.Z; W = w; }
    public Vec4(Vec2 xy, float z, float w) { X = xy.X; Y = xy.Y; Z = z; W = w; }
    public Vec4(Vec2 xy, Vec2 zw) { X = xy.X; Y = xy.Y; Z = zw.X; W = zw.Y; }

    public float this[int i]
    {
        readonly get => i switch { 0 => X, 1 => Y, 2 => Z, 3 => W, _ => throw new IndexOutOfRangeException() };
        set
        {
            switch (i)
            {
                case 0: X = value; break;
                case 1: Y = value; break;
                case 2: Z = value; break;
                case 3: W = value; break;
                default: throw new IndexOutOfRangeException();
            }
        }
    }

    public readonly Vec2 XY => new(X, Y);
    public readonly Vec3 XYZ => new(X, Y, Z);

    public static Vec4 Zero => default;
    public static Vec4 One => new(1f);
    public static Vec4 UnitX => new(1f, 0f, 0f, 0f);
    public static Vec4 UnitY => new(0f, 1f, 0f, 0f);
    public static Vec4 UnitZ => new(0f, 0f, 1f, 0f);
    public static Vec4 UnitW => new(0f, 0f, 0f, 1f);

    public static Vec4 operator +(Vec4 a, Vec4 b) => new(a.X + b.X, a.Y + b.Y, a.Z + b.Z, a.W + b.W);
    public static Vec4 operator -(Vec4 a, Vec4 b) => new(a.X - b.X, a.Y - b.Y, a.Z - b.Z, a.W - b.W);
    public static Vec4 operator *(Vec4 a, Vec4 b) => new(a.X * b.X, a.Y * b.Y, a.Z * b.Z, a.W * b.W);
    public static Vec4 operator /(Vec4 a, Vec4 b) => new(a.X / b.X, a.Y / b.Y, a.Z / b.Z, a.W / b.W);
    public static Vec4 operator *(Vec4 a, float s) => new(a.X * s, a.Y * s, a.Z * s, a.W * s);
    public static Vec4 operator *(float s, Vec4 a) => new(s * a.X, s * a.Y, s * a.Z, s * a.W);
    public static Vec4 operator /(Vec4 a, float s) => new(a.X / s, a.Y / s, a.Z / s, a.W / s);
    public static Vec4 operator +(Vec4 a, float s) => new(a.X + s, a.Y + s, a.Z + s, a.W + s);
    public static Vec4 operator -(Vec4 a, float s) => new(a.X - s, a.Y - s, a.Z - s, a.W - s);
    public static Vec4 operator -(Vec4 a) => new(-a.X, -a.Y, -a.Z, -a.W);
    public static bool operator ==(Vec4 a, Vec4 b) => a.X == b.X && a.Y == b.Y && a.Z == b.Z && a.W == b.W;
    public static bool operator !=(Vec4 a, Vec4 b) => !(a == b);

    public static implicit operator System.Numerics.Vector4(Vec4 v) => new(v.X, v.Y, v.Z, v.W);
    public static implicit operator Vec4(System.Numerics.Vector4 v) => new(v.X, v.Y, v.Z, v.W);
    public static implicit operator Vec4((float X, float Y, float Z, float W) v) => new(v.X, v.Y, v.Z, v.W);
    public static explicit operator Vec4(IVec4 v) => new(v.X, v.Y, v.Z, v.W);
    public static explicit operator Vec4(UVec4 v) => new(v.X, v.Y, v.Z, v.W);

    public readonly void Deconstruct(out float x, out float y, out float z, out float w) { x = X; y = Y; z = Z; w = W; }
    public readonly bool Equals(Vec4 o) => this == o;
    public override readonly bool Equals(object? o) => o is Vec4 v && Equals(v);
    public override readonly int GetHashCode() => HashCode.Combine(X, Y, Z, W);
    public override readonly string ToString() => string.Create(CultureInfo.InvariantCulture, $"({X}, {Y}, {Z}, {W})");
}

/// <summary>kor::IVec2.</summary>
[StructLayout(LayoutKind.Sequential)]
public record struct IVec2(int X, int Y)
{
    public static IVec2 operator +(IVec2 a, IVec2 b) => new(a.X + b.X, a.Y + b.Y);
    public static IVec2 operator -(IVec2 a, IVec2 b) => new(a.X - b.X, a.Y - b.Y);
    public static IVec2 operator *(IVec2 a, int s) => new(a.X * s, a.Y * s);
    public static IVec2 operator -(IVec2 a) => new(-a.X, -a.Y);
    public static implicit operator IVec2((int X, int Y) v) => new(v.X, v.Y);
    /// <summary>Truncated toward zero, as C++'s conversion.</summary>
    public static explicit operator IVec2(Vec2 v) => new((int)v.X, (int)v.Y);
}

/// <summary>kor::IVec3.</summary>
[StructLayout(LayoutKind.Sequential)]
public record struct IVec3(int X, int Y, int Z)
{
    public static IVec3 operator +(IVec3 a, IVec3 b) => new(a.X + b.X, a.Y + b.Y, a.Z + b.Z);
    public static IVec3 operator -(IVec3 a, IVec3 b) => new(a.X - b.X, a.Y - b.Y, a.Z - b.Z);
    public static IVec3 operator *(IVec3 a, int s) => new(a.X * s, a.Y * s, a.Z * s);
    public static IVec3 operator -(IVec3 a) => new(-a.X, -a.Y, -a.Z);
    public static implicit operator IVec3((int X, int Y, int Z) v) => new(v.X, v.Y, v.Z);
    public static explicit operator IVec3(Vec3 v) => new((int)v.X, (int)v.Y, (int)v.Z);
    public static explicit operator IVec3(UVec3 v) => new((int)v.X, (int)v.Y, (int)v.Z);
}

/// <summary>kor::IVec4.</summary>
[StructLayout(LayoutKind.Sequential)]
public record struct IVec4(int X, int Y, int Z, int W)
{
    public static IVec4 operator +(IVec4 a, IVec4 b) => new(a.X + b.X, a.Y + b.Y, a.Z + b.Z, a.W + b.W);
    public static IVec4 operator -(IVec4 a, IVec4 b) => new(a.X - b.X, a.Y - b.Y, a.Z - b.Z, a.W - b.W);
    public static implicit operator IVec4((int X, int Y, int Z, int W) v) => new(v.X, v.Y, v.Z, v.W);
    public static explicit operator IVec4(Vec4 v) => new((int)v.X, (int)v.Y, (int)v.Z, (int)v.W);
}

/// <summary>kor::UVec2.</summary>
[StructLayout(LayoutKind.Sequential)]
public record struct UVec2(uint X, uint Y)
{
    public static UVec2 operator +(UVec2 a, UVec2 b) => new(a.X + b.X, a.Y + b.Y);
    public static UVec2 operator -(UVec2 a, UVec2 b) => new(a.X - b.X, a.Y - b.Y);
    public static UVec2 operator *(UVec2 a, uint s) => new(a.X * s, a.Y * s);
    public static UVec2 operator >>(UVec2 a, int s) => new(a.X >> s, a.Y >> s);
    public static UVec2 operator <<(UVec2 a, int s) => new(a.X << s, a.Y << s);
    public static implicit operator UVec2((uint X, uint Y) v) => new(v.X, v.Y);
    public static explicit operator UVec2(Vec2 v) => new((uint)v.X, (uint)v.Y);
}

/// <summary>kor::UVec3.</summary>
[StructLayout(LayoutKind.Sequential)]
public record struct UVec3(uint X, uint Y, uint Z)
{
    public static UVec3 operator +(UVec3 a, UVec3 b) => new(a.X + b.X, a.Y + b.Y, a.Z + b.Z);
    public static UVec3 operator -(UVec3 a, UVec3 b) => new(a.X - b.X, a.Y - b.Y, a.Z - b.Z);
    public static UVec3 operator *(UVec3 a, uint s) => new(a.X * s, a.Y * s, a.Z * s);
    public static implicit operator UVec3((uint X, uint Y, uint Z) v) => new(v.X, v.Y, v.Z);
    public static explicit operator UVec3(Vec3 v) => new((uint)v.X, (uint)v.Y, (uint)v.Z);
    public static explicit operator UVec3(IVec3 v) => new((uint)v.X, (uint)v.Y, (uint)v.Z);
}

/// <summary>kor::UVec4.</summary>
[StructLayout(LayoutKind.Sequential)]
public record struct UVec4(uint X, uint Y, uint Z, uint W)
{
    public static UVec4 operator +(UVec4 a, UVec4 b) => new(a.X + b.X, a.Y + b.Y, a.Z + b.Z, a.W + b.W);
    public static implicit operator UVec4((uint X, uint Y, uint Z, uint W) v) => new(v.X, v.Y, v.Z, v.W);
    public static explicit operator UVec4(Vec4 v) => new((uint)v.X, (uint)v.Y, (uint)v.Z, (uint)v.W);
}
