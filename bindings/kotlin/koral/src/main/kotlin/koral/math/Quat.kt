package koral

/**
 * kor::Quat: a rotation, stored x, y, z, w. `a * b` rotates by b, then by a; `q * v` rotates a vector.
 * `Quat()` is the identity.
 */
data class Quat(val x: Float, val y: Float, val z: Float, val w: Float) {
    constructor() : this(0f, 0f, 0f, 1f)
    constructor(xyz: Vec3, w: Float) : this(xyz.x, xyz.y, xyz.z, w)

    val xyz: Vec3 get() = Vec3(x, y, z)

    operator fun times(q: Quat) = Quat(
        w * q.x + x * q.w + y * q.z - z * q.y,
        w * q.y + y * q.w + z * q.x - x * q.z,
        w * q.z + z * q.w + x * q.y - y * q.x,
        w * q.w - x * q.x - y * q.y - z * q.z)
    /** [v] rotated by this (unit) quaternion. */
    operator fun times(v: Vec3): Vec3 {
        val u = xyz
        val uv = cross(u, v)
        val uuv = cross(u, uv)
        return v + (uv * w + uuv) * 2f
    }
    operator fun times(s: Float) = Quat(x * s, y * s, z * s, w * s)
    operator fun plus(q: Quat) = Quat(x + q.x, y + q.y, z + q.z, w + q.w)
    operator fun minus(q: Quat) = Quat(x - q.x, y - q.y, z - q.z, w - q.w)
    operator fun unaryMinus() = Quat(-x, -y, -z, -w)
    fun toArray() = floatArrayOf(x, y, z, w)
    override fun toString() = "Quat($x, $y, $z, $w)"

    companion object {
        val Identity = Quat()

        /** [angle] radians counter-clockwise about [axis] (normalised for you). */
        fun angleAxis(angle: Float, axis: Vec3): Quat {
            val s = kotlin.math.sin(angle * 0.5f)
            return Quat(normalize(axis) * s, kotlin.math.cos(angle * 0.5f))
        }
        /** From Euler angles (pitch, yaw, roll) in radians. */
        fun fromEuler(euler: Vec3): Quat {
            val h = euler * 0.5f
            val cx = kotlin.math.cos(h.x); val cy = kotlin.math.cos(h.y); val cz = kotlin.math.cos(h.z)
            val sx = kotlin.math.sin(h.x); val sy = kotlin.math.sin(h.y); val sz = kotlin.math.sin(h.z)
            return Quat(sx * cy * cz - cx * sy * sz, cx * sy * cz + sx * cy * sz, cx * cy * sz - sx * sy * cz, cx * cy * cz + sx * sy * sz)
        }
        /** The rotation of a rotation matrix (orthonormal columns). */
        fun fromMatrix(m: Mat3): Quat {
            val fourXSq = m[0, 0] - m[1, 1] - m[2, 2]
            val fourYSq = m[1, 1] - m[0, 0] - m[2, 2]
            val fourZSq = m[2, 2] - m[0, 0] - m[1, 1]
            val fourWSq = m[0, 0] + m[1, 1] + m[2, 2]
            var biggest = 0
            var fourBiggestSq = fourWSq
            if (fourXSq > fourBiggestSq) { fourBiggestSq = fourXSq; biggest = 1 }
            if (fourYSq > fourBiggestSq) { fourBiggestSq = fourYSq; biggest = 2 }
            if (fourZSq > fourBiggestSq) { fourBiggestSq = fourZSq; biggest = 3 }
            val big = kotlin.math.sqrt(fourBiggestSq + 1f) * 0.5f
            val mult = 0.25f / big
            return when (biggest) {
                0 -> Quat((m[1, 2] - m[2, 1]) * mult, (m[2, 0] - m[0, 2]) * mult, (m[0, 1] - m[1, 0]) * mult, big)
                1 -> Quat(big, (m[0, 1] + m[1, 0]) * mult, (m[2, 0] + m[0, 2]) * mult, (m[1, 2] - m[2, 1]) * mult)
                2 -> Quat((m[0, 1] + m[1, 0]) * mult, big, (m[1, 2] + m[2, 1]) * mult, (m[2, 0] - m[0, 2]) * mult)
                else -> Quat((m[2, 0] + m[0, 2]) * mult, (m[1, 2] + m[2, 1]) * mult, big, (m[0, 1] - m[1, 0]) * mult)
            }
        }
        fun fromMatrix(m: Mat4) = fromMatrix(Mat3(m))
        /** The rotation that turns -Z (forward) toward [direction] with +Y toward [up]. */
        fun lookRotation(direction: Vec3, up: Vec3 = Vec3.Up): Quat {
            val c2 = -normalize(direction)
            val right = cross(up, c2)
            val c0 = right * (1f / kotlin.math.sqrt(kotlin.math.max(1e-5f, dot(right, right))))
            return fromMatrix(Mat3(c0, cross(c2, c0), c2))
        }
        /** The shortest rotation turning direction [from] onto [to]. */
        fun fromTo(from: Vec3, to: Vec3): Quat {
            val f = normalize(from)
            val t = normalize(to)
            val d = dot(f, t)
            if (d >= 1f - 1e-6f) return Identity
            if (d <= -1f + 1e-6f) return Quat(anyPerpendicular(f), 0f)
            val s = kotlin.math.sqrt((1f + d) * 2f)
            return Quat(cross(f, t) * (1f / s), s * 0.5f)
        }
    }
}
