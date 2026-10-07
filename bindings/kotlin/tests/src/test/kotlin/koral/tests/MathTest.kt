package koral.tests

import java.lang.foreign.Arena
import java.lang.foreign.MemorySegment
import java.lang.foreign.ValueLayout.JAVA_FLOAT
import java.lang.foreign.ValueLayout.JAVA_INT
import koral.*
import koral.interop.KoralMathLayouts
import koral.interop.KoralMathNative
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

// kmath in Kotlin against kmath itself, through koral_math_c.h. What is arithmetic, floor and sqrt only (PCG,
// noise, matrices, geometry) must agree bit for bit; what goes through kotlin.math's sin and friends — which
// round through double — to a few ULPs. No application: the math needs none.
class MathTest {
    private val a = Arena.ofAuto()

    private fun same(x: Float, y: Float) = x.toRawBits() == y.toRawBits() || (x.isNaN() && y.isNaN())
    private fun same(x: Vec3, y: Vec3) = same(x.x, y.x) && same(x.y, y.y) && same(x.z, y.z)
    private fun same(x: Mat4, y: Mat4) = x.toArray().zip(y.toArray()).all { (p, q) -> same(p, q) }

    private fun v3(v: Vec3): MemorySegment = a.allocateFrom(JAVA_FLOAT, v.x, v.y, v.z)
    private fun q(v: Quat): MemorySegment = a.allocateFrom(JAVA_FLOAT, v.x, v.y, v.z, v.w)
    private fun m4(m: Mat4): MemorySegment = a.allocateFrom(JAVA_FLOAT, *m.toArray())
    private fun vec3(s: MemorySegment) = Vec3(s.get(JAVA_FLOAT, 0), s.get(JAVA_FLOAT, 4), s.get(JAVA_FLOAT, 8))
    private fun quat(s: MemorySegment) = Quat(s.get(JAVA_FLOAT, 0), s.get(JAVA_FLOAT, 4), s.get(JAVA_FLOAT, 8), s.get(JAVA_FLOAT, 12))
    private fun mat4(s: MemorySegment) = Mat4(s.asSlice(0, 64).toArray(JAVA_FLOAT))

    @Test
    fun randomMatchesNative() {
        val reference = Random(42uL, 54uL)
        assertEquals(0xa15c02b7u, reference.nextU32())
        assertEquals(0x7b47f409u, reference.nextU32())

        val managed = Random(1234uL, 99uL)
        val native = KoralMathNative.koral_random_new(a, 1234L, 99L)
        repeat(2000) { i ->
            assertEquals(KoralMathNative.koral_random_next_u32(native).toUInt(), managed.nextU32(), "nextU32")
            assertEquals(KoralMathNative.koral_random_next_u32_below(native, 7 + i).toUInt(), managed.nextU32((7 + i).toUInt()), "nextU32(bound)")
            assertEquals(KoralMathNative.koral_random_next_int(native, -50, 50), managed.nextInt(-50, 50), "nextInt")
            assertTrue(same(KoralMathNative.koral_random_next_float(native), managed.nextFloat()), "nextFloat")
            assertEquals(KoralMathNative.koral_random_next_double(native), managed.nextDouble(), "nextDouble")
            assertTrue(same(vec3(KoralMathNative.koral_random_inside_unit_sphere(a, native)), managed.insideUnitSphere()), "insideUnitSphere")
            assertTrue(approxEqual(vec3(KoralMathNative.koral_random_on_unit_sphere(a, native)), managed.onUnitSphere(), 1e-6f), "onUnitSphere")
            assertTrue(approxEqual(KoralMathNative.koral_random_next_gaussian(native, 1f, 2f), managed.nextGaussian(1f, 2f), 1e-5f), "nextGaussian")
        }
        val jumped = Random(5uL)
        val nativeJumped = KoralMathNative.koral_random_new(a, 5L, Random.DefaultStream.toLong())
        jumped.advance(123456uL)
        KoralMathNative.koral_random_advance(nativeJumped, 123456L)
        assertEquals(KoralMathNative.koral_random_next_u32(nativeJumped).toUInt(), jumped.nextU32(), "advance")

        val mine = IntArray(10) { it }
        Random(77uL).shuffle(mine)
        val theirs = a.allocateFrom(JAVA_INT, *IntArray(10) { it })
        KoralMathNative.koral_random_shuffle(KoralMathNative.koral_random_new(a, 77L, Random.DefaultStream.toLong()), theirs, 10L, 4L)
        assertEquals(theirs.toArray(JAVA_INT).toList(), mine.toList(), "shuffle")
        for (v in 0 until 1000) assertEquals(KoralMathNative.koral_hash(v).toUInt(), hash(v.toUInt()), "hash")
    }

