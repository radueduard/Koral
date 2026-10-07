package koral

// kor::Vec2/3/4 and their integer forms (kmath/vector.h), as immutable values: the same names, layouts and
// meanings. Arithmetic is component-wise; the functions over vectors (dot, cross, normalize, lerp, ...) are
// top-level, as they are free functions in C++ — see KMath.kt. Koral's world is right-handed, +Y up, and a
// camera looks down -Z.

/** kor::Vec2. */
data class Vec2(val x: Float, val y: Float) {
    constructor(s: Float) : this(s, s)

    operator fun get(i: Int): Float = when (i) { 0 -> x; 1 -> y; else -> throw IndexOutOfBoundsException(i) }
    operator fun plus(o: Vec2) = Vec2(x + o.x, y + o.y)
    operator fun minus(o: Vec2) = Vec2(x - o.x, y - o.y)
    operator fun times(o: Vec2) = Vec2(x * o.x, y * o.y)
    operator fun div(o: Vec2) = Vec2(x / o.x, y / o.y)
    operator fun times(s: Float) = Vec2(x * s, y * s)
    operator fun div(s: Float) = Vec2(x / s, y / s)
    operator fun plus(s: Float) = Vec2(x + s, y + s)
    operator fun minus(s: Float) = Vec2(x - s, y - s)
    operator fun unaryMinus() = Vec2(-x, -y)
    fun toArray() = floatArrayOf(x, y)
    override fun toString() = "($x, $y)"

    companion object {
        val Zero = Vec2(0f, 0f)
        val One = Vec2(1f, 1f)
        val UnitX = Vec2(1f, 0f)
        val UnitY = Vec2(0f, 1f)
    }
}

/** kor::Vec3. */
data class Vec3(val x: Float, val y: Float, val z: Float) {
    constructor(s: Float) : this(s, s, s)
    constructor(xy: Vec2, z: Float) : this(xy.x, xy.y, z)
    /** The first three of a Vec4 (C++'s explicit conversion). */
    constructor(v: Vec4) : this(v.x, v.y, v.z)

    val xy: Vec2 get() = Vec2(x, y)

    operator fun get(i: Int): Float = when (i) { 0 -> x; 1 -> y; 2 -> z; else -> throw IndexOutOfBoundsException(i) }
    operator fun plus(o: Vec3) = Vec3(x + o.x, y + o.y, z + o.z)
    operator fun minus(o: Vec3) = Vec3(x - o.x, y - o.y, z - o.z)
    operator fun times(o: Vec3) = Vec3(x * o.x, y * o.y, z * o.z)
    operator fun div(o: Vec3) = Vec3(x / o.x, y / o.y, z / o.z)
    operator fun times(s: Float) = Vec3(x * s, y * s, z * s)
    operator fun div(s: Float) = Vec3(x / s, y / s, z / s)
    operator fun plus(s: Float) = Vec3(x + s, y + s, z + s)
    operator fun minus(s: Float) = Vec3(x - s, y - s, z - s)
    operator fun unaryMinus() = Vec3(-x, -y, -z)
    /** The same component replaced: `v.with(1, 0f)`. */
    fun with(i: Int, value: Float) = when (i) { 0 -> copy(x = value); 1 -> copy(y = value); 2 -> copy(z = value); else -> throw IndexOutOfBoundsException(i) }
    fun toArray() = floatArrayOf(x, y, z)
    override fun toString() = "($x, $y, $z)"

    // The Kotlin bindings' earlier names, kept.
    infix fun dot(o: Vec3) = koral.dot(this, o)
    infix fun cross(o: Vec3) = koral.cross(this, o)
    val length: Float get() = koral.length(this)
    fun normalized(): Vec3 = normalize(this)

    companion object {
        val Zero = Vec3(0f, 0f, 0f)
        val One = Vec3(1f, 1f, 1f)
        val UnitX = Vec3(1f, 0f, 0f)
        val UnitY = Vec3(0f, 1f, 0f)
        val UnitZ = Vec3(0f, 0f, 1f)
        val Up = Vec3(0f, 1f, 0f)
        val Down = Vec3(0f, -1f, 0f)
        val Right = Vec3(1f, 0f, 0f)
        val Left = Vec3(-1f, 0f, 0f)
        val Forward = Vec3(0f, 0f, -1f)
        val Back = Vec3(0f, 0f, 1f)
    }
}

