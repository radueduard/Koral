@file:Suppress("NOTHING_TO_INLINE")

package koral

import kotlin.math.abs as kabs
import kotlin.math.acos as kacos
import kotlin.math.atan2 as katan2
import kotlin.math.floor as kfloor
import kotlin.math.ceil as kceil
import kotlin.math.sqrt as ksqrt
import kotlin.math.tan as ktan

// kmath's free functions, top-level and under the same names (in Kotlin's lowerCamelCase): dot, cross,
// normalize, perspective, ... Each does its arithmetic in the order the C++ one does, so what is only
// additions, multiplications, divisions, square roots and floor gives the same bits as Koral itself (the
// tests check it against the C interface). Scalar functions kotlin.math already has (abs, floor, sqrt, sin,
// min, max, ...) are not repeated here: use kotlin.math's — but note its round() rounds halves to even, where
// C++'s and koral's round(Vec) round them away from zero.

const val Pi = 3.14159265358979323846f
const val Tau = 2f * Pi
const val HalfPi = Pi / 2f
/** The tolerance approxEqual uses when given none. */
const val Epsilon = 1e-5f
/** C's FLT_EPSILON: the gap between 1 and the next float. */
private const val FltEpsilon = 1.1920929E-07f

// ---- scalars (kmath/scalar.h) -----------------------------------------------------------------------------

fun radians(degrees: Float) = degrees * (Pi / 180f)
fun degrees(radians: Float) = radians * (180f / Pi)
fun clamp(v: Float, lo: Float, hi: Float) = fmin(fmax(v, lo), hi)
fun clamp(v: Int, lo: Int, hi: Int) = if (v < lo) lo else if (hi < v) hi else v
fun saturate(v: Float) = clamp(v, 0f, 1f)
/** v - floor(v): in [0, 1), negative v included. */
fun fract(v: Float) = v - kfloor(v)
/** The GLSL mod: the result has the sign of [m]. */
fun mod(v: Float, m: Float) = v - m * kfloor(v / m)
fun mod(v: Int, m: Int): Int { val r = v % m; return if (r != 0 && (r < 0) != (m < 0)) r + m else r }
fun inverseSqrt(v: Float) = 1f / ksqrt(v)
fun approxEqual(a: Float, b: Float, epsilon: Float = Epsilon) = kabs(a - b) <= epsilon
/** a + (b - a) * t, not clamped. */
fun lerp(a: Float, b: Float, t: Float) = a + (b - a) * t
fun inverseLerp(a: Float, b: Float, v: Float) = if (a == b) 0f else (v - a) / (b - a)
fun remap(v: Float, inMin: Float, inMax: Float, outMin: Float, outMax: Float) = lerp(outMin, outMax, inverseLerp(inMin, inMax, v))
fun step(edge: Float, v: Float) = if (v < edge) 0f else 1f
fun smoothStep(edge0: Float, edge1: Float, v: Float): Float {
    val t = saturate((v - edge0) / (edge1 - edge0))
    return t * t * (3f - 2f * t)
}
fun smootherStep(edge0: Float, edge1: Float, v: Float): Float {
    val t = saturate((v - edge0) / (edge1 - edge0))
    return t * t * t * (t * (t * 6f - 15f) + 10f)
}
fun moveTowards(current: Float, target: Float, maxDelta: Float) =
    if (kabs(target - current) <= maxDelta) target else current + kotlin.math.sign(target - current) * maxDelta
/** The shortest signed difference between two angles in radians, in [-Pi, Pi]. */
fun deltaAngle(from: Float, to: Float): Float { val d = mod(to - from, Tau); return if (d > Pi) d - Tau else d }
fun wrapAngle(radians: Float) = mod(radians + Pi, Tau) - Pi

/** The state a smoothDamp spring keeps between calls: C++'s `velocity` reference. */
class Velocity<T>(var value: T)

/** A critically damped spring toward [target]; [velocity] is its state, kept between calls. */
fun smoothDamp(current: Float, target: Float, velocity: Velocity<Float>, smoothTime: Float, deltaTime: Float,
               maxSpeed: Float = Float.POSITIVE_INFINITY): Float {
    val st = maxOf(0.0001f, smoothTime)
    val omega = 2f / st
    val x = omega * deltaTime
    val exp = 1f / (1f + x + 0.48f * x * x + 0.235f * x * x * x)
    val maxChange = maxSpeed * st
    val change = clamp(current - target, -maxChange, maxChange)
    val clampedTarget = current - change
    val temp = (velocity.value + omega * change) * deltaTime
    velocity.value = (velocity.value - omega * temp) * exp
    var output = clampedTarget + (change + temp) * exp
    if ((target - current > 0f) == (output > target)) {
        output = target
        velocity.value = (output - target) / deltaTime
    }
    return output
}

