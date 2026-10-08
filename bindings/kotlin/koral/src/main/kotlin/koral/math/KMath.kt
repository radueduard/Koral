@file:Suppress("NOTHING_TO_INLINE")

package koral

import kotlin.math.abs as kabs
import kotlin.math.acos as kacos
import kotlin.math.atan2 as katan2
import kotlin.math.sqrt as ksqrt
import kotlin.math.tan as ktan

// kmath's free functions, top-level and under the same names (in Kotlin's lowerCamelCase): dot, cross,
// normalize, perspective, ... The GLSL chapters (trigonometric, exponential, common, geometric, relational,
// integer, packing) and the matrices are generated (generated/Glsl.kt, generated/Matrices.kt) for every type
// C++ has them for; this file is the rest. Each does its arithmetic in the order the C++ one does, so what is
// only additions, multiplications, divisions, square roots and floor gives the same bits as Koral itself (the
// tests check it against the C interface).

/** The state a smoothDamp spring keeps between calls: C++'s `velocity` reference. */
typealias Velocity<T> = Ref<T>

// The Kotlin bindings' earlier names on Vec3, kept.
infix fun Vec3.dot(o: Vec3) = koral.dot(this, o)
infix fun Vec3.cross(o: Vec3) = koral.cross(this, o)
val Vec3.length: Float get() = koral.length(this)
fun Vec3.normalized(): Vec3 = normalize(this)

/** Gram–Schmidt: (unit normal, unit tangent perpendicular to it). */
fun orthoNormalize(normal: Vec3, tangent: Vec3): Pair<Vec3, Vec3> {
    val n = normalize(normal)
    return n to normalizeOr(tangent - n * dot(tangent, n), anyPerpendicular(n))
}

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
/** Rotation about X, as eulerAngles(q).x. */
fun pitch(q: Quat): Float {
    val py = 2f * (q.y * q.z + q.w * q.x)
    val px = q.w * q.w - q.x * q.x - q.y * q.y + q.z * q.z
    return if (kabs(px) < 1e-7f && kabs(py) < 1e-7f) 2f * katan2(q.x, q.w) else katan2(py, px)
}
/** Rotation about Y. */
fun yaw(q: Quat) = kotlin.math.asin(clamp(-2f * (q.x * q.z - q.w * q.y), -1f, 1f))
/** Rotation about Z. */
fun roll(q: Quat) = katan2(2f * (q.x * q.y + q.w * q.z), q.w * q.w + q.x * q.x - q.y * q.y - q.z * q.z)
/** q followed by [angle] radians about [axis] in q's frame: q * angleAxis(angle, axis). */
fun rotate(q: Quat, angle: Float, axis: Vec3) = q * Quat.angleAxis(angle, axis)
/** [v] turned [angle] radians about [axis]. */
fun rotate(v: Vec3, angle: Float, axis: Vec3) = Quat.angleAxis(angle, axis) * v
/** Component-wise, not normalised: slerp or nlerp for rotations. */
fun lerp(a: Quat, b: Quat, t: Float) = a + (b - a) * t
/** glm::mix of quaternions: slerp. */
fun mix(a: Quat, b: Quat, t: Float) = slerp(a, b, t)
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

/** A right-handed view matrix: the camera at [eye] looking at [target] (down -Z). */
fun lookAt(eye: Vec3, target: Vec3, up: Vec3 = Vec3.Up): Mat4 {
    val f = normalize(target - eye)
    val s = normalize(cross(f, up))
    val u = cross(s, f)
    return Mat4(Vec4(s.x, u.x, -f.x, 0f), Vec4(s.y, u.y, -f.y, 0f), Vec4(s.z, u.z, -f.z, 0f), Vec4(-dot(s, eye), -dot(u, eye), dot(f, eye), 1f))
}
/** lookAt by its right-handed name. */
fun lookAtRH(eye: Vec3, target: Vec3, up: Vec3 = Vec3.Up) = lookAt(eye, target, up)
/** A left-handed view matrix: the camera looks down +Z. */
fun lookAtLH(eye: Vec3, target: Vec3, up: Vec3 = Vec3.Up): Mat4 {
    val f = normalize(target - eye)
    val s = normalize(cross(up, f))
    val u = cross(f, s)
    return Mat4(Vec4(s.x, u.x, f.x, 0f), Vec4(s.y, u.y, f.y, 0f), Vec4(s.z, u.z, f.z, 0f), Vec4(-dot(s, eye), -dot(u, eye), -dot(f, eye), 1f))
}

