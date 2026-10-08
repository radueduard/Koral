using System.Runtime.InteropServices;

namespace Koral;

// kmath/geometry.h: shapes and the questions asked of them. Hits are distances along the ray (in units of
// its direction's length); a ray starting inside a solid shape hits it at 0. Layouts are the C++ types'.

/// <summary>kor::Ray.</summary>
[StructLayout(LayoutKind.Sequential)]
public record struct Ray(Vec3 Origin, Vec3 Direction)
{
    /// <summary>From the origin toward -Z, as C++'s default.</summary>
    public Ray() : this(Vec3.Zero, Vec3.Forward) { }
    public readonly Vec3 At(float distance) => Origin + Direction * distance;
    /// <summary>From <paramref name="from"/> toward <paramref name="to"/>, the direction normalised.</summary>
    public static Ray Between(Vec3 from, Vec3 to) => new(from, KMath.Normalize(to - from));
}

/// <summary>kor::Plane: the points p with Dot(Normal, p) + Distance == 0.</summary>
[StructLayout(LayoutKind.Sequential)]
public record struct Plane(Vec3 Normal, float Distance)
{
    /// <summary>The XZ plane, normal +Y, as C++'s default.</summary>
    public Plane() : this(Vec3.Up, 0f) { }
    public static Plane FromPointNormal(Vec3 point, Vec3 normal)
    {
        Vec3 n = KMath.Normalize(normal);
        return new Plane(n, -KMath.Dot(n, point));
    }
    /// <summary>Through three points; counter-clockwise a → b → c faces the normal.</summary>
    public static Plane FromPoints(Vec3 a, Vec3 b, Vec3 c) => FromPointNormal(a, KMath.Cross(b - a, c - a));
    public readonly float SignedDistance(Vec3 p) => KMath.Dot(Normal, p) + Distance;
    public readonly Plane Normalized()
    {
        float len = KMath.Length(Normal);
        return len > 0f ? new Plane(Normal / len, Distance / len) : this;
    }
}

/// <summary>kor::Sphere.</summary>
[StructLayout(LayoutKind.Sequential)]
public record struct Sphere(Vec3 Center, float Radius)
{
    public readonly bool Contains(Vec3 p) => KMath.Distance2(Center, p) <= Radius * Radius;

    /// <summary>A sphere around all the points (Ritter's).</summary>
    public static Sphere FromPoints(ReadOnlySpan<Vec3> points)
    {
        if (points.IsEmpty) return default;
        Vec3 Farthest(ReadOnlySpan<Vec3> all, Vec3 from)
        {
            Vec3 best = all[0];
            float bestSq = -1f;
            foreach (var p in all)
            {
                float sq = KMath.Distance2(from, p);
                if (sq > bestSq) { bestSq = sq; best = p; }
            }
            return best;
        }
        Vec3 a = Farthest(points, points[0]), b = Farthest(points, a);
        var s = new Sphere((a + b) * 0.5f, KMath.Distance(a, b) * 0.5f);
        foreach (var p in points)
        {
            float d = KMath.Distance(s.Center, p);
            if (d > s.Radius)
            {
                float r = (s.Radius + d) * 0.5f;
                s.Center += (p - s.Center) * ((r - s.Radius) / d);
                s.Radius = r;
            }
        }
        return s;
    }
}

/// <summary>kor::Aabb. <c>new Aabb()</c> is <see cref="Empty"/> (min +inf, max -inf), what Expand grows from.</summary>
[StructLayout(LayoutKind.Sequential)]
public record struct Aabb(Vec3 Min, Vec3 Max)
{
    /// <summary>The empty box, as C++'s default (<c>default(Aabb)</c> is a point at the origin instead).</summary>
    public Aabb() : this(new Vec3(float.PositiveInfinity), new Vec3(float.NegativeInfinity)) { }
    public static Aabb Empty => new(new Vec3(float.PositiveInfinity), new Vec3(float.NegativeInfinity));
    public static Aabb FromCenterExtents(Vec3 center, Vec3 halfExtents) => new(center - halfExtents, center + halfExtents);
    public static Aabb FromPoints(ReadOnlySpan<Vec3> points)
    {
        var box = Empty;
        foreach (var p in points) box = box.Expand(p);
        return box;
    }