fun nextPowerOfTwo(v: Int): Int = if (v <= 1) 1 else Integer.highestOneBit(v - 1) shl 1
fun isPowerOfTwo(v: Int) = v != 0 && (v and (v - 1)) == 0
fun alignUp(v: Long, alignment: Long) = (v + alignment - 1) and (alignment - 1).inv()
fun divideRoundUp(a: Int, b: Int) = (a + b - 1) / b

// ---- vectors (kmath/vector.h) -------------------------------------------------------------------------

fun dot(a: Vec2, b: Vec2) = a.x * b.x + a.y * b.y
fun dot(a: Vec3, b: Vec3) = a.x * b.x + a.y * b.y + a.z * b.z
fun dot(a: Vec4, b: Vec4) = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w
fun cross(a: Vec3, b: Vec3) = Vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x)
/** The z of the 3D cross product: positive when b is counter-clockwise from a. */
fun cross(a: Vec2, b: Vec2) = a.x * b.y - a.y * b.x
fun lengthSquared(v: Vec2) = dot(v, v)
fun lengthSquared(v: Vec3) = dot(v, v)
fun lengthSquared(v: Vec4) = dot(v, v)
fun length(v: Vec2) = ksqrt(dot(v, v))
fun length(v: Vec3) = ksqrt(dot(v, v))
fun length(v: Vec4) = ksqrt(dot(v, v))
fun distance(a: Vec2, b: Vec2) = length(b - a)
fun distance(a: Vec3, b: Vec3) = length(b - a)
fun distanceSquared(a: Vec2, b: Vec2) = lengthSquared(b - a)
fun distanceSquared(a: Vec3, b: Vec3) = lengthSquared(b - a)
/** v / length(v); a zero vector stays zero rather than becoming NaN. */
fun normalize(v: Vec2): Vec2 { val len = length(v); return if (len > 0f) v * (1f / len) else v }
fun normalize(v: Vec3): Vec3 { val len = length(v); return if (len > 0f) v * (1f / len) else v }
fun normalize(v: Vec4): Vec4 { val len = length(v); return if (len > 0f) v * (1f / len) else v }
fun normalizeOr(v: Vec3, fallback: Vec3): Vec3 { val len = length(v); return if (len > FltEpsilon) v * (1f / len) else fallback }
fun clampLength(v: Vec3, maxLength: Float): Vec3 { val sq = lengthSquared(v); return if (sq > maxLength * maxLength) v * (maxLength / ksqrt(sq)) else v }
fun reflect(i: Vec3, n: Vec3) = i - n * (2f * dot(n, i))
fun reflect(i: Vec2, n: Vec2) = i - n * (2f * dot(n, i))
fun refract(i: Vec3, n: Vec3, eta: Float): Vec3 {
    val d = dot(n, i)
    val k = 1f - eta * eta * (1f - d * d)
    return if (k < 0f) Vec3.Zero else i * eta - n * (eta * d + ksqrt(k))
}
fun project(v: Vec3, onto: Vec3): Vec3 { val sq = dot(onto, onto); return if (sq > 0f) onto * (dot(v, onto) / sq) else Vec3.Zero }
fun projectOnPlane(v: Vec3, n: Vec3) = v - n * dot(v, n)
fun angle(a: Vec3, b: Vec3): Float {
    val denom = ksqrt(lengthSquared(a) * lengthSquared(b))
    return if (denom > 0f) kacos(clamp(dot(a, b) / denom, -1f, 1f)) else 0f
}
fun signedAngle(a: Vec3, b: Vec3, axis: Vec3) = katan2(dot(cross(a, b), axis), dot(a, b))
fun anyPerpendicular(v: Vec3) = normalize(cross(v, if (kabs(v.x) < 0.9f) Vec3.UnitX else Vec3.UnitY))
/** Gram–Schmidt: (unit normal, unit tangent perpendicular to it). */
fun orthoNormalize(normal: Vec3, tangent: Vec3): Pair<Vec3, Vec3> {
    val n = normalize(normal)
    return n to normalizeOr(tangent - n * dot(tangent, n), anyPerpendicular(n))
}
fun approxEqual(a: Vec2, b: Vec2, epsilon: Float = Epsilon) = approxEqual(a.x, b.x, epsilon) && approxEqual(a.y, b.y, epsilon)
fun approxEqual(a: Vec3, b: Vec3, epsilon: Float = Epsilon) = approxEqual(a.x, b.x, epsilon) && approxEqual(a.y, b.y, epsilon) && approxEqual(a.z, b.z, epsilon)
fun approxEqual(a: Vec4, b: Vec4, epsilon: Float = Epsilon) = approxEqual(a.xyz, b.xyz, epsilon) && approxEqual(a.w, b.w, epsilon)
private inline fun fmin(a: Float, b: Float) = if (b < a) b else a   // kor::Min's choice on ties and signed zeros
private inline fun fmax(a: Float, b: Float) = if (a < b) b else a
fun minComponent(v: Vec3) = fmin(fmin(v.x, v.y), v.z)
fun maxComponent(v: Vec3) = fmax(fmax(v.x, v.y), v.z)
fun sum(v: Vec3) = v.x + v.y + v.z
fun product(v: Vec3) = v.x * v.y * v.z

