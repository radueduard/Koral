namespace Koral;

/// <summary>
/// kmath's free functions — kor::Dot, kor::Normalize, kor::Perspective, ... — under the same names. With
/// <c>using static Koral.KMath;</c> they read exactly as in C++: <c>Normalize(Cross(a, b))</c>.
/// </summary>
/// <remarks>
/// Each does its arithmetic in the order the C++ one does, so what is only additions, multiplications,
/// divisions, square roots and Floor gives the same bits as Koral itself (the tests check it against the C
/// interface); what goes through MathF.Sin and friends agrees to within the last bit or two.
/// </remarks>
public static partial class KMath
{
    // ---- scalars (kmath/scalar.h) -------------------------------------------------------------------

    public const float Pi = MathF.PI;
    public const float Tau = 2f * MathF.PI;
    public const float HalfPi = MathF.PI / 2f;
    /// <summary>The tolerance ApproxEqual uses when given none.</summary>
    public const float Epsilon = 1e-5f;
    /// <summary>C's FLT_EPSILON: the gap between 1 and the next float (not .NET's float.Epsilon, the smallest denormal).</summary>
    private const float FltEpsilon = 1.1920929E-07f;

    public static float Radians(float degrees) => degrees * (Pi / 180f);
    public static float Degrees(float radians) => radians * (180f / Pi);
    public static float Min(float a, float b) => b < a ? b : a;
    public static float Max(float a, float b) => a < b ? b : a;
    public static int Min(int a, int b) => b < a ? b : a;
    public static int Max(int a, int b) => a < b ? b : a;
    public static float Clamp(float v, float lo, float hi) => Min(Max(v, lo), hi);
    public static int Clamp(int v, int lo, int hi) => Min(Max(v, lo), hi);
    public static float Saturate(float v) => Clamp(v, 0f, 1f);
    public static float Abs(float v) => v < 0f ? -v : v;
    public static float Sign(float v) => (0f < v ? 1 : 0) - (v < 0f ? 1 : 0);
    public static float Floor(float v) => MathF.Floor(v);
    public static float Ceil(float v) => MathF.Ceiling(v);
    /// <summary>Halves away from zero, like C++'s std::round (not .NET's default banker's rounding).</summary>
    public static float Round(float v) => MathF.Round(v, MidpointRounding.AwayFromZero);
    public static float Trunc(float v) => MathF.Truncate(v);
    /// <summary>v - Floor(v): in [0, 1), negative v included.</summary>
    public static float Fract(float v) => v - MathF.Floor(v);
    /// <summary>The GLSL mod: the result has the sign of <paramref name="m"/>.</summary>
    public static float Mod(float v, float m) => v - m * MathF.Floor(v / m);
    public static int Mod(int v, int m) { int r = v % m; return r != 0 && (r < 0) != (m < 0) ? r + m : r; }
    public static float Sqrt(float v) => MathF.Sqrt(v);
    public static float InverseSqrt(float v) => 1f / MathF.Sqrt(v);
    public static float Pow(float v, float e) => MathF.Pow(v, e);
    public static float Exp(float v) => MathF.Exp(v);
    public static float Log(float v) => MathF.Log(v);
    public static float Sin(float v) => MathF.Sin(v);
    public static float Cos(float v) => MathF.Cos(v);
    public static float Tan(float v) => MathF.Tan(v);
    public static float Asin(float v) => MathF.Asin(v);
    public static float Acos(float v) => MathF.Acos(v);
    public static float Atan(float v) => MathF.Atan(v);
    public static float Atan2(float y, float x) => MathF.Atan2(y, x);
    public static bool IsFinite(float v) => float.IsFinite(v);
    public static bool IsNan(float v) => float.IsNaN(v);
    public static bool ApproxEqual(float a, float b, float epsilon = Epsilon) => Abs(a - b) <= epsilon;