    public readonly bool Valid => Min.X <= Max.X && Min.Y <= Max.Y && Min.Z <= Max.Z;
    public readonly Vec3 Center => (Min + Max) * 0.5f;
    public readonly Vec3 Size => Max - Min;
    public readonly Vec3 HalfExtents => (Max - Min) * 0.5f;
    public readonly float Volume => Valid ? KMath.CompMul(Size) : 0f;
    public readonly float SurfaceArea
    {
        get
        {
            if (!Valid) return 0f;
            Vec3 s = Size;
            return 2f * (s.X * s.Y + s.Y * s.Z + s.Z * s.X);
        }
    }
    /// <summary>Corner i of 8: bit 0 picks Max.X, bit 1 Max.Y, bit 2 Max.Z.</summary>
    public readonly Vec3 Corner(int i) => new((i & 1) != 0 ? Max.X : Min.X, (i & 2) != 0 ? Max.Y : Min.Y, (i & 4) != 0 ? Max.Z : Min.Z);
    public readonly bool Contains(Vec3 p) => p.X >= Min.X && p.Y >= Min.Y && p.Z >= Min.Z && p.X <= Max.X && p.Y <= Max.Y && p.Z <= Max.Z;
    public readonly bool Contains(Aabb b) => Contains(b.Min) && Contains(b.Max);
    /// <summary>This box grown to take in <paramref name="p"/> (C++'s Expand changes the box in place; a C# value returns the new one).</summary>
    public readonly Aabb Expand(Vec3 p) => new(KMath.Min(Min, p), KMath.Max(Max, p));
    public readonly Aabb Expand(Aabb b) => new(KMath.Min(Min, b.Min), KMath.Max(Max, b.Max));
    public readonly Aabb Inflated(float amount) => new(Min - new Vec3(amount), Max + new Vec3(amount));
    /// <summary>The box around this box after <paramref name="m"/> (Arvo's method).</summary>
    public readonly Aabb Transformed(Mat4 m)
    {
        if (!Valid) return this;
        Vec3 lo = (Vec3)m.C3, hi = lo;
        for (int c = 0; c < 3; ++c)
        {
            Vec3 col = (Vec3)m[c];
            Vec3 a = col * Min[c], b = col * Max[c];
            lo += KMath.Min(a, b);
            hi += KMath.Max(a, b);
        }
        return new Aabb(lo, hi);
    }
}

/// <summary>kor::Obb: a box of HalfExtents around Center, turned by Rotation.</summary>
[StructLayout(LayoutKind.Sequential)]
public record struct Obb(Vec3 Center, Vec3 HalfExtents, Quat Rotation)
{
    /// <summary>A unit cube at the origin, as C++'s default.</summary>
    public Obb() : this(Vec3.Zero, new Vec3(0.5f), Quat.Identity) { }
    public static Obb FromAabb(Aabb local, Mat4 m)
    {
        if (!KMath.Decompose(m, out _, out var rotation, out var scale)) return new Obb(KMath.TransformPoint(m, local.Center), Vec3.Zero, Quat.Identity);
        return new Obb(KMath.TransformPoint(m, local.Center), KMath.Abs(local.HalfExtents * scale), rotation);
    }
    public readonly Aabb Bounds()
    {
        Mat3 r = KMath.ToMat3(Rotation);
        Vec3 extent = default;
        for (int i = 0; i < 3; ++i)
            extent[i] = KMath.Abs(r[0, i]) * HalfExtents.X + KMath.Abs(r[1, i]) * HalfExtents.Y + KMath.Abs(r[2, i]) * HalfExtents.Z;
        return new Aabb(Center - extent, Center + extent);
    }
    public readonly bool Contains(Vec3 p)
    {
        Vec3 local = KMath.Abs(KMath.Conjugate(Rotation) * (p - Center));
        return local.X <= HalfExtents.X && local.Y <= HalfExtents.Y && local.Z <= HalfExtents.Z;
    }
    public readonly Vec3 Corner(int i) => Center + Rotation * new Vec3((i & 1) != 0 ? HalfExtents.X : -HalfExtents.X,
        (i & 2) != 0 ? HalfExtents.Y : -HalfExtents.Y, (i & 4) != 0 ? HalfExtents.Z : -HalfExtents.Z);
}