fun abs(v: Vec2) = Vec2(kabs(v.x), kabs(v.y))
fun abs(v: Vec3) = Vec3(kabs(v.x), kabs(v.y), kabs(v.z))
fun abs(v: Vec4) = Vec4(kabs(v.x), kabs(v.y), kabs(v.z), kabs(v.w))
fun floor(v: Vec2) = Vec2(kfloor(v.x), kfloor(v.y))
fun floor(v: Vec3) = Vec3(kfloor(v.x), kfloor(v.y), kfloor(v.z))
fun ceil(v: Vec2) = Vec2(kceil(v.x), kceil(v.y))
fun ceil(v: Vec3) = Vec3(kceil(v.x), kceil(v.y), kceil(v.z))
private fun roundAway(v: Float): Float {
    val r = kotlin.math.truncate(v)
    return if (kabs(v - r) >= 0.5f) r + kotlin.math.sign(v) else r
}
/** Halves away from zero, as C++'s round. */
fun round(v: Vec2) = Vec2(roundAway(v.x), roundAway(v.y))
fun round(v: Vec3) = Vec3(roundAway(v.x), roundAway(v.y), roundAway(v.z))
fun fract(v: Vec2) = Vec2(fract(v.x), fract(v.y))
fun fract(v: Vec3) = Vec3(fract(v.x), fract(v.y), fract(v.z))
fun sqrt(v: Vec3) = Vec3(ksqrt(v.x), ksqrt(v.y), ksqrt(v.z))
fun radians(v: Vec3) = Vec3(radians(v.x), radians(v.y), radians(v.z))
fun degrees(v: Vec3) = Vec3(degrees(v.x), degrees(v.y), degrees(v.z))
fun min(a: Vec2, b: Vec2) = Vec2(fmin(a.x, b.x), fmin(a.y, b.y))
fun min(a: Vec3, b: Vec3) = Vec3(fmin(a.x, b.x), fmin(a.y, b.y), fmin(a.z, b.z))
fun min(a: Vec4, b: Vec4) = Vec4(fmin(a.x, b.x), fmin(a.y, b.y), fmin(a.z, b.z), fmin(a.w, b.w))
fun max(a: Vec2, b: Vec2) = Vec2(fmax(a.x, b.x), fmax(a.y, b.y))
fun max(a: Vec3, b: Vec3) = Vec3(fmax(a.x, b.x), fmax(a.y, b.y), fmax(a.z, b.z))
fun max(a: Vec4, b: Vec4) = Vec4(fmax(a.x, b.x), fmax(a.y, b.y), fmax(a.z, b.z), fmax(a.w, b.w))
fun clamp(v: Vec2, lo: Vec2, hi: Vec2) = min(max(v, lo), hi)
fun clamp(v: Vec3, lo: Vec3, hi: Vec3) = min(max(v, lo), hi)
fun clamp(v: Vec4, lo: Vec4, hi: Vec4) = min(max(v, lo), hi)
fun clamp(v: Vec2, lo: Float, hi: Float) = clamp(v, Vec2(lo), Vec2(hi))
fun clamp(v: Vec3, lo: Float, hi: Float) = clamp(v, Vec3(lo), Vec3(hi))
fun clamp(v: Vec4, lo: Float, hi: Float) = clamp(v, Vec4(lo), Vec4(hi))
fun saturate(v: Vec3) = clamp(v, 0f, 1f)
fun lerp(a: Vec2, b: Vec2, t: Float) = a + (b - a) * t
fun lerp(a: Vec3, b: Vec3, t: Float) = a + (b - a) * t
fun lerp(a: Vec4, b: Vec4, t: Float) = a + (b - a) * t
fun moveTowards(current: Vec3, target: Vec3, maxDistance: Float): Vec3 {
    val d = target - current
    val len = length(d)
    return if (len <= maxDistance || len == 0f) target else current + d * (maxDistance / len)
}
fun smoothDamp(current: Vec3, target: Vec3, velocity: Velocity<Vec3>, smoothTime: Float, deltaTime: Float,
               maxSpeed: Float = Float.POSITIVE_INFINITY): Vec3 {
    val st = maxOf(0.0001f, smoothTime)
    val omega = 2f / st
    val x = omega * deltaTime
    val exp = 1f / (1f + x + 0.48f * x * x + 0.235f * x * x * x)
    val change = clampLength(current - target, maxSpeed * st)
    val clampedTarget = current - change
    val temp = (velocity.value + change * omega) * deltaTime
    velocity.value = (velocity.value - temp * omega) * exp
    var output = clampedTarget + (change + temp) * exp
    if (dot(target - current, output - target) > 0f) {
        output = target
        velocity.value = (output - target) / deltaTime
    }
    return output
}
fun isFinite(v: Vec3) = v.x.isFinite() && v.y.isFinite() && v.z.isFinite()

