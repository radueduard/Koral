using System.Runtime.InteropServices;

namespace Koral;

/// <summary>
/// kor::Transform: where something is — position, rotation and per-axis scale, applied scale first.
/// <c>parent * child</c> places a child inside its parent (exact unless the parent's scale is non-uniform and
/// the child is rotated against it). <c>new Transform()</c> is the identity.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public record struct Transform(Vec3 Position, Quat Rotation, Vec3 Scale)
{
    public Transform() : this(Vec3.Zero, Quat.Identity, Vec3.One) { }
    public Transform(Vec3 position) : this(position, Quat.Identity, Vec3.One) { }
    public Transform(Vec3 position, Quat rotation) : this(position, rotation, Vec3.One) { }
    /// <summary>From an affine matrix (shear is lost).</summary>
    public Transform(Mat4 m) : this()
    {
        KMath.Decompose(m, out var position, out var rotation, out var scale);
        Position = position;
        Rotation = rotation;
        Scale = scale;
    }

    public static Transform Identity => new();

    public readonly Mat4 Matrix => KMath.Compose(Position, Rotation, Scale);
    public readonly Mat4 InverseMatrix => KMath.Inverse(Matrix);
    public readonly Vec3 Forward => Rotation * Vec3.Forward;
    public readonly Vec3 Right => Rotation * Vec3.Right;
    public readonly Vec3 Up => Rotation * Vec3.Up;

    public readonly Vec3 TransformPoint(Vec3 p) => Position + Rotation * (p * Scale);
    public readonly Vec3 TransformDirection(Vec3 d) => Rotation * (d * Scale);
    public readonly Vec3 InverseTransformPoint(Vec3 p) => (KMath.Conjugate(Rotation) * (p - Position)) / Scale;
    public readonly Vec3 InverseTransformDirection(Vec3 d) => (KMath.Conjugate(Rotation) * d) / Scale;

    /// <summary>This transform turned to look at <paramref name="target"/>.</summary>
    public readonly Transform LookAt(Vec3 target, Vec3? up = null) => this with { Rotation = Quat.LookRotation(target - Position, up ?? Vec3.Up) };
    public readonly Transform Translate(Vec3 by) => this with { Position = Position + by };
    /// <summary>Rotated by <paramref name="q"/> in world space.</summary>
    public readonly Transform Rotate(Quat q) => this with { Rotation = KMath.Normalize(q * Rotation) };
    /// <summary>The transform that undoes this one (exact for uniform scale).</summary>
    public readonly Transform Inverse()
    {
        Quat inv = KMath.Conjugate(Rotation);
        Vec3 invScale = Vec3.One / Scale;
        return new Transform(inv * (-Position * invScale), inv, invScale);
    }

    public static Transform operator *(Transform parent, Transform child) =>
        new(parent.TransformPoint(child.Position), parent.Rotation * child.Rotation, parent.Scale * child.Scale);
}

public static partial class KMath
{
    /// <summary>Lerps position and scale, slerps rotation.</summary>
    public static Transform Lerp(Transform a, Transform b, float t) => new(Lerp(a.Position, b.Position, t), Slerp(a.Rotation, b.Rotation, t), Lerp(a.Scale, b.Scale, t));
}
