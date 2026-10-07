package koral

/**
 * kor::Mat4: sixteen floats, column after column (`m[column * 4 + row]`), the layout of the shaders. `m[c]` is
 * column c and `m[c, r]` the element in row r. Vectors are columns: `projection * view * model * point` applies
 * model first, as in C++ and GLSL. Immutable; `Mat4()` is the identity, as C++'s is.
 */
class Mat4(values: FloatArray) {
    init { require(values.size == 16) { "a Mat4 is 16 floats" } }
    private val m = values.copyOf()

    /** The identity. */
    constructor() : this(floatArrayOf(1f, 0f, 0f, 0f, 0f, 1f, 0f, 0f, 0f, 0f, 1f, 0f, 0f, 0f, 0f, 1f))
    /** [diagonal] on the diagonal, zero elsewhere. */
    constructor(diagonal: Float) : this(FloatArray(16) { if (it % 5 == 0) diagonal else 0f })
    constructor(c0: Vec4, c1: Vec4, c2: Vec4, c3: Vec4) : this(c0.toArray() + c1.toArray() + c2.toArray() + c3.toArray())
    /** The 3x3 in the upper left, the rest the identity's. */
    constructor(m: Mat3) : this(Vec4(m[0], 0f), Vec4(m[1], 0f), Vec4(m[2], 0f), Vec4.UnitW)

    operator fun get(column: Int, row: Int): Float = m[column * 4 + row]
    operator fun get(column: Int): Vec4 = Vec4(m[column * 4], m[column * 4 + 1], m[column * 4 + 2], m[column * 4 + 3])
    fun row(r: Int) = Vec4(m[r], m[4 + r], m[8 + r], m[12 + r])
    /** The sixteen floats, column after column. */
    fun toArray(): FloatArray = m.copyOf()
    /** A copy with one element changed. */
    fun with(column: Int, row: Int, value: Float) = Mat4(m.copyOf().also { it[column * 4 + row] = value })
    /** A copy with one column replaced. */
    fun withColumn(column: Int, value: Vec4) = Mat4(m.copyOf().also { value.toArray().copyInto(it, column * 4) })

    /** this * o: o applied first. */
    operator fun times(o: Mat4) = Mat4(this * o[0], this * o[1], this * o[2], this * o[3])
    /** Matrix times column vector: ((c0 * x + c1 * y) + c2 * z) + c3 * w, C++'s order. */
    operator fun times(v: Vec4): Vec4 = this[0] * v.x + this[1] * v.y + this[2] * v.z + this[3] * v.w
    operator fun times(s: Float) = Mat4(FloatArray(16) { m[it] * s })
    operator fun plus(o: Mat4) = Mat4(FloatArray(16) { m[it] + o.m[it] })
    operator fun minus(o: Mat4) = Mat4(FloatArray(16) { m[it] - o.m[it] })

    override fun equals(other: Any?) = other is Mat4 && m.contentEquals(other.m)
    override fun hashCode() = m.contentHashCode()
    override fun toString() = (0 until 4).joinToString("; ", "[", "]") { row(it).toString() }

    companion object {
        val Identity = Mat4()
        val Zero = Mat4(0f)

        // The Kotlin bindings' earlier names, kept: the top-level functions are kmath's.
        fun translation(by: Vec3) = koral.translation(by)
        fun scale(by: Vec3) = scaling(by)
        fun rotation(radians: Float, axis: Vec3) = koral.rotation(radians, axis)
        fun perspective(fovYRadians: Float, aspect: Float, near: Float, far: Float) = koral.perspective(fovYRadians, aspect, near, far)
        fun lookAt(eye: Vec3, center: Vec3, up: Vec3 = Vec3.Up) = koral.lookAt(eye, center, up)
    }
}

/** kor::Mat3: three columns, as [Mat4]. */
class Mat3(values: FloatArray) {
    init { require(values.size == 9) { "a Mat3 is 9 floats" } }
    private val m = values.copyOf()

    constructor() : this(floatArrayOf(1f, 0f, 0f, 0f, 1f, 0f, 0f, 0f, 1f))
    constructor(diagonal: Float) : this(FloatArray(9) { if (it % 4 == 0) diagonal else 0f })
    constructor(c0: Vec3, c1: Vec3, c2: Vec3) : this(c0.toArray() + c1.toArray() + c2.toArray())
    /** The upper-left 3x3 of a Mat4. */
    constructor(m: Mat4) : this(Vec3(m[0]), Vec3(m[1]), Vec3(m[2]))

    operator fun get(column: Int, row: Int): Float = m[column * 3 + row]
    operator fun get(column: Int): Vec3 = Vec3(m[column * 3], m[column * 3 + 1], m[column * 3 + 2])
    fun row(r: Int) = Vec3(m[r], m[3 + r], m[6 + r])
    fun toArray(): FloatArray = m.copyOf()

    operator fun times(o: Mat3) = Mat3(this * o[0], this * o[1], this * o[2])
    operator fun times(v: Vec3): Vec3 = this[0] * v.x + this[1] * v.y + this[2] * v.z
    operator fun times(s: Float) = Mat3(FloatArray(9) { m[it] * s })

    override fun equals(other: Any?) = other is Mat3 && m.contentEquals(other.m)
    override fun hashCode() = m.contentHashCode()
    override fun toString() = (0 until 3).joinToString("; ", "[", "]") { row(it).toString() }

    companion object {
        val Identity = Mat3()
        val Zero = Mat3(0f)
    }
}

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