// ---- matrices (kmath/matrix.h) ------------------------------------------------------------------------

fun transpose(m: Mat4) = Mat4(m.row(0), m.row(1), m.row(2), m.row(3))
fun transpose(m: Mat3) = Mat3(m.row(0), m.row(1), m.row(2))
fun determinant(m: Mat3) = dot(m[0], cross(m[1], m[2]))
fun determinant(m: Mat4): Float {
    val s0 = m[2, 2] * m[3, 3] - m[3, 2] * m[2, 3]; val s1 = m[2, 1] * m[3, 3] - m[3, 1] * m[2, 3]
    val s2 = m[2, 1] * m[3, 2] - m[3, 1] * m[2, 2]; val s3 = m[2, 0] * m[3, 3] - m[3, 0] * m[2, 3]
    val s4 = m[2, 0] * m[3, 2] - m[3, 0] * m[2, 2]; val s5 = m[2, 0] * m[3, 1] - m[3, 0] * m[2, 1]
    val c0 = +(m[1, 1] * s0 - m[1, 2] * s1 + m[1, 3] * s2)
    val c1 = -(m[1, 0] * s0 - m[1, 2] * s3 + m[1, 3] * s4)
    val c2 = +(m[1, 0] * s1 - m[1, 1] * s3 + m[1, 3] * s5)
    val c3 = -(m[1, 0] * s2 - m[1, 1] * s4 + m[1, 2] * s5)
    return m[0, 0] * c0 + m[0, 1] * c1 + m[0, 2] * c2 + m[0, 3] * c3
}
fun inverse(m: Mat3): Mat3 {
    val r0 = cross(m[1], m[2]); val r1 = cross(m[2], m[0]); val r2 = cross(m[0], m[1])
    val inv = 1f / dot(m[0], r0)
    return transpose(Mat3(r0 * inv, r1 * inv, r2 * inv))
}
/** The inverse; a singular matrix gives non-finite elements. */
fun inverse(m: Mat4): Mat4 {
    val a2323 = m[2, 2] * m[3, 3] - m[2, 3] * m[3, 2]; val a1323 = m[2, 1] * m[3, 3] - m[2, 3] * m[3, 1]
    val a1223 = m[2, 1] * m[3, 2] - m[2, 2] * m[3, 1]; val a0323 = m[2, 0] * m[3, 3] - m[2, 3] * m[3, 0]
    val a0223 = m[2, 0] * m[3, 2] - m[2, 2] * m[3, 0]; val a0123 = m[2, 0] * m[3, 1] - m[2, 1] * m[3, 0]
    val a2313 = m[1, 2] * m[3, 3] - m[1, 3] * m[3, 2]; val a1313 = m[1, 1] * m[3, 3] - m[1, 3] * m[3, 1]
    val a1213 = m[1, 1] * m[3, 2] - m[1, 2] * m[3, 1]; val a2312 = m[1, 2] * m[2, 3] - m[1, 3] * m[2, 2]
    val a1312 = m[1, 1] * m[2, 3] - m[1, 3] * m[2, 1]; val a1212 = m[1, 1] * m[2, 2] - m[1, 2] * m[2, 1]
    val a0313 = m[1, 0] * m[3, 3] - m[1, 3] * m[3, 0]; val a0213 = m[1, 0] * m[3, 2] - m[1, 2] * m[3, 0]
    val a0312 = m[1, 0] * m[2, 3] - m[1, 3] * m[2, 0]; val a0212 = m[1, 0] * m[2, 2] - m[1, 2] * m[2, 0]
    val a0113 = m[1, 0] * m[3, 1] - m[1, 1] * m[3, 0]; val a0112 = m[1, 0] * m[2, 1] - m[1, 1] * m[2, 0]
    val det = m[0, 0] * (m[1, 1] * a2323 - m[1, 2] * a1323 + m[1, 3] * a1223) -
              m[0, 1] * (m[1, 0] * a2323 - m[1, 2] * a0323 + m[1, 3] * a0223) +
              m[0, 2] * (m[1, 0] * a1323 - m[1, 1] * a0323 + m[1, 3] * a0123) -
              m[0, 3] * (m[1, 0] * a1223 - m[1, 1] * a0223 + m[1, 2] * a0123)
    val inv = 1f / det
    val r = FloatArray(16)
    fun set(c: Int, row: Int, v: Float) { r[c * 4 + row] = v }
    set(0, 0, inv * (m[1, 1] * a2323 - m[1, 2] * a1323 + m[1, 3] * a1223))
    set(0, 1, inv * -(m[0, 1] * a2323 - m[0, 2] * a1323 + m[0, 3] * a1223))
    set(0, 2, inv * (m[0, 1] * a2313 - m[0, 2] * a1313 + m[0, 3] * a1213))
    set(0, 3, inv * -(m[0, 1] * a2312 - m[0, 2] * a1312 + m[0, 3] * a1212))
    set(1, 0, inv * -(m[1, 0] * a2323 - m[1, 2] * a0323 + m[1, 3] * a0223))
    set(1, 1, inv * (m[0, 0] * a2323 - m[0, 2] * a0323 + m[0, 3] * a0223))
    set(1, 2, inv * -(m[0, 0] * a2313 - m[0, 2] * a0313 + m[0, 3] * a0213))
    set(1, 3, inv * (m[0, 0] * a2312 - m[0, 2] * a0312 + m[0, 3] * a0212))
    set(2, 0, inv * (m[1, 0] * a1323 - m[1, 1] * a0323 + m[1, 3] * a0123))
    set(2, 1, inv * -(m[0, 0] * a1323 - m[0, 1] * a0323 + m[0, 3] * a0123))
    set(2, 2, inv * (m[0, 0] * a1313 - m[0, 1] * a0313 + m[0, 3] * a0113))
    set(2, 3, inv * -(m[0, 0] * a1312 - m[0, 1] * a0312 + m[0, 3] * a0112))
    set(3, 0, inv * -(m[1, 0] * a1223 - m[1, 1] * a0223 + m[1, 2] * a0123))
    set(3, 1, inv * (m[0, 0] * a1223 - m[0, 1] * a0223 + m[0, 2] * a0123))
    set(3, 2, inv * -(m[0, 0] * a1213 - m[0, 1] * a0213 + m[0, 2] * a0113))
    set(3, 3, inv * (m[0, 0] * a1212 - m[0, 1] * a0212 + m[0, 2] * a0112))
    return Mat4(r)
}
/** The matrix that transforms normals for [model]: the inverse transpose of its 3x3. */
fun normalMatrix(model: Mat4) = transpose(inverse(Mat3(model)))
fun approxEqual(a: Mat4, b: Mat4, epsilon: Float = Epsilon) = (0 until 4).all { approxEqual(a[it], b[it], epsilon) }
fun approxEqual(a: Mat3, b: Mat3, epsilon: Float = Epsilon) = (0 until 3).all { approxEqual(a[it], b[it], epsilon) }