/// <summary>kor::Triangle.</summary>
[StructLayout(LayoutKind.Sequential)]
public record struct Triangle(Vec3 A, Vec3 B, Vec3 C)
{
    public readonly Vec3 Normal => KMath.Normalize(KMath.Cross(B - A, C - A));
    public readonly float Area => 0.5f * KMath.Length(KMath.Cross(B - A, C - A));
    public readonly Vec3 Centroid => (A + B + C) * (1f / 3f);
    /// <summary>(u, v, w) with p = u*A + v*B + w*C.</summary>
    public readonly Vec3 Barycentric(Vec3 p)
    {
        Vec3 v0 = B - A, v1 = C - A, v2 = p - A;
        float d00 = KMath.Dot(v0, v0), d01 = KMath.Dot(v0, v1), d11 = KMath.Dot(v1, v1);
        float d20 = KMath.Dot(v2, v0), d21 = KMath.Dot(v2, v1);
        float denom = d00 * d11 - d01 * d01;
        if (denom == 0f) return new Vec3(1f, 0f, 0f);
        float v = (d11 * d20 - d01 * d21) / denom;
        float w = (d00 * d21 - d01 * d20) / denom;
        return new Vec3(1f - v - w, v, w);
    }
}

/// <summary>kor::Aabb2: Aabb in 2D, an axis-aligned rectangle by its corners.</summary>
[StructLayout(LayoutKind.Sequential)]
public record struct Aabb2(Vec2 Min, Vec2 Max)
{
    /// <summary>The empty box, as C++'s default.</summary>
    public Aabb2() : this(new Vec2(float.PositiveInfinity), new Vec2(float.NegativeInfinity)) { }
    public static Aabb2 Empty => new(new Vec2(float.PositiveInfinity), new Vec2(float.NegativeInfinity));
    public static Aabb2 FromPositionSize(Vec2 position, Vec2 size) => new(position, position + size);
    public readonly bool Valid => Min.X <= Max.X && Min.Y <= Max.Y;
    public readonly Vec2 Size => Max - Min;
    public readonly Vec2 Center => (Min + Max) * 0.5f;
    public readonly float Area => Valid ? Size.X * Size.Y : 0f;
    public readonly bool Contains(Vec2 p) => p.X >= Min.X && p.Y >= Min.Y && p.X <= Max.X && p.Y <= Max.Y;
    public readonly Aabb2 Expand(Vec2 p) => new(KMath.Min(Min, p), KMath.Max(Max, p));
    public readonly Aabb2 Inflated(float amount) => new(Min - new Vec2(amount), Max + new Vec2(amount));
}

/// <summary>kor::Containment.</summary>
public enum Containment { Outside, Intersects, Inside }

/// <summary>kor::TriangleHit: U and V are the barycentric weights of B and C (A's is 1 - U - V).</summary>
[StructLayout(LayoutKind.Sequential)]
public record struct TriangleHit(float Distance, float U, float V);