private fun ClipSpace.leftHanded() = this == ClipSpace.LeftHandedZeroToOne || this == ClipSpace.LeftHandedNegativeOneToOne
private fun ClipSpace.zeroToOne() = this == ClipSpace.RightHandedZeroToOne || this == ClipSpace.LeftHandedZeroToOne
private operator fun FloatArray.set(column: Int, row: Int, value: Float) { this[column * 4 + row] = value }
private inline fun mat4(identity: Boolean, build: FloatArray.() -> Unit) = Mat4(FloatArray(16) { if (identity && it % 5 == 0) 1f else 0f }.apply(build))
private fun FloatArray.perspectiveDepth(near: Float, far: Float, clip: ClipSpace) {
    this[2, 3] = if (clip.leftHanded()) 1f else -1f
    if (clip.zeroToOne()) {
        this[2, 2] = if (clip.leftHanded()) far / (far - near) else far / (near - far)
        this[3, 2] = -(far * near) / (far - near)
    } else {
        this[2, 2] = if (clip.leftHanded()) (far + near) / (far - near) else -(far + near) / (far - near)
        this[3, 2] = -(2f * far * near) / (far - near)
    }
}
/** Perspective projection, vertical field of view in radians; by default depth 0 at near to 1 at far. Y is not flipped. */
fun perspective(fovY: Float, aspect: Float, near: Float, far: Float, clip: ClipSpace = ClipSpace.RightHandedZeroToOne): Mat4 {
    val f = 1f / ktan(fovY * 0.5f)
    return mat4(false) { this[0, 0] = f / aspect; this[1, 1] = f; perspectiveDepth(near, far, clip) }
}
/** Perspective from a field of view and the viewport's size in pixels. */
fun perspectiveFov(fov: Float, width: Float, height: Float, near: Float, far: Float, clip: ClipSpace = ClipSpace.RightHandedZeroToOne): Mat4 {
    val h = kotlin.math.cos(0.5f * fov) / kotlin.math.sin(0.5f * fov)
    return mat4(false) { this[0, 0] = h * height / width; this[1, 1] = h; perspectiveDepth(near, far, clip) }
}
/** Perspective with no far plane. */
fun infinitePerspective(fovY: Float, aspect: Float, near: Float, clip: ClipSpace = ClipSpace.RightHandedZeroToOne): Mat4 {
    val range = ktan(fovY * 0.5f) * near
    return mat4(false) {
        this[0, 0] = (2f * near) / (range * aspect * 2f)
        this[1, 1] = (2f * near) / (range * 2f)
        this[2, 2] = if (clip.leftHanded()) 1f else -1f
        this[2, 3] = if (clip.leftHanded()) 1f else -1f
        this[3, 2] = if (clip.zeroToOne()) -near else -2f * near
    }
}
/** An off-centre perspective (glm::frustum): the view volume of the near-plane rectangle. */
fun frustumProjection(left: Float, right: Float, bottom: Float, top: Float, near: Float, far: Float,
                      clip: ClipSpace = ClipSpace.RightHandedZeroToOne): Mat4 = mat4(false) {
    this[0, 0] = (2f * near) / (right - left)
    this[1, 1] = (2f * near) / (top - bottom)
    val side = if (clip.leftHanded()) -1f else 1f
    this[2, 0] = side * (right + left) / (right - left)
    this[2, 1] = side * (top + bottom) / (top - bottom)
    perspectiveDepth(near, far, clip)
}
/** Reversed depth (1 at near, 0 at far); an infinite [far] for no far plane. */
fun perspectiveReversedZ(fovY: Float, aspect: Float, near: Float, far: Float = Float.POSITIVE_INFINITY): Mat4 {
    val f = 1f / ktan(fovY * 0.5f)
    val c2 = if (far.isInfinite()) 0f else near / (far - near)
    val c3 = if (far.isInfinite()) near else far * near / (far - near)
    return Mat4(Vec4(f / aspect, 0f, 0f, 0f), Vec4(0f, f, 0f, 0f), Vec4(0f, 0f, c2, -1f), Vec4(0f, 0f, c3, 0f))
}
/** Orthographic projection of the box [left, right] x [bottom, top] x [near, far] in front of the camera. */
fun orthographic(left: Float, right: Float, bottom: Float, top: Float, near: Float, far: Float,
                 clip: ClipSpace = ClipSpace.RightHandedZeroToOne): Mat4 = mat4(true) {
    this[0, 0] = 2f / (right - left)
    this[1, 1] = 2f / (top - bottom)
    this[3, 0] = -(right + left) / (right - left)
    this[3, 1] = -(top + bottom) / (top - bottom)
    val sign = if (clip.leftHanded()) 1f else -1f
    if (clip.zeroToOne()) {
        this[2, 2] = sign / (far - near)
        this[3, 2] = -near / (far - near)
    } else {
        this[2, 2] = sign * 2f / (far - near)
        this[3, 2] = -(far + near) / (far - near)
    }
}
/** A 2D orthographic projection: x and y only (glm's four-argument ortho). */
fun orthographic(left: Float, right: Float, bottom: Float, top: Float): Mat4 = mat4(true) {
    this[0, 0] = 2f / (right - left)
    this[1, 1] = 2f / (top - bottom)
    this[2, 2] = -1f
    this[3, 0] = -(right + left) / (right - left)
    this[3, 1] = -(top + bottom) / (top - bottom)
}
/** Where [obj] lands in window coordinates; [viewport] is (x, y, width, height). */
fun project(obj: Vec3, model: Mat4, projection: Mat4, viewport: Vec4, clip: ClipSpace = ClipSpace.RightHandedZeroToOne): Vec3 {
    var v = projection * (model * Vec4(obj, 1f))
    v /= v.w
    v = if (clip.zeroToOne()) Vec4(v.x * 0.5f + 0.5f, v.y * 0.5f + 0.5f, v.z, v.w) else v * 0.5f + 0.5f
    return Vec3(v.x * viewport.z + viewport.x, v.y * viewport.w + viewport.y, v.z)
}
/** The inverse of project: the point in object space under window coordinates [window]. */
fun unProject(window: Vec3, model: Mat4, projection: Mat4, viewport: Vec4, clip: ClipSpace = ClipSpace.RightHandedZeroToOne): Vec3 {
    val inverse = inverse(projection * model)
    var v = Vec4((window.x - viewport.x) / viewport.z, (window.y - viewport.y) / viewport.w, window.z, 1f)
    v = if (clip.zeroToOne()) Vec4(v.x * 2f - 1f, v.y * 2f - 1f, v.z, v.w) else v * 2f - 1f
    var o = inverse * v
    o /= o.w
    return Vec3(o)
}
/** A matrix that narrows a projection to the [size]-pixel region around [center]: for picking. */
fun pickMatrix(center: Vec2, size: Vec2, viewport: Vec4): Mat4 {
    val m = Mat4()
    if (!(size.x > 0f && size.y > 0f)) return m
    val t = Vec3((viewport.z - 2f * (center.x - viewport.x)) / size.x, (viewport.w - 2f * (center.y - viewport.y)) / size.y, 0f)
    return scale(translate(m, t), Vec3(viewport.z / size.x, viewport.w / size.y, 1f))
}