// ---- quaternions (kmath/quaternion.h) -----------------------------------------------------------------

fun dot(a: Quat, b: Quat) = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w
fun length(q: Quat) = ksqrt(dot(q, q))
fun normalize(q: Quat): Quat { val len = length(q); return if (len > 0f) q * (1f / len) else Quat.Identity }
fun conjugate(q: Quat) = Quat(-q.x, -q.y, -q.z, q.w)
fun inverse(q: Quat) = conjugate(q) * (1f / dot(q, q))
fun angle(q: Quat) = 2f * kacos(clamp(q.w, -1f, 1f))
fun axis(q: Quat): Vec3 { val s2 = 1f - q.w * q.w; return if (s2 <= 0f) Vec3.UnitZ else q.xyz * (1f / ksqrt(s2)) }
/** (pitch, yaw, roll) in radians. */
fun eulerAngles(q: Quat): Vec3 {
    val py = 2f * (q.y * q.z + q.w * q.x)
    val px = q.w * q.w - q.x * q.x - q.y * q.y + q.z * q.z
    val pitch = if (kabs(px) < 1e-7f && kabs(py) < 1e-7f) 2f * katan2(q.x, q.w) else katan2(py, px)
    val yaw = kotlin.math.asin(clamp(-2f * (q.x * q.z - q.w * q.y), -1f, 1f))
    val roll = katan2(2f * (q.x * q.y + q.w * q.z), q.w * q.w + q.x * q.x - q.y * q.y - q.z * q.z)
    return Vec3(pitch, yaw, roll)
}
fun toMat3(q: Quat): Mat3 {
    val xx = q.x * q.x; val yy = q.y * q.y; val zz = q.z * q.z; val xz = q.x * q.z; val xy = q.x * q.y; val yz = q.y * q.z
    val wx = q.w * q.x; val wy = q.w * q.y; val wz = q.w * q.z
    return Mat3(Vec3(1f - 2f * (yy + zz), 2f * (xy + wz), 2f * (xz - wy)),
                Vec3(2f * (xy - wz), 1f - 2f * (xx + zz), 2f * (yz + wx)),
                Vec3(2f * (xz + wy), 2f * (yz - wx), 1f - 2f * (xx + yy)))
}
fun toMat4(q: Quat) = Mat4(toMat3(q))
fun nlerp(a: Quat, b: Quat, t: Float): Quat { val bb = if (dot(a, b) < 0f) -b else b; return normalize(a + (bb - a) * t) }
fun slerp(a: Quat, b: Quat, t: Float): Quat {
    var bb = b
    var cosTheta = dot(a, b)
    if (cosTheta < 0f) { bb = -b; cosTheta = -cosTheta }
    if (cosTheta > 1f - 1e-6f) return nlerp(a, bb, t)
    val angle = kacos(cosTheta)
    return (a * kotlin.math.sin((1f - t) * angle) + bb * kotlin.math.sin(t * angle)) * (1f / kotlin.math.sin(angle))
}
fun rotateTowards(from: Quat, to: Quat, maxRadians: Float): Quat {
    val angle = 2f * kacos(clamp(kabs(dot(from, to)), 0f, 1f))
    return if (angle <= maxRadians || angle == 0f) to else slerp(from, to, maxRadians / angle)
}
fun approxEqual(a: Quat, b: Quat, epsilon: Float = Epsilon) =
    approxEqual(a.x, b.x, epsilon) && approxEqual(a.y, b.y, epsilon) && approxEqual(a.z, b.z, epsilon) && approxEqual(a.w, b.w, epsilon)