    @Test
    fun noiseMatchesNative() {
        for (seed in listOf(0u, 1u, 1234u, 0xdeadbeefu)) {
            val managed = Noise(seed)
            val native = a.allocate(KoralMathLayouts.KoralNoise)
            KoralMathNative.koral_noise_init(native, seed.toInt())
            val points = Random(seed.toULong() + 1uL)
            var mismatches = 0
            repeat(3000) {
                val p = points.insideBox(Vec3(-300f), Vec3(300f))
                if (!same(KoralMathNative.koral_noise_perlin2(native, p.x, p.y), managed.perlin(p.x, p.y))) mismatches++
                if (!same(KoralMathNative.koral_noise_perlin3(native, p.x, p.y, p.z), managed.perlin(p))) mismatches++
                if (!same(KoralMathNative.koral_noise_simplex2(native, p.x, p.y), managed.simplex(p.x, p.y))) mismatches++
                if (!same(KoralMathNative.koral_noise_simplex3(native, p.x, p.y, p.z), managed.simplex(p))) mismatches++
                if (!same(KoralMathNative.koral_noise_value2(native, p.x, p.y), managed.value(p.x, p.y))) mismatches++
                if (!same(KoralMathNative.koral_noise_value3(native, p.x, p.y, p.z), managed.value(p))) mismatches++
                if (!same(KoralMathNative.koral_noise_cellular2(native, p.x, p.y), managed.cellular(p.x, p.y))) mismatches++
                if (!same(KoralMathNative.koral_noise_cellular3(native, p.x, p.y, p.z), managed.cellular(p))) mismatches++
            }
            assertEquals(0, mismatches, "noise of seed $seed, bit for bit")
            val options = a.allocateFrom(JAVA_INT, 1, 6, 0, 0)
            options.set(JAVA_FLOAT, 8, 2.1f)
            options.set(JAVA_FLOAT, 12, 0.45f)
            assertTrue(same(KoralMathNative.koral_noise_fractal3(native, 1, v3(Vec3(1.5f, 2.5f, -3.5f)), options),
                            managed.fractal(Noise.Kind.Simplex, Vec3(1.5f, 2.5f, -3.5f), FractalOptions(FractalType.Ridged, 6, 2.1f, 0.45f))), "fractal")
        }
    }

    @Test
    fun matricesMatchNative() {
        val random = Random(3uL)
        repeat(500) {
            val t = random.insideBox(Vec3(-5f), Vec3(5f))
            val s = random.insideBox(Vec3(0.2f), Vec3(3f))
            val r = random.rotation()
            val m = compose(t, r, s)
            assertTrue(same(m, mat4(KoralMathNative.koral_compose(a, v3(t), q(r), v3(s)))), "compose")
            assertTrue(same(inverse(m), mat4(KoralMathNative.koral_mat4_inverse(a, m4(m)))), "inverse")
            assertTrue(same(m * inverse(m), mat4(KoralMathNative.koral_mat4_mul(a, m4(m), m4(inverse(m))))), "Mat4 * Mat4")
            assertTrue(same(determinant(m), KoralMathNative.koral_mat4_determinant(m4(m))), "determinant")
            assertTrue(same(transformPoint(m, t), vec3(KoralMathNative.koral_transform_point(a, m4(m), v3(t)))), "transformPoint")
            assertTrue(same(r * t, vec3(KoralMathNative.koral_quat_rotate(a, q(r), v3(t)))), "Quat * Vec3")
            val eye = random.insideBox(Vec3(-9f), Vec3(9f))
            assertTrue(same(lookAt(eye, t), mat4(KoralMathNative.koral_look_at(a, v3(eye), v3(t), v3(Vec3.Up)))), "lookAt")
            assertTrue(approxEqual(eulerAngles(r), vec3(KoralMathNative.koral_quat_euler_angles(a, q(r))), 1e-5f), "eulerAngles")
            assertTrue(sameRotation(slerp(r, Quat.Identity, 0.3f), quat(KoralMathNative.koral_quat_slerp(a, q(r), q(Quat.Identity), 0.3f)), 1e-5f), "slerp")
        }
        assertTrue(approxEqual(perspective(1.1f, 1.7f, 0.1f, 300f), mat4(KoralMathNative.koral_perspective(a, 1.1f, 1.7f, 0.1f, 300f)), 1e-6f), "perspective")
        assertTrue(same(orthographic(-3f, 4f, -1f, 2f, 0.5f, 20f), mat4(KoralMathNative.koral_orthographic(a, -3f, 4f, -1f, 2f, 0.5f, 20f))), "orthographic")
        assertTrue(Mat4() == Mat4.Identity && Quat() == Quat.Identity && !Aabb().valid, "C++'s defaults")
    }

