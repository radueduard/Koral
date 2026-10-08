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
    private fun vec4(s: MemorySegment) = Vec4(s.get(JAVA_FLOAT, 0), s.get(JAVA_FLOAT, 4), s.get(JAVA_FLOAT, 8), s.get(JAVA_FLOAT, 12))
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
        assertTrue(approxEqual(perspective(1.1f, 1.7f, 0.1f, 300f), mat4(KoralMathNative.koral_perspective(a, 1.1f, 1.7f, 0.1f, 300f, 0)), 1e-6f), "perspective")
        assertTrue(same(orthographic(-3f, 4f, -1f, 2f, 0.5f, 20f), mat4(KoralMathNative.koral_orthographic(a, -3f, 4f, -1f, 2f, 0.5f, 20f, 0))), "orthographic")
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
            assertEquals(KoralMathNative.koral_pack_unorm4x8(c4).toUInt(), packUnorm4x8(Vec4(c, 1f)), "packUnorm4x8")
            val f = random.nextFloat(-70000f, 70000f) * random.nextFloat() * random.nextFloat()
            assertEquals(KoralMathNative.koral_float_to_half(f).toUShort(), floatToHalf(f), "floatToHalf($f)")
            val h = random.nextU32(65536u).toInt().toShort()
            assertTrue(same(KoralMathNative.koral_half_to_float(h), halfToFloat(h.toUShort())), "halfToFloat($h)")
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

    @Test
    fun camerasAndEulerMatchNative() {
        for (clip in ClipSpace.entries) {
            val c = clip.ordinal
            assertTrue(same(perspective(1.1f, 1.7f, 0.1f, 300f, clip), mat4(KoralMathNative.koral_perspective(a, 1.1f, 1.7f, 0.1f, 300f, c))), "perspective $clip")
            assertTrue(same(perspectiveFov(1.1f, 640f, 480f, 0.1f, 300f, clip), mat4(KoralMathNative.koral_perspective_fov(a, 1.1f, 640f, 480f, 0.1f, 300f, c))), "perspectiveFov $clip")
            assertTrue(same(infinitePerspective(1.1f, 1.7f, 0.1f, clip), mat4(KoralMathNative.koral_infinite_perspective(a, 1.1f, 1.7f, 0.1f, c))), "infinitePerspective $clip")
            assertTrue(same(frustumProjection(-1f, 2f, -1.5f, 1f, 0.5f, 90f, clip), mat4(KoralMathNative.koral_frustum_projection(a, -1f, 2f, -1.5f, 1f, 0.5f, 90f, c))), "frustumProjection $clip")
            assertTrue(same(orthographic(-3f, 4f, -1f, 2f, 0.5f, 20f, clip), mat4(KoralMathNative.koral_orthographic(a, -3f, 4f, -1f, 2f, 0.5f, 20f, c))), "orthographic $clip")
            val model = compose(Vec3(1f, -2f, 3f), Quat.angleAxis(0.7f, Vec3(1f, 2f, 3f)), Vec3(2f, 0.5f, 1.5f))
            val proj = perspective(1f, 1.3f, 0.1f, 50f, clip) * lookAt(Vec3(0f, 0f, 10f), Vec3.Zero)
            val viewport = Vec4(10f, 20f, 800f, 600f)
            val vp = a.allocateFrom(JAVA_FLOAT, viewport.x, viewport.y, viewport.z, viewport.w)
            val w = project(Vec3(0.3f, -0.2f, 0.5f), model, proj, viewport, clip)
            assertTrue(same(w, vec3(KoralMathNative.koral_project(a, v3(Vec3(0.3f, -0.2f, 0.5f)), m4(model), m4(proj), vp, c))), "project $clip")
            assertTrue(same(unProject(w, model, proj, viewport, clip), vec3(KoralMathNative.koral_unproject(a, v3(w), m4(model), m4(proj), vp, c))), "unProject $clip")
        }
        val random = Random(31uL)
        repeat(200) { i ->
            val angles = Vec3(random.nextFloat(-3f, 3f), random.nextFloat(-1.5f, 1.5f), random.nextFloat(-3f, 3f))
            val order = EulerOrder.entries[i % 12]
            val m = eulerAngles(order, angles)
            assertTrue(approxEqual(m, mat4(KoralMathNative.koral_euler_angles(a, order.ordinal, v3(angles))), 1e-6f), "eulerAngles $order")
            assertTrue(approxEqual(extractEulerAngles(order, m), vec3(KoralMathNative.koral_extract_euler_angles(a, order.ordinal, m4(m))), 1e-4f), "extract $order")
            assertTrue(approxEqual(eulerAngles(order, extractEulerAngles(order, m)), m, 1e-4f), "round trip $order")
            val r = random.rotation()
            assertTrue(same(pitch(r), KoralMathNative.koral_quat_pitch(q(r))) && same(yaw(r), KoralMathNative.koral_quat_yaw(q(r))) &&
                       same(roll(r), KoralMathNative.koral_quat_roll(q(r))), "pitch/yaw/roll")
        }
        assertTrue(approxEqual(eulerAngleXYZ(0.1f, 0.2f, 0.3f), mat4(KoralMathNative.koral_euler_angle_xyz(a, 0.1f, 0.2f, 0.3f)), 1e-6f), "eulerAngleXYZ")
        assertTrue(approxEqual(yawPitchRoll(0.1f, 0.2f, 0.3f), mat4(KoralMathNative.koral_yaw_pitch_roll(a, 0.1f, 0.2f, 0.3f)), 1e-6f), "yawPitchRoll")

        // The glm-shaped surface reads as in C++.
        val v = Vec4(1f, 2f, 3f, 4f)
        assertTrue(v.zyx == Vec3(3f, 2f, 1f) && v.xxyy == Vec4(1f, 1f, 2f, 2f), "swizzles")
        assertTrue(all(lessThan(IVec3(1, 2, 3), IVec3(2, 3, 4))) && any(equal(UVec2(1, 2), UVec2(0, 2))), "relational")
        assertTrue(transpose(Mat2x3(Vec3(1f, 2f, 3f), Vec3(4f, 5f, 6f))) == Mat3x2(Vec2(1f, 4f), Vec2(2f, 5f), Vec2(3f, 6f)), "Mat2x3")
        assertTrue(DMat4(2.0) * DVec4(1.0) == DVec4(2.0), "DMat4")
        assertTrue(floor(Vec3(-0.5f, 1.5f, 2f)) == Vec3(-1f, 1f, 2f) && mix(Vec2(0f), Vec2(10f), 0.5f) == Vec2(5f), "GLSL functions on vectors")
    }

    @Test
    fun materialColors() {
        assertTrue(MaterialColors.Red500 == colorFromHex(0xF44336) && MaterialColors.TealA400 == colorFromHex(0x1DE9B6), "palette by name")
        for (hue in MaterialHue.entries) for (shade in listOf(50, 100, 250, 500, 900, 1000)) {
            assertEquals(vec4(KoralMathNative.koral_material_color(a, hue.ordinal, shade)), materialColor(hue, shade), "materialColor $hue $shade")
            assertEquals(vec4(KoralMathNative.koral_material_accent(a, hue.ordinal, shade)), materialAccent(hue, shade), "materialAccent $hue $shade")
        }
        val blue = TonalPalette.fromColor(colorFromHex(0x0000FF))
        assertEquals(colorFromHex(0xE0E0FF), blue.tone(90f))
        assertEquals(colorFromHex(0x343DFF), blue.tone(40f))
        val hct = Hct.fromColor(colorFromHex(0x0000FF))
        assertTrue(kotlin.math.abs(hct.hue - 282.788f) < 0.01f && hct.toColor() == colorFromHex(0x0000FF), "hct $hct")
        val dark = MaterialScheme.fromSeed(colorFromHex(0x0000FF), dark = true, variant = SchemeVariant.Monochrome)
        assertTrue(kotlin.math.abs(Hct.fromColor(dark.primary).tone - 100f) < 1f, "monochrome dark primary")
        val scheme = MaterialScheme.fromSeed(colorFromHex(0x6750A4), dark = false)
        assertTrue(contrastRatio(scheme.onPrimary, scheme.primary) >= 4.5f, "onPrimary reads")
        val pixels = ByteArray(100 * 4) { if (it % 4 == 3) -1 else 128.toByte() } + ByteArray(20 * 4) { if (it % 4 == 1 || it % 4 == 2) 0 else -1 }
        assertEquals(colorFromHex(0xFF0000), seedColors(pixels)[0], "seedColors")
    }
}
