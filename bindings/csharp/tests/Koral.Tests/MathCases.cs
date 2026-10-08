using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Koral.Tests.Native;
using static Koral.KMath;

// Value types cross this assembly's own P/Invokes as they are, as they do the library's.
[assembly: DisableRuntimeMarshalling]

namespace Koral.Tests;

// kmath in C# against kmath itself, through koral_math_c.h. What is arithmetic and Floor only (PCG, noise,
// matrices, geometry) must agree bit for bit; what goes through MathF.Sin and friends, to a few ULPs.
public static unsafe partial class Cases
{
    private static bool Same(float a, float b) => BitConverter.SingleToInt32Bits(a) == BitConverter.SingleToInt32Bits(b) || (float.IsNaN(a) && float.IsNaN(b));
    private static bool Same(Vec3 a, Vec3 b) => Same(a.X, b.X) && Same(a.Y, b.Y) && Same(a.Z, b.Z);
    private static bool Same(Mat4 a, Mat4 b)
    {
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                if (!Same(a[c, r], b[c, r])) return false;
        return true;
    }

    public static void MathRandomMatchesNative()
    {
        // PCG32's published reference output.
        var reference = new Random(42, 54);
        Check.Equal(0xa15c02b7u, reference.NextU32(), "pcg32 reference 0");
        Check.Equal(0x7b47f409u, reference.NextU32(), "pcg32 reference 1");

        var managed = new Random(1234, 99);
        var native = KoralMathNative.koral_random_new(1234, 99);
        for (int i = 0; i < 2000; ++i)
        {
            Check.Equal(KoralMathNative.koral_random_next_u32(&native), managed.NextU32(), "NextU32");
            Check.Equal(KoralMathNative.koral_random_next_u32_below(&native, 7u + (uint)i), managed.NextU32(7u + (uint)i), "NextU32(bound)");
            Check.Equal(KoralMathNative.koral_random_next_int(&native, -50, 50), managed.NextInt(-50, 50), "NextInt");
            Check.That(Same(KoralMathNative.koral_random_next_float(&native), managed.NextFloat()), "NextFloat");
            Check.Equal(KoralMathNative.koral_random_next_double(&native), managed.NextDouble(), "NextDouble");
            Check.That(Same(KoralMathNative.koral_random_inside_unit_sphere(&native), managed.InsideUnitSphere()), "InsideUnitSphere");
            Check.That(ApproxEqual(KoralMathNative.koral_random_on_unit_sphere(&native), managed.OnUnitSphere(), 1e-6f), "OnUnitSphere");
            Check.That(ApproxEqual(KoralMathNative.koral_random_next_gaussian(&native, 1f, 2f), managed.NextGaussian(1f, 2f), 1e-5f), "NextGaussian");
        }
        var jumped = new Random(5);
        var nativeJumped = KoralMathNative.koral_random_new(5, Random.DefaultStream);
        jumped.Advance(123456);
        KoralMathNative.koral_random_advance(&nativeJumped, 123456);
        Check.Equal(KoralMathNative.koral_random_next_u32(&nativeJumped), jumped.NextU32(), "Advance");

        int[] a = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9], b = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9];
        new Random(77).Shuffle<int>(a);
        var shuffler = KoralMathNative.koral_random_new(77, Random.DefaultStream);
        fixed (int* p = b) KoralMathNative.koral_random_shuffle(&shuffler, p, 10, sizeof(int));
        Check.That(a.SequenceEqual(b), $"Shuffle: {string.Join(",", a)} vs {string.Join(",", b)}");
        for (uint v = 0; v < 1000; ++v) Check.Equal(KoralMathNative.koral_hash(v), Hash(v), "Hash");
    }

    public static void MathNoiseMatchesNative()
    {
        foreach (uint seed in new uint[] { 0, 1, 1234, 0xdeadbeef })
        {
            var managed = new Noise(seed);
            KoralNoise native;
            KoralMathNative.koral_noise_init(&native, seed);
            var points = new Random(seed + 1);
            int mismatches = 0;
            for (int i = 0; i < 3000; ++i)
            {
                Vec3 p = points.InsideBox(new Vec3(-300f), new Vec3(300f));
                if (!Same(KoralMathNative.koral_noise_perlin2(&native, p.X, p.Y), managed.Perlin(p.X, p.Y))) ++mismatches;
                if (!Same(KoralMathNative.koral_noise_perlin3(&native, p.X, p.Y, p.Z), managed.Perlin(p))) ++mismatches;
                if (!Same(KoralMathNative.koral_noise_simplex2(&native, p.X, p.Y), managed.Simplex(p.X, p.Y))) ++mismatches;
                if (!Same(KoralMathNative.koral_noise_simplex3(&native, p.X, p.Y, p.Z), managed.Simplex(p))) ++mismatches;
                if (!Same(KoralMathNative.koral_noise_value2(&native, p.X, p.Y), managed.Value(p.X, p.Y))) ++mismatches;
                if (!Same(KoralMathNative.koral_noise_value3(&native, p.X, p.Y, p.Z), managed.Value(p))) ++mismatches;
                if (!Same(KoralMathNative.koral_noise_cellular2(&native, p.X, p.Y), managed.Cellular(p.X, p.Y))) ++mismatches;
                if (!Same(KoralMathNative.koral_noise_cellular3(&native, p.X, p.Y, p.Z), managed.Cellular(p))) ++mismatches;
            }
            Check.Equal(0, mismatches, $"noise of seed {seed}, bit for bit");
            var options = new FractalOptions(FractalType.Ridged, 6, 2.1f, 0.45f);
            var nativeOptions = new KoralFractalOptions { type = 1, octaves = 6, lacunarity = 2.1f, gain = 0.45f };
            Check.That(Same(KoralMathNative.koral_noise_fractal3(&native, 1, new Vec3(1.5f, 2.5f, -3.5f), nativeOptions),
                            managed.Fractal(Noise.Kind.Simplex, new Vec3(1.5f, 2.5f, -3.5f), options)), "Fractal");
        }
    }

    public static void MathMatricesMatchNative()
    {
        var random = new Random(3);
        for (int i = 0; i < 500; ++i)
        {
            Vec3 t = random.InsideBox(new Vec3(-5f), new Vec3(5f)), s = random.InsideBox(new Vec3(0.2f), new Vec3(3f));
            Quat q = KoralMathNative.koral_random_rotation(KoralRandomBox.Of(i));
            Mat4 m = Compose(t, q, s), n = KoralMathNative.koral_compose(t, q, s);
            Check.That(Same(m, n), "Compose");
            Check.That(Same(Inverse(m), KoralMathNative.koral_mat4_inverse(m)), "Inverse(Mat4)");
            Check.That(Same(m * Inverse(m), KoralMathNative.koral_mat4_mul(m, KoralMathNative.koral_mat4_inverse(m))), "Mat4 * Mat4");
            Check.That(Same(Determinant(m), KoralMathNative.koral_mat4_determinant(m)), "Determinant");
            Check.That(Same(TransformPoint(m, t), KoralMathNative.koral_transform_point(m, t)), "TransformPoint");
            Check.That(Same(q * t, KoralMathNative.koral_quat_rotate(q, t)), "Quat * Vec3");
            Check.That(Same(Quat.FromMatrix(ToMat3(q)).X, KoralMathNative.koral_quat_from_matrix(ToMat3(q)).X), "Quat.FromMatrix");
            Vec3 eye = random.InsideBox(new Vec3(-9f), new Vec3(9f));
            Check.That(Same(LookAt(eye, t, Vec3.Up), KoralMathNative.koral_look_at(eye, t, Vec3.Up)), "LookAt");
            Check.That(ApproxEqual(Quat.AngleAxis(s.X, t), KoralMathNative.koral_quat_angle_axis(s.X, t), 1e-6f), "AngleAxis");
            Check.That(ApproxEqual(EulerAngles(q), KoralMathNative.koral_quat_euler_angles(q), 1e-5f), "EulerAngles");
            Check.That(SameRotation(Slerp(q, Quat.Identity, 0.3f), KoralMathNative.koral_quat_slerp(q, Quat.Identity, 0.3f), 1e-5f), "Slerp");
        }
        Check.That(ApproxEqual(Perspective(1.1f, 1.7f, 0.1f, 300f), KoralMathNative.koral_perspective(1.1f, 1.7f, 0.1f, 300f, 0), 1e-6f), "Perspective");
        Check.That(Same(Orthographic(-3f, 4f, -1f, 2f, 0.5f, 20f), KoralMathNative.koral_orthographic(-3f, 4f, -1f, 2f, 0.5f, 20f, 0)), "Orthographic");

        var parent = new Transform(new Vec3(1, 2, 3), Quat.AngleAxis(0.5f, Vec3.Up), new Vec3(2f));
        var child = new Transform(new Vec3(-1, 0, 4), Quat.AngleAxis(-0.2f, Vec3.Right));
        Check.That(Same((parent * child).Matrix, KoralMathNative.koral_transform_matrix(KoralMathNative.koral_transform_mul(parent, child))), "Transform");
        Check.That(new Mat4() == Mat4.Identity && new Quat() == Quat.Identity && !new Aabb().Valid, "C++'s defaults");
    }

    public static void MathCamerasAndEulerMatchNative()
    {
        foreach (var clip in Enum.GetValues<ClipSpace>())
        {
            Check.That(Same(Perspective(1.1f, 1.7f, 0.1f, 300f, clip), KoralMathNative.koral_perspective(1.1f, 1.7f, 0.1f, 300f, (int)clip)), $"Perspective {clip}");
            Check.That(Same(PerspectiveFov(1.1f, 640f, 480f, 0.1f, 300f, clip), KoralMathNative.koral_perspective_fov(1.1f, 640f, 480f, 0.1f, 300f, (int)clip)), $"PerspectiveFov {clip}");
            Check.That(Same(InfinitePerspective(1.1f, 1.7f, 0.1f, clip), KoralMathNative.koral_infinite_perspective(1.1f, 1.7f, 0.1f, (int)clip)), $"InfinitePerspective {clip}");
            Check.That(Same(FrustumProjection(-1f, 2f, -1.5f, 1f, 0.5f, 90f, clip), KoralMathNative.koral_frustum_projection(-1f, 2f, -1.5f, 1f, 0.5f, 90f, (int)clip)), $"FrustumProjection {clip}");
            Check.That(Same(Orthographic(-3f, 4f, -1f, 2f, 0.5f, 20f, clip), KoralMathNative.koral_orthographic(-3f, 4f, -1f, 2f, 0.5f, 20f, (int)clip)), $"Orthographic {clip}");
            Mat4 model = Compose(new Vec3(1f, -2f, 3f), Quat.AngleAxis(0.7f, new Vec3(1f, 2f, 3f)), new Vec3(2f, 0.5f, 1.5f));
            Mat4 proj = Perspective(1f, 1.3f, 0.1f, 50f, clip) * LookAt(new Vec3(0f, 0f, 10f), Vec3.Zero);
            var viewport = new Vec4(10f, 20f, 800f, 600f);
            Vec3 w = Project(new Vec3(0.3f, -0.2f, 0.5f), model, proj, viewport, clip);
            Check.That(Same(w, KoralMathNative.koral_project(new Vec3(0.3f, -0.2f, 0.5f), model, proj, viewport, (int)clip)), $"Project {clip}");
            Check.That(Same(UnProject(w, model, proj, viewport, clip), KoralMathNative.koral_unproject(w, model, proj, viewport, (int)clip)), $"UnProject {clip}");
        }
        Check.That(Same(Orthographic(-3f, 4f, -1f, 2f), KoralMathNative.koral_orthographic_2d(-3f, 4f, -1f, 2f)), "Orthographic 2D");
        Check.That(Same(LookAtLH(new Vec3(1f, 2f, 3f), new Vec3(-2f, 0f, 1f)), KoralMathNative.koral_look_at_lh(new Vec3(1f, 2f, 3f), new Vec3(-2f, 0f, 1f), Vec3.Up)), "LookAtLH");
        Check.That(Same(PickMatrix(new Vec2(400f, 300f), new Vec2(5f, 3f), new Vec4(0f, 0f, 800f, 600f)),
                        KoralMathNative.koral_pick_matrix(new Vec2(400f, 300f), new Vec2(5f, 3f), new Vec4(0f, 0f, 800f, 600f))), "PickMatrix");

        var random = new Random(31);
        for (int i = 0; i < 200; ++i)
        {
            var angles = new Vec3(random.NextFloat(-3f, 3f), random.NextFloat(-1.5f, 1.5f), random.NextFloat(-3f, 3f));
            var order = (EulerOrder)(i % 12);
            Mat4 m = EulerAngles(order, angles);
            Check.That(ApproxEqual(m, KoralMathNative.koral_euler_angles((int)order, angles), 1e-6f), $"EulerAngles {order}");
            Check.That(ApproxEqual(ExtractEulerAngles(order, m), KoralMathNative.koral_extract_euler_angles((int)order, m), 1e-4f), $"ExtractEulerAngles {order}");
            Check.That(ApproxEqual(EulerAngles(order, ExtractEulerAngles(order, m)), m, 1e-4f), $"Euler round trip {order}");
            Check.That(ApproxEqual(QuatFromEuler(order, angles), KoralMathNative.koral_quat_from_euler_order((int)order, angles), 1e-6f), $"QuatFromEuler {order}");
            Quat q = random.Rotation();
            Check.That(Same(Pitch(q), KoralMathNative.koral_quat_pitch(q)) && Same(Yaw(q), KoralMathNative.koral_quat_yaw(q)) && Same(Roll(q), KoralMathNative.koral_quat_roll(q)), "Pitch/Yaw/Roll");
            Check.That(ApproxEqual(Rotate(q, 0.4f, angles), KoralMathNative.koral_quat_rotate_axis(q, 0.4f, angles), 1e-6f), "Rotate(Quat)");
            Check.That(ApproxEqual(Rotate(angles, 0.4f, Vec3.UnitY), KoralMathNative.koral_vec3_rotate(angles, 0.4f, Vec3.UnitY), 1e-6f), "Rotate(Vec3)");
            Check.That(Same(Lerp(q, Quat.Identity, 0.3f).X, KoralMathNative.koral_quat_lerp(q, Quat.Identity, 0.3f).X), "Lerp(Quat)");
        }
        Check.That(ApproxEqual(EulerAngleXYZ(0.1f, 0.2f, 0.3f), KoralMathNative.koral_euler_angle_xyz(0.1f, 0.2f, 0.3f), 1e-6f), "EulerAngleXYZ");
        Check.That(ApproxEqual(EulerAngleZY(0.1f, 0.2f), KoralMathNative.koral_euler_angle_zy(0.1f, 0.2f), 1e-6f), "EulerAngleZY");
        Check.That(ApproxEqual(YawPitchRoll(0.1f, 0.2f, 0.3f), KoralMathNative.koral_yaw_pitch_roll(0.1f, 0.2f, 0.3f), 1e-6f), "YawPitchRoll");

        // The glm-shaped surface reads as in C++.
        var v = new Vec4(1f, 2f, 3f, 4f);
        Check.That(v.ZYX == new Vec3(3f, 2f, 1f) && v.XXYY == new Vec4(1f, 1f, 2f, 2f), "swizzles");
        Check.That(All(LessThan(new IVec3(1, 2, 3), new IVec3(2, 3, 4))) && Any(Equal(new UVec2(1u, 2u), new UVec2(0u, 2u))), "relational");
        Check.That(Transpose(new Mat2x3(new Vec3(1f, 2f, 3f), new Vec3(4f, 5f, 6f))) == new Mat3x2(new Vec2(1f, 4f), new Vec2(2f, 5f), new Vec2(3f, 6f)), "Mat2x3");
        Check.That(new DMat4(2.0) * new DVec4(1.0) == new DVec4(2.0), "DMat4");
    }

    public static void MathMaterialColors()
    {
        Check.That(MaterialColors.Red500 == ColorFromHex(0xF44336) && MaterialColors.TealA400 == ColorFromHex(0x1DE9B6), "palette by name");
        foreach (var hue in Enum.GetValues<MaterialHue>())
            foreach (int shade in new[] { 50, 100, 250, 500, 900, 1000 })
            {
                Check.That(MaterialColor(hue, shade) == KoralMathNative.koral_material_color((int)hue, shade), $"MaterialColor {hue} {shade}");
                Check.That(MaterialAccent(hue, shade) == KoralMathNative.koral_material_accent((int)hue, shade), $"MaterialAccent {hue} {shade}");
            }
        // Google's expectations (material-color-utilities' tests).
        var blue = TonalPalette.FromColor(ColorFromHex(0x0000FF));
        Check.That(blue.Tone(90f) == ColorFromHex(0xE0E0FF) && blue.Tone(40f) == ColorFromHex(0x343DFF), "TonalPalette");
        var hct = Hct.FromColor(ColorFromHex(0x0000FF));
        Check.That(MathF.Abs(hct.Hue - 282.788f) < 0.01f && hct.ToColor() == ColorFromHex(0x0000FF), $"Hct {hct}");
        var dark = MaterialScheme.FromSeed(ColorFromHex(0x0000FF), dark: true, SchemeVariant.Monochrome);
        Check.That(MathF.Abs(Hct.FromColor(dark.Primary).Tone - 100f) < 1f, "monochrome dark primary");
        var scheme = MaterialScheme.FromSeed(ColorFromHex(0x6750A4), dark: false);
        Check.That(ContrastRatio(scheme.OnPrimary, scheme.Primary) >= 4.5f, "onPrimary reads");
        byte[] pixels = [.. Enumerable.Repeat(new byte[] { 128, 128, 128, 255 }, 100).SelectMany(p => p), .. Enumerable.Repeat(new byte[] { 255, 0, 0, 255 }, 20).SelectMany(p => p)];
        Check.That(SeedColors(pixels)[0] == ColorFromHex(0xFF0000), "SeedColors");
    }

    public static void MathGeometryMatchesNative()
    {
        var random = new Random(11);
        var viewProjection = Perspective(Radians(70f), 1.3f, 0.5f, 40f) * LookAt(new Vec3(0, 2, 10), Vec3.Zero);
        var frustum = Frustum.FromMatrix(viewProjection);
        var nativeFrustum = KoralMathNative.koral_frustum_from_matrix(viewProjection);
        for (int i = 0; i < 6; ++i) Check.That(Same(frustum[i].Normal, nativeFrustum[i].Normal) && Same(frustum[i].Distance, nativeFrustum[i].Distance), "Frustum planes");

        for (int i = 0; i < 1000; ++i)
        {
            Vec3 c = random.InsideBox(new Vec3(-20f), new Vec3(20f));
            var box = Aabb.FromCenterExtents(c, new Vec3(random.NextFloat(0.1f, 3f)));
            var sphere = new Sphere(c, random.NextFloat(0.1f, 3f));
            var ray = new Ray(random.InsideBox(new Vec3(-20f), new Vec3(20f)), random.OnUnitSphere());
            float d;
            bool hit = KoralMathNative.koral_raycast_aabb(ray, box, float.PositiveInfinity, &d) != 0;
            var mine = Raycast(ray, box);
            Check.That(hit == mine.HasValue && (!hit || Same(d, mine!.Value)), "Raycast(Aabb)");
            hit = KoralMathNative.koral_raycast_sphere(ray, sphere, float.PositiveInfinity, &d) != 0;
            mine = Raycast(ray, sphere);
            Check.That(hit == mine.HasValue && (!hit || Same(d, mine!.Value)), "Raycast(Sphere)");
            var tri = new Triangle(c, c + random.InsideUnitSphere() * 5f, c + random.InsideUnitSphere() * 5f);
            KoralTriangleHitOut th;
            hit = KoralMathNative.koral_raycast_triangle(ray, tri, float.PositiveInfinity, 0, &th.Hit) != 0;
            var tm = Raycast(ray, tri);
            Check.That(hit == tm.HasValue && (!hit || (Same(th.Hit.Distance, tm!.Value.Distance) && Same(th.Hit.U, tm.Value.U))), "Raycast(Triangle)");
            Check.Equal((Containment)KoralMathNative.koral_frustum_classify_aabb(frustum, box), frustum.Classify(box), "Classify(Aabb)");
            Check.Equal((Containment)KoralMathNative.koral_frustum_classify_sphere(frustum, sphere), frustum.Classify(sphere), "Classify(Sphere)");
            Check.That(Same(ClosestPoint(tri, ray.Origin), KoralMathNative.koral_closest_point_triangle(tri, ray.Origin)), "ClosestPoint(Triangle)");
            var obb = new Obb(c, new Vec3(1f, 2f, 0.5f), random.Rotation());
            var obb2 = new Obb(c + random.InsideUnitSphere() * 4f, new Vec3(1f, 0.5f, 2f), random.Rotation());
            Check.Equal(KoralMathNative.koral_overlaps_obb_obb(obb, obb2) != 0, Overlaps(obb, obb2), "Overlaps(Obb, Obb)");
            Mat4 m = Compose(c, random.Rotation(), new Vec3(1f, 2f, 3f));
            Check.Equal(KoralMathNative.koral_aabb_transformed(box, m), box.Transformed(m), "Aabb.Transformed");
        }

        // Bulk: the native vectorised path equals the managed one-at-a-time functions.
        var points = new Vec3[257];
        for (int i = 0; i < points.Length; ++i) points[i] = random.InsideBox(new Vec3(-10f), new Vec3(10f));
        var m2 = Compose(new Vec3(1, 2, 3), random.Rotation(), new Vec3(0.5f, 2f, 1f));
        var moved = new Vec3[points.Length];
        Bulk.TransformPoints(m2, points, moved);
        bool all = true;
        for (int i = 0; i < points.Length; ++i) all &= Same(moved[i], TransformPoint(m2, points[i]));
        Check.That(all, "Bulk.TransformPoints");
        var boxes = points.Select(p => Aabb.FromCenterExtents(p, new Vec3(0.5f))).ToArray();
        var visible = new bool[boxes.Length];
        int count = Bulk.Cull(frustum, boxes, visible);
        Check.Equal(boxes.Count(b => Overlaps(frustum, b)), count, "Bulk.Cull count");
        Check.That(Enumerable.Range(0, boxes.Length).All(i => visible[i] == Overlaps(frustum, boxes[i])), "Bulk.Cull");
        Check.Equal(Aabb.FromPoints(points), Bulk.Bounds(points), "Bulk.Bounds");
    }

    public static void MathColorAndEasingMatchNative()
    {
        for (int e = 0; e <= (int)Easing.InOutBounce; ++e)
            for (float t = 0f; t <= 1f; t += 1f / 64f)
                Check.That(ApproxEqual(Ease((Easing)e, t), KoralMathNative.koral_ease(e, t), 2e-6f), $"Ease({(Easing)e}, {t})");
        var random = new Random(9);
        for (int i = 0; i < 500; ++i)
        {
            var c = new Vec3(random.NextFloat(), random.NextFloat(), random.NextFloat());
            Check.That(ApproxEqual(RgbToHsv(c), KoralMathNative.koral_rgb_to_hsv(c), 1e-6f), "RgbToHsv");
            Check.That(ApproxEqual(HslToRgb(c), KoralMathNative.koral_hsl_to_rgb(c), 1e-6f), "HslToRgb");
            Check.That(ApproxEqual(LinearToOklab(c), KoralMathNative.koral_linear_to_oklab(c), 1e-5f), "LinearToOklab");
            Check.That(ApproxEqual(SrgbToLinear(c.X), KoralMathNative.koral_srgb_to_linear(c.X), 1e-6f), "SrgbToLinear");
            Check.Equal(KoralMathNative.koral_pack_unorm4x8(new Vec4(c, 1f)), PackUnorm4x8(new Vec4(c, 1f)), "PackUnorm4x8");
            float f = random.NextFloat(-70000f, 70000f) * random.NextFloat() * random.NextFloat();
            Check.Equal(KoralMathNative.koral_float_to_half(f), FloatToHalf(f), $"FloatToHalf({f})");
            ushort h = (ushort)random.NextU32(65536);
            Check.That(Same(KoralMathNative.koral_half_to_float(h), HalfToFloat(h)), $"HalfToFloat({h})");
            Vec3 n = random.OnUnitSphere();
            Check.Equal(KoralMathNative.koral_pack_octahedral(n), PackOctahedral(n), "PackOctahedral");
        }
        Vec3[] path = [new(0, 0, 0), new(1, 0, 0), new(1, 1, 0), new(0, 1, 1)];
        fixed (Vec3* p = path)
            for (float t = 0f; t <= 4f; t += 0.25f)
                Check.That(Same(SamplePath(path, t, true), KoralMathNative.koral_sample_path(p, 4, t, 1)), $"SamplePath({t})");
    }
}

