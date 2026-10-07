namespace Koral;

/// <summary>kor::Easing: Penner's curves. In: slow start; Out: slow end; InOut: both.</summary>
public enum Easing : byte
{
    Linear,
    InSine, OutSine, InOutSine,
    InQuad, OutQuad, InOutQuad,
    InCubic, OutCubic, InOutCubic,
    InQuart, OutQuart, InOutQuart,
    InQuint, OutQuint, InOutQuint,
    InExpo, OutExpo, InOutExpo,
    InCirc, OutCirc, InOutCirc,
    InBack, OutBack, InOutBack,
    InElastic, OutElastic, InOutElastic,
    InBounce, OutBounce, InOutBounce,
}

public static partial class KMath
{
    private static float OutBounce(float t)
    {
        const float n = 7.5625f, d = 2.75f;
        if (t < 1f / d) return n * t * t;
        if (t < 2f / d) { t -= 1.5f / d; return n * t * t + 0.75f; }
        if (t < 2.5f / d) { t -= 2.25f / d; return n * t * t + 0.9375f; }
        t -= 2.625f / d;
        return n * t * t + 0.984375f;
    }

    /// <summary>The eased progress at <paramref name="t"/> (clamped to [0, 1]).</summary>
    public static float Ease(Easing easing, float t)
    {
        t = Saturate(t);
        const float c1 = 1.70158f, c2 = c1 * 1.525f, c3 = c1 + 1f, c4 = Tau / 3f, c5 = Tau / 4.5f, pi = Pi;
        switch (easing)
        {
            case Easing.Linear: return t;
            case Easing.InSine: return 1f - MathF.Cos(t * pi * 0.5f);
            case Easing.OutSine: return MathF.Sin(t * pi * 0.5f);
            case Easing.InOutSine: return -(MathF.Cos(pi * t) - 1f) * 0.5f;
            case Easing.InQuad: return t * t;
            case Easing.OutQuad: return 1f - (1f - t) * (1f - t);
            case Easing.InOutQuad: return t < 0.5f ? 2f * t * t : 1f - MathF.Pow(-2f * t + 2f, 2f) * 0.5f;
            case Easing.InCubic: return t * t * t;
            case Easing.OutCubic: return 1f - MathF.Pow(1f - t, 3f);
            case Easing.InOutCubic: return t < 0.5f ? 4f * t * t * t : 1f - MathF.Pow(-2f * t + 2f, 3f) * 0.5f;
            case Easing.InQuart: return t * t * t * t;
            case Easing.OutQuart: return 1f - MathF.Pow(1f - t, 4f);
            case Easing.InOutQuart: return t < 0.5f ? 8f * t * t * t * t : 1f - MathF.Pow(-2f * t + 2f, 4f) * 0.5f;
            case Easing.InQuint: return t * t * t * t * t;
            case Easing.OutQuint: return 1f - MathF.Pow(1f - t, 5f);
            case Easing.InOutQuint: return t < 0.5f ? 16f * t * t * t * t * t : 1f - MathF.Pow(-2f * t + 2f, 5f) * 0.5f;
            case Easing.InExpo: return t == 0f ? 0f : MathF.Pow(2f, 10f * t - 10f);
            case Easing.OutExpo: return t == 1f ? 1f : 1f - MathF.Pow(2f, -10f * t);
            case Easing.InOutExpo:
                if (t == 0f || t == 1f) return t;
                return t < 0.5f ? MathF.Pow(2f, 20f * t - 10f) * 0.5f : (2f - MathF.Pow(2f, -20f * t + 10f)) * 0.5f;
            case Easing.InCirc: return 1f - MathF.Sqrt(1f - t * t);
            case Easing.OutCirc: return MathF.Sqrt(1f - (t - 1f) * (t - 1f));
            case Easing.InOutCirc:
                return t < 0.5f ? (1f - MathF.Sqrt(1f - 4f * t * t)) * 0.5f : (MathF.Sqrt(1f - MathF.Pow(-2f * t + 2f, 2f)) + 1f) * 0.5f;
            case Easing.InBack: return c3 * t * t * t - c1 * t * t;
            case Easing.OutBack: return 1f + c3 * MathF.Pow(t - 1f, 3f) + c1 * MathF.Pow(t - 1f, 2f);
            case Easing.InOutBack:
                return t < 0.5f ? (MathF.Pow(2f * t, 2f) * ((c2 + 1f) * 2f * t - c2)) * 0.5f
                                : (MathF.Pow(2f * t - 2f, 2f) * ((c2 + 1f) * (t * 2f - 2f) + c2) + 2f) * 0.5f;
            case Easing.InElastic:
                if (t == 0f || t == 1f) return t;
                return -MathF.Pow(2f, 10f * t - 10f) * MathF.Sin((t * 10f - 10.75f) * c4);
            case Easing.OutElastic:
                if (t == 0f || t == 1f) return t;
                return MathF.Pow(2f, -10f * t) * MathF.Sin((t * 10f - 0.75f) * c4) + 1f;
            case Easing.InOutElastic:
                if (t == 0f || t == 1f) return t;
                return t < 0.5f ? -(MathF.Pow(2f, 20f * t - 10f) * MathF.Sin((20f * t - 11.125f) * c5)) * 0.5f
                                : (MathF.Pow(2f, -20f * t + 10f) * MathF.Sin((20f * t - 11.125f) * c5)) * 0.5f + 1f;
            case Easing.InBounce: return 1f - OutBounce(1f - t);
            case Easing.OutBounce: return OutBounce(t);
            case Easing.InOutBounce: return t < 0.5f ? (1f - OutBounce(1f - 2f * t)) * 0.5f : (1f + OutBounce(2f * t - 1f)) * 0.5f;
        }
        return t;
    }
    public static float Ease(Easing easing, float a, float b, float t) => Lerp(a, b, Ease(easing, t));
    public static Vec2 Ease(Easing easing, Vec2 a, Vec2 b, float t) => Lerp(a, b, Ease(easing, t));
    public static Vec3 Ease(Easing easing, Vec3 a, Vec3 b, float t) => Lerp(a, b, Ease(easing, t));
    public static Vec4 Ease(Easing easing, Vec4 a, Vec4 b, float t) => Lerp(a, b, Ease(easing, t));