    /// <summary>a + (b - a) * t, not clamped.</summary>
    public static float Lerp(float a, float b, float t) => a + (b - a) * t;
    public static float InverseLerp(float a, float b, float v) => a == b ? 0f : (v - a) / (b - a);
    public static float Remap(float v, float inMin, float inMax, float outMin, float outMax) => Lerp(outMin, outMax, InverseLerp(inMin, inMax, v));
    public static float Step(float edge, float v) => v < edge ? 0f : 1f;
    public static float SmoothStep(float edge0, float edge1, float v)
    {
        float t = Saturate((v - edge0) / (edge1 - edge0));
        return t * t * (3f - 2f * t);
    }
    public static float SmootherStep(float edge0, float edge1, float v)
    {
        float t = Saturate((v - edge0) / (edge1 - edge0));
        return t * t * t * (t * (t * 6f - 15f) + 10f);
    }
    public static float MoveTowards(float current, float target, float maxDelta) =>
        Abs(target - current) <= maxDelta ? target : current + Sign(target - current) * maxDelta;
    /// <summary>The shortest signed difference between two angles in radians, in [-Pi, Pi].</summary>
    public static float DeltaAngle(float from, float to)
    {
        float d = Mod(to - from, Tau);
        return d > Pi ? d - Tau : d;
    }
    public static float WrapAngle(float radians) => Mod(radians + Pi, Tau) - Pi;

    /// <summary>A critically damped spring toward <paramref name="target"/>; <paramref name="velocity"/> is its state.</summary>
    public static float SmoothDamp(float current, float target, ref float velocity, float smoothTime, float deltaTime, float maxSpeed = float.PositiveInfinity)
    {
        smoothTime = Max(0.0001f, smoothTime);
        float omega = 2f / smoothTime;
        float x = omega * deltaTime;
        float exp = 1f / (1f + x + 0.48f * x * x + 0.235f * x * x * x);
        float maxChange = maxSpeed * smoothTime;
        float change = Clamp(current - target, -maxChange, maxChange);
        float clampedTarget = current - change;
        float temp = (velocity + omega * change) * deltaTime;
        velocity = (velocity - omega * temp) * exp;
        float output = clampedTarget + (change + temp) * exp;
        if ((target - current > 0f) == (output > target))
        {
            output = target;
            velocity = (output - target) / deltaTime;
        }
        return output;
    }

    public static uint NextPowerOfTwo(uint v) => v <= 1 ? 1 : System.Numerics.BitOperations.RoundUpToPowerOf2(v);
    public static bool IsPowerOfTwo(uint v) => v != 0 && (v & (v - 1)) == 0;
    public static uint AlignUp(uint v, uint alignment) => (v + alignment - 1) & ~(alignment - 1);
    public static ulong AlignUp(ulong v, ulong alignment) => (v + alignment - 1) & ~(alignment - 1);
    public static int DivideRoundUp(int a, int b) => (a + b - 1) / b;
    public static uint DivideRoundUp(uint a, uint b) => (a + b - 1) / b;

    // ---- vectors (kmath/vector.h) --------------------------------------------------------------------