    @Test
    fun geometryAndBulkMatchNative() {
        val random = Random(11uL)
        val viewProjection = perspective(radians(70f), 1.3f, 0.5f, 40f) * lookAt(Vec3(0f, 2f, 10f), Vec3.Zero)
        val frustum = Frustum.fromMatrix(viewProjection)
        val nativeFrustum = KoralMathNative.koral_frustum_from_matrix(a, m4(viewProjection)).toArray(JAVA_FLOAT)
        frustum.planes.forEachIndexed { i, p ->
            assertTrue(same(p.normal, Vec3(nativeFrustum[i * 4], nativeFrustum[i * 4 + 1], nativeFrustum[i * 4 + 2])) && same(p.distance, nativeFrustum[i * 4 + 3]), "frustum planes")
        }
        val nf = a.allocateFrom(JAVA_FLOAT, *nativeFrustum)
        val distance = a.allocate(JAVA_FLOAT)
        repeat(1000) {
            val c = random.insideBox(Vec3(-20f), Vec3(20f))
            val box = Aabb.fromCenterExtents(c, Vec3(random.nextFloat(0.1f, 3f)))
            val ray = Ray(random.insideBox(Vec3(-20f), Vec3(20f)), random.onUnitSphere())
            val nray = a.allocateFrom(JAVA_FLOAT, ray.origin.x, ray.origin.y, ray.origin.z, ray.direction.x, ray.direction.y, ray.direction.z)
            val nbox = a.allocateFrom(JAVA_FLOAT, *box.min.toArray(), *box.max.toArray())
            val hit = KoralMathNative.koral_raycast_aabb(nray, nbox, Float.POSITIVE_INFINITY, distance)
            val mine = raycast(ray, box)
            assertEquals(hit, mine != null, "raycast(Aabb) hit")
            if (hit) assertTrue(same(distance.get(JAVA_FLOAT, 0), mine!!), "raycast(Aabb) distance")
            assertEquals(KoralMathNative.koral_frustum_classify_aabb(nf, nbox), frustum.classify(box).ordinal, "classify(Aabb)")
            val m = compose(c, random.rotation(), Vec3(1f, 2f, 3f))
            val moved = KoralMathNative.koral_aabb_transformed(a, nbox, m4(m)).toArray(JAVA_FLOAT)
            assertEquals(Aabb(Vec3(moved[0], moved[1], moved[2]), Vec3(moved[3], moved[4], moved[5])), box.transformed(m), "Aabb.transformed")
        }

        val points = List(257) { random.insideBox(Vec3(-10f), Vec3(10f)) }
        val m2 = compose(Vec3(1f, 2f, 3f), random.rotation(), Vec3(0.5f, 2f, 1f))
        assertTrue(Bulk.transformPoints(m2, points).zip(points).all { (p, q) -> same(p, transformPoint(m2, q)) }, "Bulk.transformPoints")
        val boxes = points.map { Aabb.fromCenterExtents(it, Vec3(0.5f)) }
        val visible = Bulk.cull(frustum, boxes)
        assertTrue(boxes.indices.all { visible[it] == overlaps(frustum, boxes[it]) }, "Bulk.cull")
        assertEquals(Aabb.fromPoints(points), Bulk.bounds(points), "Bulk.bounds")
    }

    @Test
    fun colorAndEasingMatchNative() {
        for (e in Easing.entries) {
            var t = 0f
            while (t <= 1f) {
                assertTrue(approxEqual(ease(e, t), KoralMathNative.koral_ease(e.ordinal, t), 2e-6f), "ease($e, $t)")
                t += 1f / 64f
            }
        }
        val random = Random(9uL)
        repeat(500) {
            val c = Vec3(random.nextFloat(), random.nextFloat(), random.nextFloat())
            assertTrue(approxEqual(rgbToHsv(c), vec3(KoralMathNative.koral_rgb_to_hsv(a, v3(c))), 1e-6f), "rgbToHsv")
            assertTrue(approxEqual(linearToOklab(c), vec3(KoralMathNative.koral_linear_to_oklab(a, v3(c))), 1e-5f), "linearToOklab")
            val c4 = a.allocateFrom(JAVA_FLOAT, c.x, c.y, c.z, 1f)
            assertEquals(KoralMathNative.koral_pack_unorm4x8(c4), packUnorm4x8(Vec4(c, 1f)), "packUnorm4x8")
            val f = random.nextFloat(-70000f, 70000f) * random.nextFloat() * random.nextFloat()
            assertEquals(KoralMathNative.koral_float_to_half(f), floatToHalf(f), "floatToHalf($f)")
            val h = random.nextU32(65536u).toInt().toShort()
            assertTrue(same(KoralMathNative.koral_half_to_float(h), halfToFloat(h)), "halfToFloat($h)")
            val n = random.onUnitSphere()
            assertEquals(KoralMathNative.koral_pack_octahedral(v3(n)), packOctahedral(n), "packOctahedral")
        }
        val path = listOf(Vec3(0f, 0f, 0f), Vec3(1f, 0f, 0f), Vec3(1f, 1f, 0f), Vec3(0f, 1f, 1f))
        val np = a.allocateFrom(JAVA_FLOAT, *path.flatMap { it.toArray().asList() }.toFloatArray())
        var t = 0f
        while (t <= 4f) {
            assertTrue(same(samplePath(path, t, true), vec3(KoralMathNative.koral_sample_path(a, np, 4L, t, true))), "samplePath($t)")
            t += 0.25f
        }
    }
}