/** kor::Vec4. */
data class Vec4(val x: Float, val y: Float, val z: Float, val w: Float) {
    constructor(s: Float) : this(s, s, s, s)
    constructor(xyz: Vec3, w: Float) : this(xyz.x, xyz.y, xyz.z, w)
    constructor(xy: Vec2, z: Float, w: Float) : this(xy.x, xy.y, z, w)
    constructor(xy: Vec2, zw: Vec2) : this(xy.x, xy.y, zw.x, zw.y)

    val xy: Vec2 get() = Vec2(x, y)
    val xyz: Vec3 get() = Vec3(x, y, z)

    operator fun get(i: Int): Float = when (i) { 0 -> x; 1 -> y; 2 -> z; 3 -> w; else -> throw IndexOutOfBoundsException(i) }
    operator fun plus(o: Vec4) = Vec4(x + o.x, y + o.y, z + o.z, w + o.w)
    operator fun minus(o: Vec4) = Vec4(x - o.x, y - o.y, z - o.z, w - o.w)
    operator fun times(o: Vec4) = Vec4(x * o.x, y * o.y, z * o.z, w * o.w)
    operator fun div(o: Vec4) = Vec4(x / o.x, y / o.y, z / o.z, w / o.w)
    operator fun times(s: Float) = Vec4(x * s, y * s, z * s, w * s)
    operator fun div(s: Float) = Vec4(x / s, y / s, z / s, w / s)
    operator fun unaryMinus() = Vec4(-x, -y, -z, -w)
    fun toArray() = floatArrayOf(x, y, z, w)
    override fun toString() = "($x, $y, $z, $w)"

    companion object {
        val Zero = Vec4(0f, 0f, 0f, 0f)
        val One = Vec4(1f, 1f, 1f, 1f)
        val UnitX = Vec4(1f, 0f, 0f, 0f)
        val UnitY = Vec4(0f, 1f, 0f, 0f)
        val UnitZ = Vec4(0f, 0f, 1f, 0f)
        val UnitW = Vec4(0f, 0f, 0f, 1f)
    }
}

operator fun Float.times(v: Vec2) = v * this
operator fun Float.times(v: Vec3) = v * this
operator fun Float.times(v: Vec4) = v * this

/** kor::IVec2, IVec3, IVec4. */
data class IVec2(val x: Int, val y: Int) {
    operator fun plus(o: IVec2) = IVec2(x + o.x, y + o.y)
    operator fun minus(o: IVec2) = IVec2(x - o.x, y - o.y)
    operator fun times(s: Int) = IVec2(x * s, y * s)
    fun toVec2() = Vec2(x.toFloat(), y.toFloat())
}
data class IVec3(val x: Int, val y: Int, val z: Int) {
    operator fun plus(o: IVec3) = IVec3(x + o.x, y + o.y, z + o.z)
    operator fun minus(o: IVec3) = IVec3(x - o.x, y - o.y, z - o.z)
    operator fun times(s: Int) = IVec3(x * s, y * s, z * s)
    fun toVec3() = Vec3(x.toFloat(), y.toFloat(), z.toFloat())
}
data class IVec4(val x: Int, val y: Int, val z: Int, val w: Int)

/** kor::UVec2, UVec3, UVec4: unsigned in C++, Ints here, with the same bits. */
data class UVec2(val x: Int, val y: Int) {
    operator fun plus(o: UVec2) = UVec2(x + o.x, y + o.y)
    operator fun minus(o: UVec2) = UVec2(x - o.x, y - o.y)
    fun toVec2() = Vec2(Integer.toUnsignedLong(x).toFloat(), Integer.toUnsignedLong(y).toFloat())
}
data class UVec3(val x: Int, val y: Int, val z: Int) {
    operator fun plus(o: UVec3) = UVec3(x + o.x, y + o.y, z + o.z)
    operator fun minus(o: UVec3) = UVec3(x - o.x, y - o.y, z - o.z)
}
data class UVec4(val x: Int, val y: Int, val z: Int, val w: Int)