/** The same rotation, whichever of q and -q was given. */
fun sameRotation(a: Quat, b: Quat, epsilon: Float = Epsilon) = approxEqual(a, b, epsilon) || approxEqual(a, -b, epsilon)

// ---- transforms and cameras (kmath/transform.h) -------------------------------------------------------

fun translation(by: Vec3) = Mat4().withColumn(3, Vec4(by, 1f))
fun scaling(by: Vec3) = Mat4(Vec4(by.x, 0f, 0f, 0f), Vec4(0f, by.y, 0f, 0f), Vec4(0f, 0f, by.z, 0f), Vec4.UnitW)
fun rotation(angle: Float, axis: Vec3) = toMat4(Quat.angleAxis(angle, axis))
fun rotation(q: Quat) = toMat4(q)
/** Translation * Rotation * Scale: scale first, then rotate, then move. */
fun compose(translation: Vec3, rotation: Quat, scale: Vec3): Mat4 {
    val r = toMat3(rotation)
    return Mat4(Vec4(r[0] * scale.x, 0f), Vec4(r[1] * scale.y, 0f), Vec4(r[2] * scale.z, 0f), Vec4(translation, 1f))
}
/** Splits an affine matrix back into what compose took (as a Transform); null for a degenerate one. */
fun decompose(m: Mat4): Transform? {
    val c0 = Vec3(m[0]); val c1 = Vec3(m[1]); val c2 = Vec3(m[2])
    var scale = Vec3(length(c0), length(c1), length(c2))
    if (scale.x == 0f || scale.y == 0f || scale.z == 0f) return null
    if (dot(c0, cross(c1, c2)) < 0f) scale = scale.copy(x = -scale.x)
    return Transform(Vec3(m[3]), normalize(Quat.fromMatrix(Mat3(c0 / scale.x, c1 / scale.y, c2 / scale.z))), scale)
}
/** m * translation(by). */
fun translate(m: Mat4, by: Vec3) = m.withColumn(3, m[0] * by.x + m[1] * by.y + m[2] * by.z + m[3])
/** m * rotation(angle, axis). */
fun rotate(m: Mat4, angle: Float, axis: Vec3) = m * rotation(angle, axis)
/** m * scaling(by). */
fun scale(m: Mat4, by: Vec3) = Mat4(m[0] * by.x, m[1] * by.y, m[2] * by.z, m[3])

