package koral

import kotlin.math.cos
import kotlin.math.pow
import kotlin.math.sin
import kotlin.math.sqrt

/** kor::Easing: Penner's curves. In: slow start; Out: slow end; InOut: both. */
enum class Easing {
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

private fun outBounce(t0: Float): Float {
    var t = t0
    val n = 7.5625f
    val d = 2.75f
    if (t < 1f / d) return n * t * t
    if (t < 2f / d) { t -= 1.5f / d; return n * t * t + 0.75f }
    if (t < 2.5f / d) { t -= 2.25f / d; return n * t * t + 0.9375f }
    t -= 2.625f / d
    return n * t * t + 0.984375f
}

/** The eased progress at [t] (clamped to [0, 1]). */
fun ease(easing: Easing, t0: Float): Float {
    val t = saturate(t0)
    val c1 = 1.70158f
    val c2 = c1 * 1.525f
    val c3 = c1 + 1f
    val c4 = Tau / 3f
    val c5 = Tau / 4.5f
    return when (easing) {
        Easing.Linear -> t
        Easing.InSine -> 1f - cos(t * Pi * 0.5f)
        Easing.OutSine -> sin(t * Pi * 0.5f)
        Easing.InOutSine -> -(cos(Pi * t) - 1f) * 0.5f
        Easing.InQuad -> t * t
        Easing.OutQuad -> 1f - (1f - t) * (1f - t)
        Easing.InOutQuad -> if (t < 0.5f) 2f * t * t else 1f - (-2f * t + 2f).pow(2f) * 0.5f
        Easing.InCubic -> t * t * t
        Easing.OutCubic -> 1f - (1f - t).pow(3f)
        Easing.InOutCubic -> if (t < 0.5f) 4f * t * t * t else 1f - (-2f * t + 2f).pow(3f) * 0.5f
        Easing.InQuart -> t * t * t * t
        Easing.OutQuart -> 1f - (1f - t).pow(4f)
        Easing.InOutQuart -> if (t < 0.5f) 8f * t * t * t * t else 1f - (-2f * t + 2f).pow(4f) * 0.5f
        Easing.InQuint -> t * t * t * t * t
        Easing.OutQuint -> 1f - (1f - t).pow(5f)
        Easing.InOutQuint -> if (t < 0.5f) 16f * t * t * t * t * t else 1f - (-2f * t + 2f).pow(5f) * 0.5f
        Easing.InExpo -> if (t == 0f) 0f else 2f.pow(10f * t - 10f)
        Easing.OutExpo -> if (t == 1f) 1f else 1f - 2f.pow(-10f * t)
        Easing.InOutExpo -> if (t == 0f || t == 1f) t else if (t < 0.5f) 2f.pow(20f * t - 10f) * 0.5f else (2f - 2f.pow(-20f * t + 10f)) * 0.5f
        Easing.InCirc -> 1f - sqrt(1f - t * t)
        Easing.OutCirc -> sqrt(1f - (t - 1f) * (t - 1f))
        Easing.InOutCirc -> if (t < 0.5f) (1f - sqrt(1f - 4f * t * t)) * 0.5f else (sqrt(1f - (-2f * t + 2f).pow(2f)) + 1f) * 0.5f
        Easing.InBack -> c3 * t * t * t - c1 * t * t
        Easing.OutBack -> 1f + c3 * (t - 1f).pow(3f) + c1 * (t - 1f).pow(2f)
        Easing.InOutBack -> if (t < 0.5f) ((2f * t).pow(2f) * ((c2 + 1f) * 2f * t - c2)) * 0.5f
                            else ((2f * t - 2f).pow(2f) * ((c2 + 1f) * (t * 2f - 2f) + c2) + 2f) * 0.5f
        Easing.InElastic -> if (t == 0f || t == 1f) t else -2f.pow(10f * t - 10f) * sin((t * 10f - 10.75f) * c4)
        Easing.OutElastic -> if (t == 0f || t == 1f) t else 2f.pow(-10f * t) * sin((t * 10f - 0.75f) * c4) + 1f
        Easing.InOutElastic -> if (t == 0f || t == 1f) t
                               else if (t < 0.5f) -(2f.pow(20f * t - 10f) * sin((20f * t - 11.125f) * c5)) * 0.5f
                               else (2f.pow(-20f * t + 10f) * sin((20f * t - 11.125f) * c5)) * 0.5f + 1f
        Easing.InBounce -> 1f - outBounce(1f - t)
        Easing.OutBounce -> outBounce(t)
        Easing.InOutBounce -> if (t < 0.5f) (1f - outBounce(1f - 2f * t)) * 0.5f else (1f + outBounce(2f * t - 1f)) * 0.5f
    }
}
fun ease(easing: Easing, a: Float, b: Float, t: Float) = lerp(a, b, ease(easing, t))
fun ease(easing: Easing, a: Vec2, b: Vec2, t: Float) = lerp(a, b, ease(easing, t))
fun ease(easing: Easing, a: Vec3, b: Vec3, t: Float) = lerp(a, b, ease(easing, t))
fun ease(easing: Easing, a: Vec4, b: Vec4, t: Float) = lerp(a, b, ease(easing, t))

// Curves through control points, for Vec2 and Vec3 (C++'s are templates over the point type).

fun bezier(p0: Vec3, p1: Vec3, p2: Vec3, t: Float): Vec3 { val u = 1f - t; return p0 * (u * u) + p1 * (2f * u * t) + p2 * (t * t) }
fun bezier(p0: Vec2, p1: Vec2, p2: Vec2, t: Float): Vec2 { val u = 1f - t; return p0 * (u * u) + p1 * (2f * u * t) + p2 * (t * t) }
fun bezier(p0: Vec3, p1: Vec3, p2: Vec3, p3: Vec3, t: Float): Vec3 {
    val u = 1f - t
    return p0 * (u * u * u) + p1 * (3f * u * u * t) + p2 * (3f * u * t * t) + p3 * (t * t * t)
}
fun bezier(p0: Vec2, p1: Vec2, p2: Vec2, p3: Vec2, t: Float): Vec2 {
    val u = 1f - t
    return p0 * (u * u * u) + p1 * (3f * u * u * t) + p2 * (3f * u * t * t) + p3 * (t * t * t)
}
fun bezierTangent(p0: Vec3, p1: Vec3, p2: Vec3, p3: Vec3, t: Float): Vec3 {
    val u = 1f - t
    return (p1 - p0) * (3f * u * u) + (p2 - p1) * (6f * u * t) + (p3 - p2) * (3f * t * t)
}
fun hermite(p0: Vec3, m0: Vec3, p1: Vec3, m1: Vec3, t: Float): Vec3 {
    val t2 = t * t
    val t3 = t2 * t
    return p0 * (2f * t3 - 3f * t2 + 1f) + m0 * (t3 - 2f * t2 + t) + p1 * (-2f * t3 + 3f * t2) + m1 * (t3 - t2)
}
fun catmullRom(p0: Vec3, p1: Vec3, p2: Vec3, p3: Vec3, t: Float): Vec3 {
    val t2 = t * t
    val t3 = t2 * t
    return (p1 * 2f + (p2 - p0) * t + (p0 * 2f - p1 * 5f + p2 * 4f - p3) * t2 + (p1 * 3f - p0 - p2 * 3f + p3) * t3) * 0.5f
}
fun catmullRom(p0: Vec2, p1: Vec2, p2: Vec2, p3: Vec2, t: Float): Vec2 {
    val t2 = t * t
    val t3 = t2 * t
    return (p1 * 2f + (p2 - p0) * t + (p0 * 2f - p1 * 5f + p2 * 4f - p3) * t2 + (p1 * 3f - p0 - p2 * 3f + p3) * t3) * 0.5f
}
/** The Catmull–Rom path through all the points: t = 0 at the first, size - 1 at the last (or back at the first when [closed]). */
fun samplePath(points: List<Vec3>, t: Float, closed: Boolean = false): Vec3 {
    val n = points.size
    if (n == 0) return Vec3.Zero
    if (n == 1) return points[0]
    val segments = if (closed) n else n - 1
    val tt = if (closed) mod(t, segments.toFloat()) else clamp(t, 0f, segments.toFloat())
    val i = minOf(tt.toInt(), segments - 1)
    fun at(k: Int) = if (closed) points[mod(k, n)] else points[clamp(k, 0, n - 1)]
    return catmullRom(at(i - 1), at(i), at(i + 1), at(i + 2), tt - i.toFloat())
}
@JvmName("samplePath2")
fun samplePath(points: List<Vec2>, t: Float, closed: Boolean = false): Vec2 {
    val n = points.size
    if (n == 0) return Vec2.Zero
    if (n == 1) return points[0]
    val segments = if (closed) n else n - 1
    val tt = if (closed) mod(t, segments.toFloat()) else clamp(t, 0f, segments.toFloat())
    val i = minOf(tt.toInt(), segments - 1)
    fun at(k: Int) = if (closed) points[mod(k, n)] else points[clamp(k, 0, n - 1)]
    return catmullRom(at(i - 1), at(i), at(i + 1), at(i + 2), tt - i.toFloat())
}