/// <summary>Points this assembly's own P/Invokes (Generated/KoralMathNative.g.cs) at the Koral the bindings loaded.</summary>
internal static class MathInterop
{
#pragma warning disable CA2255
    [ModuleInitializer]
#pragma warning restore CA2255
    internal static void Register() => NativeLibrary.SetDllImportResolver(typeof(MathInterop).Assembly, (name, _, _) =>
    {
        if (name != "Koral" && name != "koral-net") return IntPtr.Zero;
        Bulk.Bounds(ReadOnlySpan<Vec3>.Empty);   // any call into Koral makes the bindings find and load it
        var koral = Koral.Native.NativeLibraryResolver.LoadedFrom!;
        if (name == "Koral") return NativeLibrary.Load(koral);
        // koral-net: in modules/ beside Koral, where the module's own binding loads it from.
        var file = OperatingSystem.IsWindows() ? "koral-net.dll" : OperatingSystem.IsMacOS() ? "libkoral-net.dylib" : "libkoral-net.so";
        return NativeLibrary.Load(Path.Combine(Path.GetDirectoryName(Path.GetFullPath(koral))!, "modules", file));
    });
}

/// <summary>A KoralTriangleHit to take the address of.</summary>
internal struct KoralTriangleHitOut { public TriangleHit Hit; }

/// <summary>A native generator per index, for the matrix case's random rotations.</summary>
internal static unsafe class KoralRandomBox
{
    private static readonly KoralRandom* Storage = (KoralRandom*)System.Runtime.InteropServices.NativeMemory.Alloc((nuint)sizeof(KoralRandom));
    public static KoralRandom* Of(int i) { *Storage = KoralMathNative.koral_random_new((ulong)i, 1); return Storage; }
}