/// <summary>kor::Frustum: six inward-facing planes — left, right, bottom, top, near, far.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct Frustum
{
    public Plane Left, Right, Bottom, Top, Near, Far;

    public Plane this[int i]
    {
        readonly get => i switch { 0 => Left, 1 => Right, 2 => Bottom, 3 => Top, 4 => Near, 5 => Far, _ => throw new IndexOutOfRangeException() };
        set
        {
            switch (i)
            {
                case 0: Left = value; break;
                case 1: Right = value; break;
                case 2: Bottom = value; break;
                case 3: Top = value; break;
                case 4: Near = value; break;
                case 5: Far = value; break;
                default: throw new IndexOutOfRangeException();
            }
        }
    }

    /// <summary>From a projection * view matrix with Vulkan's 0..1 depth.</summary>
    public static Frustum FromMatrix(Mat4 m)
    {
        Vec4 r0 = m.Row(0), r1 = m.Row(1), r2 = m.Row(2), r3 = m.Row(3);
        Vec4[] raw = [r3 + r0, r3 - r0, r3 + r1, r3 - r1, r2, r3 - r2];
        var f = new Frustum();
        for (int i = 0; i < 6; ++i) f[i] = new Plane((Vec3)raw[i], raw[i].W).Normalized();
        return f;
    }
    /// <summary>The 8 world-space corners (Aabb.Corner's numbering, z near/far).</summary>
    public static Vec3[] Corners(Mat4 viewProjection)
    {
        Mat4 inv = KMath.Inverse(viewProjection);
        var corners = new Vec3[8];
        for (int i = 0; i < 8; ++i)
            corners[i] = KMath.TransformPointProjective(inv, new Vec3((i & 1) != 0 ? 1f : -1f, (i & 2) != 0 ? 1f : -1f, (i & 4) != 0 ? 1f : 0f));
        return corners;
    }

    public readonly bool Contains(Vec3 p)
    {
        for (int i = 0; i < 6; ++i)
            if (this[i].SignedDistance(p) < 0f) return false;
        return true;
    }
    public readonly Containment Classify(Aabb box)
    {
        if (!box.Valid) return Containment.Outside;
        Vec3 center = box.Center, extent = box.HalfExtents;
        var result = Containment.Inside;
        for (int i = 0; i < 6; ++i)
        {
            Plane plane = this[i];
            float d = plane.SignedDistance(center);
            float r = KMath.Dot(extent, KMath.Abs(plane.Normal));
            if (d < -r) return Containment.Outside;
            if (d < r) result = Containment.Intersects;
        }
        return result;
    }
    public readonly Containment Classify(Sphere sphere)
    {
        var result = Containment.Inside;
        for (int i = 0; i < 6; ++i)
        {
            float d = this[i].SignedDistance(sphere.Center);
            if (d < -sphere.Radius) return Containment.Outside;
            if (d < sphere.Radius) result = Containment.Intersects;
        }
        return result;
    }
}

public static partial class KMath
{
    public static Aabb Union(Aabb a, Aabb b) => new(Min(a.Min, b.Min), Max(a.Max, b.Max));
    public static Aabb Intersection(Aabb a, Aabb b) => new(Max(a.Min, b.Min), Min(a.Max, b.Max));
    public static Aabb2 Union(Aabb2 a, Aabb2 b) => new(Min(a.Min, b.Min), Max(a.Max, b.Max));
    public static Aabb2 Intersection(Aabb2 a, Aabb2 b) => new(Max(a.Min, b.Min), Min(a.Max, b.Max));