    public static float Dot(Vec2 a, Vec2 b) => a.X * b.X + a.Y * b.Y;
    public static float Dot(Vec3 a, Vec3 b) => a.X * b.X + a.Y * b.Y + a.Z * b.Z;
    public static float Dot(Vec4 a, Vec4 b) => a.X * b.X + a.Y * b.Y + a.Z * b.Z + a.W * b.W;
    public static Vec3 Cross(Vec3 a, Vec3 b) => new(a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X);
    /// <summary>The z of the 3D cross product: positive when b is counter-clockwise from a.</summary>
    public static float Cross(Vec2 a, Vec2 b) => a.X * b.Y - a.Y * b.X;
    public static float LengthSquared(Vec2 v) => Dot(v, v);
    public static float LengthSquared(Vec3 v) => Dot(v, v);
    public static float LengthSquared(Vec4 v) => Dot(v, v);
    public static float Length(Vec2 v) => MathF.Sqrt(Dot(v, v));
    public static float Length(Vec3 v) => MathF.Sqrt(Dot(v, v));
    public static float Length(Vec4 v) => MathF.Sqrt(Dot(v, v));
    public static float Distance(Vec2 a, Vec2 b) => Length(b - a);
    public static float Distance(Vec3 a, Vec3 b) => Length(b - a);
    public static float DistanceSquared(Vec2 a, Vec2 b) => LengthSquared(b - a);
    public static float DistanceSquared(Vec3 a, Vec3 b) => LengthSquared(b - a);
    /// <summary>v / Length(v); a zero vector stays zero rather than becoming NaN.</summary>
    public static Vec2 Normalize(Vec2 v) { float len = Length(v); return len > 0f ? v * (1f / len) : v; }
    public static Vec3 Normalize(Vec3 v) { float len = Length(v); return len > 0f ? v * (1f / len) : v; }
    public static Vec4 Normalize(Vec4 v) { float len = Length(v); return len > 0f ? v * (1f / len) : v; }
    public static Vec2 NormalizeOr(Vec2 v, Vec2 fallback) { float len = Length(v); return len > FltEpsilon ? v * (1f / len) : fallback; }
    public static Vec3 NormalizeOr(Vec3 v, Vec3 fallback) { float len = Length(v); return len > FltEpsilon ? v * (1f / len) : fallback; }
    public static Vec2 ClampLength(Vec2 v, float maxLength) { float sq = LengthSquared(v); return sq > maxLength * maxLength ? v * (maxLength / MathF.Sqrt(sq)) : v; }
    public static Vec3 ClampLength(Vec3 v, float maxLength) { float sq = LengthSquared(v); return sq > maxLength * maxLength ? v * (maxLength / MathF.Sqrt(sq)) : v; }
    public static Vec2 Reflect(Vec2 i, Vec2 n) => i - n * (2f * Dot(n, i));
    public static Vec3 Reflect(Vec3 i, Vec3 n) => i - n * (2f * Dot(n, i));
    public static Vec3 Refract(Vec3 i, Vec3 n, float eta)
    {
        float d = Dot(n, i);
        float k = 1f - eta * eta * (1f - d * d);
        return k < 0f ? Vec3.Zero : i * eta - n * (eta * d + MathF.Sqrt(k));
    }
    public static Vec3 Project(Vec3 v, Vec3 onto) { float sq = Dot(onto, onto); return sq > 0f ? onto * (Dot(v, onto) / sq) : Vec3.Zero; }
    public static Vec3 ProjectOnPlane(Vec3 v, Vec3 n) => v - n * Dot(v, n);
    public static float Angle(Vec3 a, Vec3 b)
    {
        float denom = MathF.Sqrt(LengthSquared(a) * LengthSquared(b));
        return denom > 0f ? MathF.Acos(Clamp(Dot(a, b) / denom, -1f, 1f)) : 0f;
    }
    public static float SignedAngle(Vec3 a, Vec3 b, Vec3 axis) => MathF.Atan2(Dot(Cross(a, b), axis), Dot(a, b));
    public static Vec3 AnyPerpendicular(Vec3 v) => Normalize(Cross(v, Abs(v.X) < 0.9f ? Vec3.UnitX : Vec3.UnitY));
    public static void OrthoNormalize(ref Vec3 normal, ref Vec3 tangent)
    {
        normal = Normalize(normal);
        tangent = NormalizeOr(tangent - normal * Dot(tangent, normal), AnyPerpendicular(normal));
    }
    public static bool ApproxEqual(Vec2 a, Vec2 b, float epsilon = Epsilon) => ApproxEqual(a.X, b.X, epsilon) && ApproxEqual(a.Y, b.Y, epsilon);
    public static bool ApproxEqual(Vec3 a, Vec3 b, float epsilon = Epsilon) => ApproxEqual(a.X, b.X, epsilon) && ApproxEqual(a.Y, b.Y, epsilon) && ApproxEqual(a.Z, b.Z, epsilon);
    public static bool ApproxEqual(Vec4 a, Vec4 b, float epsilon = Epsilon) => ApproxEqual((Vec3)a, (Vec3)b, epsilon) && ApproxEqual(a.W, b.W, epsilon);
    public static float MinComponent(Vec3 v) => Min(Min(v.X, v.Y), v.Z);
    public static float MaxComponent(Vec3 v) => Max(Max(v.X, v.Y), v.Z);
    public static float Sum(Vec3 v) => v.X + v.Y + v.Z;
    public static float Product(Vec3 v) => v.X * v.Y * v.Z;

