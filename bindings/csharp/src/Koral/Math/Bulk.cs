using Koral.Native;

namespace Koral;

/// <summary>
/// kor::bulk: the same operations over many values at once, vectorised in Koral's native code (SSE2/NEON).
/// Each result equals doing it one value at a time with KMath's functions. <c>output</c> may be the input
/// itself, and must be at least as long.
/// </summary>
public static unsafe class Bulk
{
    private static void Fits(int input, int output)
    {
        if (output < input) throw new ArgumentException($"the output holds {output}, fewer than the {input} given");
    }

    public static void TransformPoints(Mat4 m, ReadOnlySpan<Vec3> points, Span<Vec3> output)
    {
        Fits(points.Length, output.Length);
        fixed (Vec3* i = points) fixed (Vec3* o = output) KoralMathNative.koral_bulk_transform_points(m, i, o, (nuint)points.Length);
    }
    public static void TransformDirections(Mat4 m, ReadOnlySpan<Vec3> directions, Span<Vec3> output)
    {
        Fits(directions.Length, output.Length);
        fixed (Vec3* i = directions) fixed (Vec3* o = output) KoralMathNative.koral_bulk_transform_directions(m, i, o, (nuint)directions.Length);
    }
    public static void Transform(Mat4 m, ReadOnlySpan<Vec4> vectors, Span<Vec4> output)
    {
        Fits(vectors.Length, output.Length);
        fixed (Vec4* i = vectors) fixed (Vec4* o = output) KoralMathNative.koral_bulk_transform(m, i, o, (nuint)vectors.Length);
    }
    /// <summary>output[i] = a[i] * b[i].</summary>
    public static void Multiply(ReadOnlySpan<Mat4> a, ReadOnlySpan<Mat4> b, Span<Mat4> output)
    {
        int n = Math.Min(a.Length, b.Length);
        Fits(n, output.Length);
        fixed (Mat4* pa = a) fixed (Mat4* pb = b) fixed (Mat4* o = output) KoralMathNative.koral_bulk_multiply(pa, pb, o, (nuint)n);
    }
    /// <summary>output[i] = parent * children[i].</summary>
    public static void Multiply(Mat4 parent, ReadOnlySpan<Mat4> children, Span<Mat4> output)
    {
        Fits(children.Length, output.Length);
        fixed (Mat4* c = children) fixed (Mat4* o = output) KoralMathNative.koral_bulk_multiply_parent(parent, c, o, (nuint)children.Length);
    }
    public static void TransformAabbs(Mat4 m, ReadOnlySpan<Aabb> boxes, Span<Aabb> output)
    {
        Fits(boxes.Length, output.Length);
        fixed (Aabb* i = boxes) fixed (Aabb* o = output) KoralMathNative.koral_bulk_transform_aabbs(m, i, o, (nuint)boxes.Length);
    }
    public static Aabb Bounds(ReadOnlySpan<Vec3> points)
    {
        fixed (Vec3* p = points) return KoralMathNative.koral_bulk_bounds(p, (nuint)points.Length);
    }
    /// <summary>visible[i] = whether spheres[i] is (at least partly) in the frustum; returns how many are.</summary>
    public static int Cull(Frustum frustum, ReadOnlySpan<Sphere> spheres, Span<bool> visible)
    {
        Fits(spheres.Length, visible.Length);
        fixed (Sphere* s = spheres) fixed (bool* v = visible) return (int)KoralMathNative.koral_bulk_cull_spheres(frustum, s, (byte*)v, (nuint)spheres.Length);
    }
    public static int Cull(Frustum frustum, ReadOnlySpan<Aabb> boxes, Span<bool> visible)
    {
        Fits(boxes.Length, visible.Length);
        fixed (Aabb* b = boxes) fixed (bool* v = visible) return (int)KoralMathNative.koral_bulk_cull_aabbs(frustum, b, (byte*)v, (nuint)boxes.Length);
    }
    public static void Normalize(Span<Vec3> vectors)
    {
        fixed (Vec3* v = vectors) KoralMathNative.koral_bulk_normalize(v, (nuint)vectors.Length);
    }
}