/** A right-handed view matrix: the camera at [eye] looking at [target]. */
fun lookAt(eye: Vec3, target: Vec3, up: Vec3 = Vec3.Up): Mat4 {
    val f = normalize(target - eye)
    val s = normalize(cross(f, up))
    val u = cross(s, f)
    return Mat4(Vec4(s.x, u.x, -f.x, 0f), Vec4(s.y, u.y, -f.y, 0f), Vec4(s.z, u.z, -f.z, 0f), Vec4(-dot(s, eye), -dot(u, eye), dot(f, eye), 1f))
}
/** Perspective projection, vertical field of view in radians, depth 0 at near to 1 at far. Y is not flipped. */
fun perspective(fovY: Float, aspect: Float, near: Float, far: Float): Mat4 {
    val f = 1f / ktan(fovY * 0.5f)
    return Mat4(Vec4(f / aspect, 0f, 0f, 0f), Vec4(0f, f, 0f, 0f), Vec4(0f, 0f, far / (near - far), -1f), Vec4(0f, 0f, -(far * near) / (far - near), 0f))
}
/** Reversed depth (1 at near, 0 at far); an infinite [far] for no far plane. */
fun perspectiveReversedZ(fovY: Float, aspect: Float, near: Float, far: Float = Float.POSITIVE_INFINITY): Mat4 {
    val f = 1f / ktan(fovY * 0.5f)
    val c2 = if (far.isInfinite()) 0f else near / (far - near)
    val c3 = if (far.isInfinite()) near else far * near / (far - near)
    return Mat4(Vec4(f / aspect, 0f, 0f, 0f), Vec4(0f, f, 0f, 0f), Vec4(0f, 0f, c2, -1f), Vec4(0f, 0f, c3, 0f))
}
fun orthographic(left: Float, right: Float, bottom: Float, top: Float, near: Float, far: Float) = Mat4(
    Vec4(2f / (right - left), 0f, 0f, 0f), Vec4(0f, 2f / (top - bottom), 0f, 0f), Vec4(0f, 0f, -1f / (far - near), 0f),
    Vec4(-(right + left) / (right - left), -(top + bottom) / (top - bottom), -near / (far - near), 1f))
/** m * (p, 1), without the projective divide. */
fun transformPoint(m: Mat4, p: Vec3) = Vec3(m[0]) * p.x + Vec3(m[1]) * p.y + Vec3(m[2]) * p.z + Vec3(m[3])
fun transformPointProjective(m: Mat4, p: Vec3): Vec3 { val h = m * Vec4(p, 1f); return Vec3(h) / h.w }
fun transformDirection(m: Mat4, d: Vec3) = Vec3(m[0]) * d.x + Vec3(m[1]) * d.y + Vec3(m[2]) * d.z