    public static Vec2 Abs(Vec2 v) => new(Abs(v.X), Abs(v.Y));
    public static Vec3 Abs(Vec3 v) => new(Abs(v.X), Abs(v.Y), Abs(v.Z));
    public static Vec4 Abs(Vec4 v) => new(Abs(v.X), Abs(v.Y), Abs(v.Z), Abs(v.W));
    public static Vec2 Floor(Vec2 v) => new(MathF.Floor(v.X), MathF.Floor(v.Y));
    public static Vec3 Floor(Vec3 v) => new(MathF.Floor(v.X), MathF.Floor(v.Y), MathF.Floor(v.Z));
    public static Vec2 Ceil(Vec2 v) => new(MathF.Ceiling(v.X), MathF.Ceiling(v.Y));
    public static Vec3 Ceil(Vec3 v) => new(MathF.Ceiling(v.X), MathF.Ceiling(v.Y), MathF.Ceiling(v.Z));
    public static Vec2 Round(Vec2 v) => new(Round(v.X), Round(v.Y));
    public static Vec3 Round(Vec3 v) => new(Round(v.X), Round(v.Y), Round(v.Z));
    public static Vec2 Fract(Vec2 v) => new(Fract(v.X), Fract(v.Y));
    public static Vec3 Fract(Vec3 v) => new(Fract(v.X), Fract(v.Y), Fract(v.Z));
    public static Vec3 Sqrt(Vec3 v) => new(MathF.Sqrt(v.X), MathF.Sqrt(v.Y), MathF.Sqrt(v.Z));
    public static Vec3 Radians(Vec3 v) => new(Radians(v.X), Radians(v.Y), Radians(v.Z));
    public static Vec3 Degrees(Vec3 v) => new(Degrees(v.X), Degrees(v.Y), Degrees(v.Z));
    public static Vec2 Min(Vec2 a, Vec2 b) => new(Min(a.X, b.X), Min(a.Y, b.Y));
    public static Vec3 Min(Vec3 a, Vec3 b) => new(Min(a.X, b.X), Min(a.Y, b.Y), Min(a.Z, b.Z));
    public static Vec4 Min(Vec4 a, Vec4 b) => new(Min(a.X, b.X), Min(a.Y, b.Y), Min(a.Z, b.Z), Min(a.W, b.W));
    public static Vec2 Max(Vec2 a, Vec2 b) => new(Max(a.X, b.X), Max(a.Y, b.Y));
    public static Vec3 Max(Vec3 a, Vec3 b) => new(Max(a.X, b.X), Max(a.Y, b.Y), Max(a.Z, b.Z));
    public static Vec4 Max(Vec4 a, Vec4 b) => new(Max(a.X, b.X), Max(a.Y, b.Y), Max(a.Z, b.Z), Max(a.W, b.W));
    public static Vec2 Clamp(Vec2 v, Vec2 lo, Vec2 hi) => Min(Max(v, lo), hi);
    public static Vec3 Clamp(Vec3 v, Vec3 lo, Vec3 hi) => Min(Max(v, lo), hi);
    public static Vec4 Clamp(Vec4 v, Vec4 lo, Vec4 hi) => Min(Max(v, lo), hi);
    public static Vec2 Clamp(Vec2 v, float lo, float hi) => Clamp(v, new Vec2(lo), new Vec2(hi));
    public static Vec3 Clamp(Vec3 v, float lo, float hi) => Clamp(v, new Vec3(lo), new Vec3(hi));
    public static Vec4 Clamp(Vec4 v, float lo, float hi) => Clamp(v, new Vec4(lo), new Vec4(hi));
    public static Vec3 Saturate(Vec3 v) => Clamp(v, 0f, 1f);
    public static Vec2 Lerp(Vec2 a, Vec2 b, float t) => a + (b - a) * t;
    public static Vec3 Lerp(Vec3 a, Vec3 b, float t) => a + (b - a) * t;
    public static Vec4 Lerp(Vec4 a, Vec4 b, float t) => a + (b - a) * t;
    public static Vec3 MoveTowards(Vec3 current, Vec3 target, float maxDistance)
    {
        Vec3 d = target - current;
        float len = Length(d);
        return len <= maxDistance || len == 0f ? target : current + d * (maxDistance / len);
    }
    public static Vec3 SmoothDamp(Vec3 current, Vec3 target, ref Vec3 velocity, float smoothTime, float deltaTime, float maxSpeed = float.PositiveInfinity)
    {
        smoothTime = Max(0.0001f, smoothTime);
        float omega = 2f / smoothTime;
        float x = omega * deltaTime;
        float exp = 1f / (1f + x + 0.48f * x * x + 0.235f * x * x * x);
        Vec3 change = ClampLength(current - target, maxSpeed * smoothTime);
        Vec3 clampedTarget = current - change;
        Vec3 temp = (velocity + change * omega) * deltaTime;
        velocity = (velocity - temp * omega) * exp;
        Vec3 output = clampedTarget + (change + temp) * exp;
        if (Dot(target - current, output - target) > 0f)
        {
            output = target;
            velocity = (output - target) / deltaTime;
        }
        return output;
    }
    public static bool IsFinite(Vec3 v) => float.IsFinite(v.X) && float.IsFinite(v.Y) && float.IsFinite(v.Z);