    public static float? Raycast(Ray ray, Plane plane, float maxDistance = float.PositiveInfinity)
    {
        float denom = Dot(plane.Normal, ray.Direction);
        if (Abs(denom) < 1e-8f) return null;
        float t = -plane.SignedDistance(ray.Origin) / denom;
        return t < 0f || t > maxDistance ? null : t;
    }
    public static float? Raycast(Ray ray, Sphere sphere, float maxDistance = float.PositiveInfinity)
    {
        Vec3 m = ray.Origin - sphere.Center;
        float a = Dot(ray.Direction, ray.Direction);
        float b = Dot(m, ray.Direction);
        float c = Dot(m, m) - sphere.Radius * sphere.Radius;
        if (c <= 0f) return 0f;
        if (b > 0f || a == 0f) return null;
        float disc = b * b - a * c;
        if (disc < 0f) return null;
        float t = (-b - MathF.Sqrt(disc)) / a;
        return t > maxDistance ? null : t;
    }
    public static float? Raycast(Ray ray, Aabb box, float maxDistance = float.PositiveInfinity)
    {
        float tMin = 0f, tMax = maxDistance;
        for (int i = 0; i < 3; ++i)
        {
            if (Abs(ray.Direction[i]) < 1e-12f)
            {
                if (ray.Origin[i] < box.Min[i] || ray.Origin[i] > box.Max[i]) return null;
                continue;
            }
            float inv = 1f / ray.Direction[i];
            float t0 = (box.Min[i] - ray.Origin[i]) * inv, t1 = (box.Max[i] - ray.Origin[i]) * inv;
            if (t0 > t1) (t0, t1) = (t1, t0);
            tMin = Max(tMin, t0);
            tMax = Min(tMax, t1);
            if (tMin > tMax) return null;
        }
        return tMin;
    }
    public static float? Raycast(Ray ray, Obb box, float maxDistance = float.PositiveInfinity)
    {
        Quat inv = Conjugate(box.Rotation);
        return Raycast(new Ray(inv * (ray.Origin - box.Center), inv * ray.Direction), new Aabb(-box.HalfExtents, box.HalfExtents), maxDistance);
    }
    /// <summary>Möller–Trumbore; both faces unless <paramref name="cullBackFaces"/>.</summary>
    public static TriangleHit? Raycast(Ray ray, Triangle tri, float maxDistance = float.PositiveInfinity, bool cullBackFaces = false)
    {
        const float eps = 1e-8f;
        Vec3 e1 = tri.B - tri.A, e2 = tri.C - tri.A;
        Vec3 p = Cross(ray.Direction, e2);
        float det = Dot(e1, p);
        if (cullBackFaces ? det < eps : Abs(det) < eps) return null;
        float invDet = 1f / det;
        Vec3 s = ray.Origin - tri.A;
        float u = Dot(s, p) * invDet;
        if (u < 0f || u > 1f) return null;
        Vec3 q = Cross(s, e1);
        float v = Dot(ray.Direction, q) * invDet;
        if (v < 0f || u + v > 1f) return null;
        float t = Dot(e2, q) * invDet;
        return t < 0f || t > maxDistance ? null : new TriangleHit(t, u, v);
    }

    public static bool Overlaps(Aabb a, Aabb b) =>
        a.Min.X <= b.Max.X && a.Min.Y <= b.Max.Y && a.Min.Z <= b.Max.Z && b.Min.X <= a.Max.X && b.Min.Y <= a.Max.Y && b.Min.Z <= a.Max.Z;
    public static bool Overlaps(Aabb2 a, Aabb2 b) => a.Min.X <= b.Max.X && b.Min.X <= a.Max.X && a.Min.Y <= b.Max.Y && b.Min.Y <= a.Max.Y;
    public static bool Overlaps(Sphere a, Sphere b) { float r = a.Radius + b.Radius; return Distance2(a.Center, b.Center) <= r * r; }
    public static bool Overlaps(Aabb box, Sphere sphere) => Distance2(ClosestPoint(box, sphere.Center), sphere.Center) <= sphere.Radius * sphere.Radius;
    public static bool Overlaps(Sphere sphere, Aabb box) => Overlaps(box, sphere);
    public static bool Overlaps(Frustum frustum, Aabb box) => frustum.Classify(box) != Containment.Outside;
    public static bool Overlaps(Frustum frustum, Sphere sphere) => frustum.Classify(sphere) != Containment.Outside;
    /// <summary>Separating-axis test over the 15 candidate axes.</summary>
    public static bool Overlaps(Obb a, Obb b)
    {
        Mat3 ra = ToMat3(a.Rotation), rb = ToMat3(b.Rotation);
        Span<float> r = stackalloc float[9], absR = stackalloc float[9];
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
            {
                r[i * 3 + j] = Dot(ra[i], rb[j]);
                absR[i * 3 + j] = Abs(r[i * 3 + j]) + 1e-6f;
            }
        Vec3 d = b.Center - a.Center;
        Vec3 t = new(Dot(d, ra.C0), Dot(d, ra.C1), Dot(d, ra.C2));
        Vec3 ea = a.HalfExtents, eb = b.HalfExtents;
        for (int i = 0; i < 3; ++i)
            if (Abs(t[i]) > ea[i] + eb[0] * absR[i * 3] + eb[1] * absR[i * 3 + 1] + eb[2] * absR[i * 3 + 2]) return false;
        for (int j = 0; j < 3; ++j)
            if (Abs(t[0] * r[j] + t[1] * r[3 + j] + t[2] * r[6 + j]) > ea[0] * absR[j] + ea[1] * absR[3 + j] + ea[2] * absR[6 + j] + eb[j]) return false;
        for (int i = 0; i < 3; ++i)
        {
            int i1 = (i + 1) % 3, i2 = (i + 2) % 3;
            for (int j = 0; j < 3; ++j)
            {
                int j1 = (j + 1) % 3, j2 = (j + 2) % 3;
                float rA = ea[i1] * absR[i2 * 3 + j] + ea[i2] * absR[i1 * 3 + j];
                float rB = eb[j1] * absR[i * 3 + j2] + eb[j2] * absR[i * 3 + j1];
                if (Abs(t[i2] * r[i1 * 3 + j] - t[i1] * r[i2 * 3 + j]) > rA + rB) return false;
            }
        }
        return true;
    }