// ---- Euler angles (kmath/euler.h, glm's gtx/euler_angle) ---------------------------------------------

internal fun axisRotation(axis: Int, angle: Float): Mat4 {
    val c = kotlin.math.cos(angle)
    val s = kotlin.math.sin(angle)
    val u = (axis + 1) % 3
    val v = (axis + 2) % 3
    return mat4(true) { this[u, u] = c; this[u, v] = s; this[v, u] = -s; this[v, v] = c }
}
private val eulerAxes = arrayOf(intArrayOf(0, 1, 2), intArrayOf(0, 2, 1), intArrayOf(1, 0, 2), intArrayOf(1, 2, 0), intArrayOf(2, 0, 1),
    intArrayOf(2, 1, 0), intArrayOf(0, 1, 0), intArrayOf(0, 2, 0), intArrayOf(1, 0, 1), intArrayOf(1, 2, 1), intArrayOf(2, 0, 2), intArrayOf(2, 1, 2))
fun eulerAngleX(angle: Float) = axisRotation(0, angle)
fun eulerAngleY(angle: Float) = axisRotation(1, angle)
fun eulerAngleZ(angle: Float) = axisRotation(2, angle)
/** The rotation [angles] = (first, second, third) describe in [order], outermost first. */
fun eulerAngles(order: EulerOrder, angles: Vec3): Mat4 {
    val a = eulerAxes[order.ordinal]
    return axisRotation(a[0], angles.x) * axisRotation(a[1], angles.y) * axisRotation(a[2], angles.z)
}
/** The angles that rebuild [m]'s rotation in [order]; at gimbal lock the third is 0. */
fun extractEulerAngles(order: EulerOrder, m: Mat4): Vec3 {
    val a = eulerAxes[order.ordinal]
    fun at(row: Int, column: Int) = m[column, row]
    val lock = 1e-6f
    if (a[0] != a[2]) {
        val i = a[0]; val j = a[1]; val k = a[2]
        val e = if ((j - i + 3) % 3 == 1) 1f else -1f
        val cb = ksqrt(at(i, i) * at(i, i) + at(i, j) * at(i, j))
        val b = katan2(e * at(i, k), cb)
        if (cb < lock) return Vec3(katan2(e * at(k, j), at(j, j)), b, 0f)
        return Vec3(katan2(-e * at(j, k), at(k, k)), b, katan2(-e * at(i, j), at(i, i)))
    }
    val i = a[0]; val j = a[1]; val k = 3 - i - j
    val e = if ((j - i + 3) % 3 == 1) 1f else -1f
    val sb = ksqrt(at(i, j) * at(i, j) + at(i, k) * at(i, k))
    val b = katan2(sb, at(i, i))
    if (sb < lock) return Vec3(katan2(e * at(k, j), at(j, j)), b, 0f)
    return Vec3(katan2(at(j, i), -e * at(k, i)), b, katan2(at(i, j), e * at(i, k)))
}
/** glm::yawPitchRoll: Y(yaw) * X(pitch) * Z(roll). */
fun yawPitchRoll(yaw: Float, pitch: Float, roll: Float) = eulerAngles(EulerOrder.YXZ, Vec3(yaw, pitch, roll))
/** The quaternion of eulerAngles(order, angles). */
fun quatFromEuler(order: EulerOrder, angles: Vec3): Quat {
    val a = eulerAxes[order.ordinal]
    fun axis(i: Int) = if (i == 0) Vec3.UnitX else if (i == 1) Vec3.UnitY else Vec3.UnitZ
    return Quat.angleAxis(angles.x, axis(a[0])) * Quat.angleAxis(angles.y, axis(a[1])) * Quat.angleAxis(angles.z, axis(a[2]))
}

/** m * (p, 1), without the projective divide. */
fun transformPoint(m: Mat4, p: Vec3) = Vec3(m[0]) * p.x + Vec3(m[1]) * p.y + Vec3(m[2]) * p.z + Vec3(m[3])
fun transformPointProjective(m: Mat4, p: Vec3): Vec3 { val h = m * Vec4(p, 1f); return Vec3(h) / h.w }
fun transformDirection(m: Mat4, d: Vec3) = Vec3(m[0]) * d.x + Vec3(m[1]) * d.y + Vec3(m[2]) * d.z