    // ---- matrices (kmath/matrix.h) -------------------------------------------------------------------

    public static Mat4 Transpose(Mat4 m) => new(m.Row(0), m.Row(1), m.Row(2), m.Row(3));
    public static Mat3 Transpose(Mat3 m) => new(m.Row(0), m.Row(1), m.Row(2));
    public static float Determinant(Mat3 m) => Dot(m.C0, Cross(m.C1, m.C2));
    public static float Determinant(Mat4 m)
    {
        float s0 = m[2, 2] * m[3, 3] - m[3, 2] * m[2, 3], s1 = m[2, 1] * m[3, 3] - m[3, 1] * m[2, 3];
        float s2 = m[2, 1] * m[3, 2] - m[3, 1] * m[2, 2], s3 = m[2, 0] * m[3, 3] - m[3, 0] * m[2, 3];
        float s4 = m[2, 0] * m[3, 2] - m[3, 0] * m[2, 2], s5 = m[2, 0] * m[3, 1] - m[3, 0] * m[2, 1];
        float c0 = +(m[1, 1] * s0 - m[1, 2] * s1 + m[1, 3] * s2);
        float c1 = -(m[1, 0] * s0 - m[1, 2] * s3 + m[1, 3] * s4);
        float c2 = +(m[1, 0] * s1 - m[1, 1] * s3 + m[1, 3] * s5);
        float c3 = -(m[1, 0] * s2 - m[1, 1] * s4 + m[1, 2] * s5);
        return m[0, 0] * c0 + m[0, 1] * c1 + m[0, 2] * c2 + m[0, 3] * c3;
    }
    public static Mat3 Inverse(Mat3 m)
    {
        Vec3 r0 = Cross(m.C1, m.C2), r1 = Cross(m.C2, m.C0), r2 = Cross(m.C0, m.C1);
        float inv = 1f / Dot(m.C0, r0);
        return Transpose(new Mat3(r0 * inv, r1 * inv, r2 * inv));
    }
    /// <summary>The inverse; a singular matrix gives non-finite elements.</summary>
    public static Mat4 Inverse(Mat4 m)
    {
        float a2323 = m[2, 2] * m[3, 3] - m[2, 3] * m[3, 2], a1323 = m[2, 1] * m[3, 3] - m[2, 3] * m[3, 1];
        float a1223 = m[2, 1] * m[3, 2] - m[2, 2] * m[3, 1], a0323 = m[2, 0] * m[3, 3] - m[2, 3] * m[3, 0];
        float a0223 = m[2, 0] * m[3, 2] - m[2, 2] * m[3, 0], a0123 = m[2, 0] * m[3, 1] - m[2, 1] * m[3, 0];
        float a2313 = m[1, 2] * m[3, 3] - m[1, 3] * m[3, 2], a1313 = m[1, 1] * m[3, 3] - m[1, 3] * m[3, 1];
        float a1213 = m[1, 1] * m[3, 2] - m[1, 2] * m[3, 1], a2312 = m[1, 2] * m[2, 3] - m[1, 3] * m[2, 2];
        float a1312 = m[1, 1] * m[2, 3] - m[1, 3] * m[2, 1], a1212 = m[1, 1] * m[2, 2] - m[1, 2] * m[2, 1];
        float a0313 = m[1, 0] * m[3, 3] - m[1, 3] * m[3, 0], a0213 = m[1, 0] * m[3, 2] - m[1, 2] * m[3, 0];
        float a0312 = m[1, 0] * m[2, 3] - m[1, 3] * m[2, 0], a0212 = m[1, 0] * m[2, 2] - m[1, 2] * m[2, 0];
        float a0113 = m[1, 0] * m[3, 1] - m[1, 1] * m[3, 0], a0112 = m[1, 0] * m[2, 1] - m[1, 1] * m[2, 0];

        float det = m[0, 0] * (m[1, 1] * a2323 - m[1, 2] * a1323 + m[1, 3] * a1223)
                  - m[0, 1] * (m[1, 0] * a2323 - m[1, 2] * a0323 + m[1, 3] * a0223)
                  + m[0, 2] * (m[1, 0] * a1323 - m[1, 1] * a0323 + m[1, 3] * a0123)
                  - m[0, 3] * (m[1, 0] * a1223 - m[1, 1] * a0223 + m[1, 2] * a0123);
        float inv = 1f / det;

        var r = Mat4.Zero;
        r[0, 0] = inv * (m[1, 1] * a2323 - m[1, 2] * a1323 + m[1, 3] * a1223);
        r[0, 1] = inv * -(m[0, 1] * a2323 - m[0, 2] * a1323 + m[0, 3] * a1223);
        r[0, 2] = inv * (m[0, 1] * a2313 - m[0, 2] * a1313 + m[0, 3] * a1213);
        r[0, 3] = inv * -(m[0, 1] * a2312 - m[0, 2] * a1312 + m[0, 3] * a1212);
        r[1, 0] = inv * -(m[1, 0] * a2323 - m[1, 2] * a0323 + m[1, 3] * a0223);
        r[1, 1] = inv * (m[0, 0] * a2323 - m[0, 2] * a0323 + m[0, 3] * a0223);
        r[1, 2] = inv * -(m[0, 0] * a2313 - m[0, 2] * a0313 + m[0, 3] * a0213);
        r[1, 3] = inv * (m[0, 0] * a2312 - m[0, 2] * a0312 + m[0, 3] * a0212);
        r[2, 0] = inv * (m[1, 0] * a1323 - m[1, 1] * a0323 + m[1, 3] * a0123);
        r[2, 1] = inv * -(m[0, 0] * a1323 - m[0, 1] * a0323 + m[0, 3] * a0123);
        r[2, 2] = inv * (m[0, 0] * a1313 - m[0, 1] * a0313 + m[0, 3] * a0113);
        r[2, 3] = inv * -(m[0, 0] * a1312 - m[0, 1] * a0312 + m[0, 3] * a0112);
        r[3, 0] = inv * -(m[1, 0] * a1223 - m[1, 1] * a0223 + m[1, 2] * a0123);
        r[3, 1] = inv * (m[0, 0] * a1223 - m[0, 1] * a0223 + m[0, 2] * a0123);
        r[3, 2] = inv * -(m[0, 0] * a1213 - m[0, 1] * a0213 + m[0, 2] * a0113);
        r[3, 3] = inv * (m[0, 0] * a1212 - m[0, 1] * a0212 + m[0, 2] * a0112);
        return r;
    }
    /// <summary>The matrix that transforms normals for <paramref name="model"/>: inverse transpose of its 3x3.</summary>
    public static Mat3 NormalMatrix(Mat4 model) => Transpose(Inverse(new Mat3(model)));
    public static bool ApproxEqual(Mat4 a, Mat4 b, float epsilon = Epsilon) =>
        ApproxEqual(a.C0, b.C0, epsilon) && ApproxEqual(a.C1, b.C1, epsilon) && ApproxEqual(a.C2, b.C2, epsilon) && ApproxEqual(a.C3, b.C3, epsilon);
    public static bool ApproxEqual(Mat3 a, Mat3 b, float epsilon = Epsilon) =>
        ApproxEqual(a.C0, b.C0, epsilon) && ApproxEqual(a.C1, b.C1, epsilon) && ApproxEqual(a.C2, b.C2, epsilon);