    public static Vec3 ClosestPoint(Aabb box, Vec3 p) => Clamp(p, box.Min, box.Max);
    public static Vec3 ClosestPoint(Plane plane, Vec3 p) => p - plane.Normal * plane.SignedDistance(p);
    public static Vec3 ClosestPoint(Sphere sphere, Vec3 p)
    {
        Vec3 d = p - sphere.Center;
        float sq = Dot(d, d);
        return sq <= sphere.Radius * sphere.Radius ? p : sphere.Center + d * (sphere.Radius / MathF.Sqrt(sq));
    }
    public static Vec3 ClosestPoint(Obb box, Vec3 p)
    {
        Vec3 local = Conjugate(box.Rotation) * (p - box.Center);
        return box.Center + box.Rotation * Clamp(local, -box.HalfExtents, box.HalfExtents);
    }
    public static Vec3 ClosestPoint(Triangle tri, Vec3 p)
    {
        Vec3 a = tri.A, b = tri.B, c = tri.C;
        Vec3 ab = b - a, ac = c - a, ap = p - a;
        float d1 = Dot(ab, ap), d2 = Dot(ac, ap);
        if (d1 <= 0f && d2 <= 0f) return a;
        Vec3 bp = p - b;
        float d3 = Dot(ab, bp), d4 = Dot(ac, bp);
        if (d3 >= 0f && d4 <= d3) return b;
        float vc = d1 * d4 - d3 * d2;
        if (vc <= 0f && d1 >= 0f && d3 <= 0f) return a + ab * (d1 / (d1 - d3));
        Vec3 cp = p - c;
        float d5 = Dot(ab, cp), d6 = Dot(ac, cp);
        if (d6 >= 0f && d5 <= d6) return c;
        float vb = d5 * d2 - d1 * d6;
        if (vb <= 0f && d2 >= 0f && d6 <= 0f) return a + ac * (d2 / (d2 - d6));
        float va = d3 * d6 - d5 * d4;
        if (va <= 0f && d4 - d3 >= 0f && d5 - d6 >= 0f) return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
        float denom = 1f / (va + vb + vc);
        return a + ab * (vb * denom) + ac * (vc * denom);
    }
    public static Vec3 ClosestPointOnSegment(Vec3 a, Vec3 b, Vec3 p)
    {
        Vec3 ab = b - a;
        float sq = Dot(ab, ab);
        return sq == 0f ? a : a + ab * Saturate(Dot(p - a, ab) / sq);
    }
    public static float Distance(Aabb box, Vec3 p) => Distance(ClosestPoint(box, p), p);
}
