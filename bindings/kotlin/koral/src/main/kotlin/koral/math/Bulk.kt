package koral

import koral.interop.KoralMathNative
import java.lang.foreign.Arena
import java.lang.foreign.MemorySegment
import java.lang.foreign.ValueLayout.JAVA_BYTE
import java.lang.foreign.ValueLayout.JAVA_FLOAT

/**
 * kor::bulk: the same operations over many values at once, vectorised in Koral's native code (SSE2/NEON). Each
 * result equals doing it one value at a time with the top-level functions. The values are copied to native
 * memory and back, so this pays off for large batches; for a handful, the one-at-a-time functions are faster.
 */
object Bulk {
    fun transformPoints(m: Mat4, points: List<Vec3>): List<Vec3> = Arena.ofConfined().use { a ->
        val buffer = a.vec3s(points)
        KoralMathNative.koral_bulk_transform_points(a.mat4(m), buffer, buffer, points.size.toLong())
        buffer.readVec3s(points.size)
    }
    fun transformDirections(m: Mat4, directions: List<Vec3>): List<Vec3> = Arena.ofConfined().use { a ->
        val buffer = a.vec3s(directions)
        KoralMathNative.koral_bulk_transform_directions(a.mat4(m), buffer, buffer, directions.size.toLong())
        buffer.readVec3s(directions.size)
    }
    fun transform(m: Mat4, vectors: List<Vec4>): List<Vec4> = Arena.ofConfined().use { a ->
        val buffer = a.allocateFrom(JAVA_FLOAT, *vectors.flatMap { it.toArray().asList() }.toFloatArray())
        KoralMathNative.koral_bulk_transform(a.mat4(m), buffer, buffer, vectors.size.toLong())
        val f = buffer.toArray(JAVA_FLOAT)
        List(vectors.size) { Vec4(f[it * 4], f[it * 4 + 1], f[it * 4 + 2], f[it * 4 + 3]) }
    }
    /** a[i] * b[i]. */
    fun multiply(a: List<Mat4>, b: List<Mat4>): List<Mat4> = Arena.ofConfined().use { arena ->
        val n = minOf(a.size, b.size)
        val out = arena.mat4s(a.take(n))
        KoralMathNative.koral_bulk_multiply(out, arena.mat4s(b.take(n)), out, n.toLong())
        out.readMat4s(n)
    }
    /** parent * children[i]. */
    fun multiply(parent: Mat4, children: List<Mat4>): List<Mat4> = Arena.ofConfined().use { a ->
        val out = a.mat4s(children)
        KoralMathNative.koral_bulk_multiply_parent(a.mat4(parent), out, out, children.size.toLong())
        out.readMat4s(children.size)
    }
    fun transformAabbs(m: Mat4, boxes: List<Aabb>): List<Aabb> = Arena.ofConfined().use { a ->
        val buffer = a.aabbs(boxes)
        KoralMathNative.koral_bulk_transform_aabbs(a.mat4(m), buffer, buffer, boxes.size.toLong())
        val f = buffer.toArray(JAVA_FLOAT)
        List(boxes.size) { Aabb(Vec3(f[it * 6], f[it * 6 + 1], f[it * 6 + 2]), Vec3(f[it * 6 + 3], f[it * 6 + 4], f[it * 6 + 5])) }
    }
    fun bounds(points: List<Vec3>): Aabb = Arena.ofConfined().use { a ->
        val f = KoralMathNative.koral_bulk_bounds(a, a.vec3s(points), points.size.toLong()).toArray(JAVA_FLOAT)
        Aabb(Vec3(f[0], f[1], f[2]), Vec3(f[3], f[4], f[5]))
    }
    /** Which spheres are (at least partly) in the frustum. */
    @JvmName("cullSpheres")
    fun cull(frustum: Frustum, spheres: List<Sphere>): BooleanArray = Arena.ofConfined().use { a ->
        val buffer = a.allocateFrom(JAVA_FLOAT, *spheres.flatMap { listOf(it.center.x, it.center.y, it.center.z, it.radius) }.toFloatArray())
        val visible = a.allocate(spheres.size.toLong().coerceAtLeast(1))
        KoralMathNative.koral_bulk_cull_spheres(a.frustum(frustum), buffer, visible, spheres.size.toLong())
        BooleanArray(spheres.size) { visible.get(JAVA_BYTE, it.toLong()).toInt() != 0 }
    }
    /** Which boxes are (at least partly) in the frustum. */
    @JvmName("cullAabbs")
    fun cull(frustum: Frustum, boxes: List<Aabb>): BooleanArray = Arena.ofConfined().use { a ->
        val visible = a.allocate(boxes.size.toLong().coerceAtLeast(1))
        KoralMathNative.koral_bulk_cull_aabbs(a.frustum(frustum), a.aabbs(boxes), visible, boxes.size.toLong())
        BooleanArray(boxes.size) { visible.get(JAVA_BYTE, it.toLong()).toInt() != 0 }
    }
    fun normalize(vectors: List<Vec3>): List<Vec3> = Arena.ofConfined().use { a ->
        val buffer = a.vec3s(vectors)
        KoralMathNative.koral_bulk_normalize(buffer, vectors.size.toLong())
        buffer.readVec3s(vectors.size)
    }
}

// Native copies of the math values, in koral_math_c.h's layouts (the C++ types' bytes).
internal fun Arena.mat4(m: Mat4): MemorySegment = allocateFrom(JAVA_FLOAT, *m.toArray())
internal fun Arena.vec3(v: Vec3): MemorySegment = allocateFrom(JAVA_FLOAT, v.x, v.y, v.z)
internal fun Arena.vec3s(points: List<Vec3>): MemorySegment =
    allocateFrom(JAVA_FLOAT, *FloatArray(maxOf(1, points.size) * 3).also { f -> points.forEachIndexed { i, p -> f[i * 3] = p.x; f[i * 3 + 1] = p.y; f[i * 3 + 2] = p.z } })
internal fun Arena.mat4s(ms: List<Mat4>): MemorySegment =
    allocateFrom(JAVA_FLOAT, *FloatArray(maxOf(1, ms.size) * 16).also { f -> ms.forEachIndexed { i, m -> m.toArray().copyInto(f, i * 16) } })
internal fun Arena.aabbs(boxes: List<Aabb>): MemorySegment =
    allocateFrom(JAVA_FLOAT, *FloatArray(maxOf(1, boxes.size) * 6).also { f ->
        boxes.forEachIndexed { i, b -> b.min.toArray().copyInto(f, i * 6); b.max.toArray().copyInto(f, i * 6 + 3) }
    })
internal fun Arena.frustum(f: Frustum): MemorySegment =
    allocateFrom(JAVA_FLOAT, *f.planes.flatMap { listOf(it.normal.x, it.normal.y, it.normal.z, it.distance) }.toFloatArray())
internal fun MemorySegment.readVec3s(n: Int): List<Vec3> {
    val f = asSlice(0, n * 12L).toArray(JAVA_FLOAT)
    return List(n) { Vec3(f[it * 3], f[it * 3 + 1], f[it * 3 + 2]) }
}
internal fun MemorySegment.readMat4s(n: Int): List<Mat4> {
    val f = asSlice(0, n * 64L).toArray(JAVA_FLOAT)
    return List(n) { Mat4(f.copyOfRange(it * 16, it * 16 + 16)) }
}