    // ---- quaternions (kmath/quaternion.h) ------------------------------------------------------------

    public static float Dot(Quat a, Quat b) => a.X * b.X + a.Y * b.Y + a.Z * b.Z + a.W * b.W;
    public static float Length(Quat q) => MathF.Sqrt(Dot(q, q));
    public static Quat Normalize(Quat q) { float len = Length(q); return len > 0f ? q * (1f / len) : Quat.Identity; }
    public static Quat Conjugate(Quat q) => new(-q.X, -q.Y, -q.Z, q.W);
    public static Quat Inverse(Quat q) => Conjugate(q) * (1f / Dot(q, q));
    public static float Angle(Quat q) => 2f * MathF.Acos(Clamp(q.W, -1f, 1f));
    public static Vec3 Axis(Quat q)
    {
        float s2 = 1f - q.W * q.W;
        return s2 <= 0f ? Vec3.UnitZ : q.XYZ * (1f / MathF.Sqrt(s2));
    }
    /// <summary>(pitch, yaw, roll) in radians.</summary>
    public static Vec3 EulerAngles(Quat q)
    {
        float py = 2f * (q.Y * q.Z + q.W * q.X), px = q.W * q.W - q.X * q.X - q.Y * q.Y + q.Z * q.Z;
        float pitch = Abs(px) < 1e-7f && Abs(py) < 1e-7f ? 2f * MathF.Atan2(q.X, q.W) : MathF.Atan2(py, px);
        float yaw = MathF.Asin(Clamp(-2f * (q.X * q.Z - q.W * q.Y), -1f, 1f));
        float roll = MathF.Atan2(2f * (q.X * q.Y + q.W * q.Z), q.W * q.W + q.X * q.X - q.Y * q.Y - q.Z * q.Z);
        return new Vec3(pitch, yaw, roll);
    }
    public static Mat3 ToMat3(Quat q)
    {
        float xx = q.X * q.X, yy = q.Y * q.Y, zz = q.Z * q.Z, xz = q.X * q.Z, xy = q.X * q.Y, yz = q.Y * q.Z;
        float wx = q.W * q.X, wy = q.W * q.Y, wz = q.W * q.Z;
        return new Mat3(new Vec3(1f - 2f * (yy + zz), 2f * (xy + wz), 2f * (xz - wy)),
                        new Vec3(2f * (xy - wz), 1f - 2f * (xx + zz), 2f * (yz + wx)),
                        new Vec3(2f * (xz + wy), 2f * (yz - wx), 1f - 2f * (xx + yy)));
    }
    public static Mat4 ToMat4(Quat q) => new(ToMat3(q));
    public static Quat Nlerp(Quat a, Quat b, float t)
    {
        if (Dot(a, b) < 0f) b = -b;
        return Normalize(a + (b - a) * t);
    }
    public static Quat Slerp(Quat a, Quat b, float t)
    {
        float cosTheta = Dot(a, b);
        if (cosTheta < 0f) { b = -b; cosTheta = -cosTheta; }
        if (cosTheta > 1f - 1e-6f) return Nlerp(a, b, t);
        float angle = MathF.Acos(cosTheta);
        return (a * MathF.Sin((1f - t) * angle) + b * MathF.Sin(t * angle)) * (1f / MathF.Sin(angle));
    }
    public static Quat RotateTowards(Quat from, Quat to, float maxRadians)
    {
        float angle = 2f * MathF.Acos(Clamp(Abs(Dot(from, to)), 0f, 1f));
        return angle <= maxRadians || angle == 0f ? to : Slerp(from, to, maxRadians / angle);
    }
    public static bool ApproxEqual(Quat a, Quat b, float epsilon = Epsilon) =>
        ApproxEqual(a.X, b.X, epsilon) && ApproxEqual(a.Y, b.Y, epsilon) && ApproxEqual(a.Z, b.Z, epsilon) && ApproxEqual(a.W, b.W, epsilon);
    /// <summary>The same rotation, whichever of q and -q was given.</summary>
    public static bool SameRotation(Quat a, Quat b, float epsilon = Epsilon) => ApproxEqual(a, b, epsilon) || ApproxEqual(a, -b, epsilon);