    // Curves through control points, for Vec2 and Vec3 (C++'s are templates over the point type).

    public static Vec3 Bezier(Vec3 p0, Vec3 p1, Vec3 p2, float t) { float u = 1f - t; return p0 * (u * u) + p1 * (2f * u * t) + p2 * (t * t); }
    public static Vec2 Bezier(Vec2 p0, Vec2 p1, Vec2 p2, float t) { float u = 1f - t; return p0 * (u * u) + p1 * (2f * u * t) + p2 * (t * t); }
    public static Vec3 Bezier(Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, float t)
    {
        float u = 1f - t;
        return p0 * (u * u * u) + p1 * (3f * u * u * t) + p2 * (3f * u * t * t) + p3 * (t * t * t);
    }
    public static Vec2 Bezier(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, float t)
    {
        float u = 1f - t;
        return p0 * (u * u * u) + p1 * (3f * u * u * t) + p2 * (3f * u * t * t) + p3 * (t * t * t);
    }
    public static Vec3 BezierTangent(Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, float t)
    {
        float u = 1f - t;
        return (p1 - p0) * (3f * u * u) + (p2 - p1) * (6f * u * t) + (p3 - p2) * (3f * t * t);
    }
    public static Vec3 Hermite(Vec3 p0, Vec3 m0, Vec3 p1, Vec3 m1, float t)
    {
        float t2 = t * t, t3 = t2 * t;
        return p0 * (2f * t3 - 3f * t2 + 1f) + m0 * (t3 - 2f * t2 + t) + p1 * (-2f * t3 + 3f * t2) + m1 * (t3 - t2);
    }
    public static Vec3 CatmullRom(Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, float t)
    {
        float t2 = t * t, t3 = t2 * t;
        return (p1 * 2f + (p2 - p0) * t + (p0 * 2f - p1 * 5f + p2 * 4f - p3) * t2 + (p1 * 3f - p0 - p2 * 3f + p3) * t3) * 0.5f;
    }
    public static Vec2 CatmullRom(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, float t)
    {
        float t2 = t * t, t3 = t2 * t;
        return (p1 * 2f + (p2 - p0) * t + (p0 * 2f - p1 * 5f + p2 * 4f - p3) * t2 + (p1 * 3f - p0 - p2 * 3f + p3) * t3) * 0.5f;
    }
    /// <summary>The Catmull–Rom path through all the points: t = 0 at the first, Length - 1 at the last.</summary>
    public static Vec3 SamplePath(ReadOnlySpan<Vec3> points, float t, bool closed = false)
    {
        int n = points.Length;
        if (n == 0) return default;
        if (n == 1) return points[0];
        int segments = closed ? n : n - 1;
        t = closed ? Mod(t, segments) : Clamp(t, 0f, segments);
        int i = Min((int)t, segments - 1);
        Vec3 At(ReadOnlySpan<Vec3> p, int k) => closed ? p[Mod(k, n)] : p[Clamp(k, 0, n - 1)];
        return CatmullRom(At(points, i - 1), At(points, i), At(points, i + 1), At(points, i + 2), t - i);
    }
    public static Vec2 SamplePath(ReadOnlySpan<Vec2> points, float t, bool closed = false)
    {
        int n = points.Length;
        if (n == 0) return default;
        if (n == 1) return points[0];
        int segments = closed ? n : n - 1;
        t = closed ? Mod(t, segments) : Clamp(t, 0f, segments);
        int i = Min((int)t, segments - 1);
        Vec2 At(ReadOnlySpan<Vec2> p, int k) => closed ? p[Mod(k, n)] : p[Clamp(k, 0, n - 1)];
        return CatmullRom(At(points, i - 1), At(points, i), At(points, i + 1), At(points, i + 2), t - i);
    }
}
