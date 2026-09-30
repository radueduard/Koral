using System.Runtime.InteropServices;

namespace Koral;

// glm's integer vectors, which System.Numerics does not have. Float vectors and matrices are
// System.Numerics' own (Vector2/3/4, Matrix4x4): glm's vec2 is Vector2, and a Matrix4x4 holds the same
// sixteen floats, in the same order, as the glm::mat4 of the same transform.

/// <summary>glm::uvec2.</summary>
[StructLayout(LayoutKind.Sequential)]
public record struct UVec2(uint X, uint Y)
{
    public static implicit operator UVec2((uint X, uint Y) v) => new(v.X, v.Y);
}

/// <summary>glm::uvec3.</summary>
[StructLayout(LayoutKind.Sequential)]
public record struct UVec3(uint X, uint Y, uint Z)
{
    public static implicit operator UVec3((uint X, uint Y, uint Z) v) => new(v.X, v.Y, v.Z);
}

/// <summary>glm::uvec4.</summary>
[StructLayout(LayoutKind.Sequential)]
public record struct UVec4(uint X, uint Y, uint Z, uint W);

/// <summary>glm::ivec2.</summary>
[StructLayout(LayoutKind.Sequential)]
public record struct IVec2(int X, int Y);

/// <summary>glm::ivec3.</summary>
[StructLayout(LayoutKind.Sequential)]
public record struct IVec3(int X, int Y, int Z)
{
    public static implicit operator IVec3((int X, int Y, int Z) v) => new(v.X, v.Y, v.Z);
}

/// <summary>glm::ivec4.</summary>
[StructLayout(LayoutKind.Sequential)]
public record struct IVec4(int X, int Y, int Z, int W);