    // ---- transforms and cameras (kmath/transform.h) --------------------------------------------------

    public static Mat4 Translation(Vec3 by) { var m = Mat4.Identity; m.C3 = new Vec4(by, 1f); return m; }
    public static Mat4 Scaling(Vec3 by) { var m = Mat4.Identity; m[0, 0] = by.X; m[1, 1] = by.Y; m[2, 2] = by.Z; return m; }
    public static Mat4 Rotation(float angle, Vec3 axis) => ToMat4(Quat.AngleAxis(angle, axis));
    public static Mat4 Rotation(Quat q) => ToMat4(q);
    /// <summary>Translation * Rotation * Scale: scale first, then rotate, then move.</summary>
    public static Mat4 Compose(Vec3 translation, Quat rotation, Vec3 scale)
    {
        Mat3 r = ToMat3(rotation);
        return new Mat4(new Vec4(r.C0 * scale.X, 0f), new Vec4(r.C1 * scale.Y, 0f), new Vec4(r.C2 * scale.Z, 0f), new Vec4(translation, 1f));
    }
    /// <summary>Splits an affine matrix back into what Compose took; false for a degenerate one.</summary>
    public static bool Decompose(Mat4 m, out Vec3 translation, out Quat rotation, out Vec3 scale)
    {
        translation = (Vec3)m.C3;
        Vec3 c0 = (Vec3)m.C0, c1 = (Vec3)m.C1, c2 = (Vec3)m.C2;
        scale = new Vec3(Length(c0), Length(c1), Length(c2));
        rotation = Quat.Identity;
        if (scale.X == 0f || scale.Y == 0f || scale.Z == 0f) return false;
        if (Dot(c0, Cross(c1, c2)) < 0f) scale.X = -scale.X;
        rotation = Normalize(Quat.FromMatrix(new Mat3(c0 / scale.X, c1 / scale.Y, c2 / scale.Z)));
        return true;
    }
    /// <summary>m * Translation(by).</summary>
    public static Mat4 Translate(Mat4 m, Vec3 by) { var r = m; r.C3 = m.C0 * by.X + m.C1 * by.Y + m.C2 * by.Z + m.C3; return r; }
    /// <summary>m * Rotation(angle, axis).</summary>
    public static Mat4 Rotate(Mat4 m, float angle, Vec3 axis) => m * Rotation(angle, axis);
    /// <summary>m * Scaling(by).</summary>
    public static Mat4 Scale(Mat4 m, Vec3 by) => new(m.C0 * by.X, m.C1 * by.Y, m.C2 * by.Z, m.C3);

