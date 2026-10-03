package koral

import kotlin.math.cos
import kotlin.math.sin
import kotlin.math.sqrt
import kotlin.math.tan

// glm's vectors and matrices, as values: what crosses into Koral as the glm type of the same name. For more
// mathematics, any library will do — a Mat4 is sixteen floats, column after column, as glm and OpenGL keep them.

/** glm::vec2. */
data class Vec2(val x: Float, val y: Float) {
    operator fun plus(o: Vec2) = Vec2(x + o.x, y + o.y)
    operator fun minus(o: Vec2) = Vec2(x - o.x, y - o.y)
    operator fun times(s: Float) = Vec2(x * s, y * s)
    operator fun unaryMinus() = Vec2(-x, -y)
    infix fun dot(o: Vec2) = x * o.x + y * o.y
    val length: Float get() = sqrt(dot(this))
    companion object { val Zero = Vec2(0f, 0f); val One = Vec2(1f, 1f) }
}

/** glm::vec3. */
data class Vec3(val x: Float, val y: Float, val z: Float) {
    operator fun plus(o: Vec3) = Vec3(x + o.x, y + o.y, z + o.z)
    operator fun minus(o: Vec3) = Vec3(x - o.x, y - o.y, z - o.z)
    operator fun times(s: Float) = Vec3(x * s, y * s, z * s)
    operator fun unaryMinus() = Vec3(-x, -y, -z)
    infix fun dot(o: Vec3) = x * o.x + y * o.y + z * o.z
    infix fun cross(o: Vec3) = Vec3(y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x)
    val length: Float get() = sqrt(dot(this))
    fun normalized(): Vec3 = length.let { if (it == 0f) this else this * (1f / it) }
    companion object { val Zero = Vec3(0f, 0f, 0f); val One = Vec3(1f, 1f, 1f); val Up = Vec3(0f, 1f, 0f) }
}

/** glm::vec4. */
data class Vec4(val x: Float, val y: Float, val z: Float, val w: Float) {
    operator fun plus(o: Vec4) = Vec4(x + o.x, y + o.y, z + o.z, w + o.w)
    operator fun minus(o: Vec4) = Vec4(x - o.x, y - o.y, z - o.z, w - o.w)
    operator fun times(s: Float) = Vec4(x * s, y * s, z * s, w * s)
    fun toArray() = floatArrayOf(x, y, z, w)
    companion object { val Zero = Vec4(0f, 0f, 0f, 0f); val One = Vec4(1f, 1f, 1f, 1f) }
}

/** glm::ivec2, ivec3, ivec4. */
data class IVec2(val x: Int, val y: Int)
data class IVec3(val x: Int, val y: Int, val z: Int)
data class IVec4(val x: Int, val y: Int, val z: Int, val w: Int)

/** glm::uvec2, uvec3, uvec4: unsigned in C++, Ints here, with the same bits. */
data class UVec2(val x: Int, val y: Int)
data class UVec3(val x: Int, val y: Int, val z: Int)
data class UVec4(val x: Int, val y: Int, val z: Int, val w: Int)

/**
 * glm::mat4: sixteen floats, column after column — `m[column * 4 + row]`, as glm and OpenGL keep them.
 * Products read as glm's do: `projection * view * model`.
 */
class Mat4(values: FloatArray) {
    init { require(values.size == 16) { "a Mat4 is 16 floats" } }
    private val m = values.copyOf()

    operator fun get(column: Int, row: Int): Float = m[column * 4 + row]
    fun toArray(): FloatArray = m.copyOf()

    operator fun times(o: Mat4): Mat4 {
        val r = FloatArray(16)
        for (c in 0 until 4) for (row in 0 until 4) {
            var sum = 0f
            for (k in 0 until 4) sum += this[k, row] * o[c, k]
            r[c * 4 + row] = sum
        }
        return Mat4(r)
    }

    operator fun times(v: Vec4): Vec4 {
        fun row(r: Int) = this[0, r] * v.x + this[1, r] * v.y + this[2, r] * v.z + this[3, r] * v.w
        return Vec4(row(0), row(1), row(2), row(3))
    }

    override fun equals(other: Any?) = other is Mat4 && m.contentEquals(other.m)
    override fun hashCode() = m.contentHashCode()
    override fun toString() = (0 until 4).joinToString(", ", "Mat4(", ")") { r -> (0 until 4).joinToString(" ") { c -> "${this[c, r]}" } }

    companion object {
        val Identity = Mat4(floatArrayOf(1f, 0f, 0f, 0f, 0f, 1f, 0f, 0f, 0f, 0f, 1f, 0f, 0f, 0f, 0f, 1f))

        fun translation(by: Vec3) = Mat4(floatArrayOf(1f, 0f, 0f, 0f, 0f, 1f, 0f, 0f, 0f, 0f, 1f, 0f, by.x, by.y, by.z, 1f))
        fun scale(by: Vec3) = Mat4(floatArrayOf(by.x, 0f, 0f, 0f, 0f, by.y, 0f, 0f, 0f, 0f, by.z, 0f, 0f, 0f, 0f, 1f))

        /** glm::rotate: [radians] about [axis]. */
        fun rotation(radians: Float, axis: Vec3): Mat4 {
            val a = axis.normalized()
            val c = cos(radians)
            val s = sin(radians)
            val t = 1f - c
            return Mat4(floatArrayOf(
                t * a.x * a.x + c, t * a.x * a.y + s * a.z, t * a.x * a.z - s * a.y, 0f,
                t * a.x * a.y - s * a.z, t * a.y * a.y + c, t * a.y * a.z + s * a.x, 0f,
                t * a.x * a.z + s * a.y, t * a.y * a.z - s * a.x, t * a.z * a.z + c, 0f,
                0f, 0f, 0f, 1f))
        }

        /** glm::perspective with Vulkan's depth range (0 to 1), as Koral builds it. */
        fun perspective(fovYRadians: Float, aspect: Float, near: Float, far: Float): Mat4 {
            val f = 1f / tan(fovYRadians / 2f)
            return Mat4(floatArrayOf(
                f / aspect, 0f, 0f, 0f,
                0f, f, 0f, 0f,
                0f, 0f, far / (near - far), -1f,
                0f, 0f, far * near / (near - far), 0f))
        }

        /** glm::lookAt (right-handed). */
        fun lookAt(eye: Vec3, center: Vec3, up: Vec3 = Vec3.Up): Mat4 {
            val f = (center - eye).normalized()
            val s = (f cross up).normalized()
            val u = s cross f
            return Mat4(floatArrayOf(
                s.x, u.x, -f.x, 0f,
                s.y, u.y, -f.y, 0f,
                s.z, u.z, -f.z, 0f,
                -(s dot eye), -(u dot eye), f dot eye, 1f))
        }
    }
}