    /// <summary>A right-handed view matrix: the camera at <paramref name="eye"/> looking at <paramref name="target"/>.</summary>
    public static Mat4 LookAt(Vec3 eye, Vec3 target, Vec3? up = null)
    {
        Vec3 f = Normalize(target - eye), s = Normalize(Cross(f, up ?? Vec3.Up)), u = Cross(s, f);
        return new Mat4(new Vec4(s.X, u.X, -f.X, 0f), new Vec4(s.Y, u.Y, -f.Y, 0f), new Vec4(s.Z, u.Z, -f.Z, 0f),
                        new Vec4(-Dot(s, eye), -Dot(u, eye), Dot(f, eye), 1f));
    }
    /// <summary>Perspective projection, vertical field of view in radians, depth 0 at near to 1 at far. Y is not flipped.</summary>
    public static Mat4 Perspective(float fovY, float aspect, float near, float far)
    {
        float f = 1f / MathF.Tan(fovY * 0.5f);
        var m = Mat4.Zero;
        m[0, 0] = f / aspect;
        m[1, 1] = f;
        m[2, 2] = far / (near - far);
        m[2, 3] = -1f;
        m[3, 2] = -(far * near) / (far - near);
        return m;
    }
    /// <summary>Reversed depth (1 at near, 0 at far); an infinite <paramref name="far"/> for no far plane.</summary>
    public static Mat4 PerspectiveReversedZ(float fovY, float aspect, float near, float far = float.PositiveInfinity)
    {
        float f = 1f / MathF.Tan(fovY * 0.5f);
        var m = Mat4.Zero;
        m[0, 0] = f / aspect;
        m[1, 1] = f;
        m[2, 3] = -1f;
        if (float.IsInfinity(far))
        {
            m[3, 2] = near;
        }
        else
        {
            m[2, 2] = near / (far - near);
            m[3, 2] = far * near / (far - near);
        }
        return m;
    }
    public static Mat4 Orthographic(float left, float right, float bottom, float top, float near, float far)
    {
        var m = Mat4.Identity;
        m[0, 0] = 2f / (right - left);
        m[1, 1] = 2f / (top - bottom);
        m[2, 2] = -1f / (far - near);
        m[3, 0] = -(right + left) / (right - left);
        m[3, 1] = -(top + bottom) / (top - bottom);
        m[3, 2] = -near / (far - near);
        return m;
    }
    /// <summary>m * (p, 1), without the projective divide.</summary>
    public static Vec3 TransformPoint(Mat4 m, Vec3 p) => (Vec3)m.C0 * p.X + (Vec3)m.C1 * p.Y + (Vec3)m.C2 * p.Z + (Vec3)m.C3;
    public static Vec3 TransformPointProjective(Mat4 m, Vec3 p) { Vec4 h = m * new Vec4(p, 1f); return (Vec3)h / h.W; }
    public static Vec3 TransformDirection(Mat4 m, Vec3 d) => (Vec3)m.C0 * d.X + (Vec3)m.C1 * d.Y + (Vec3)m.C2 * d.Z;
}
