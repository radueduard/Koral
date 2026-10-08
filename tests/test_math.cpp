// kmath: the properties that must hold whatever the implementation — identities, inverses, round trips —
// plus the fixed outputs other bindings are checked against (Random's sequence, Noise's field).

#include <gtest/gtest.h>

#include <kmath.h>

#include <algorithm>
#include <format>
#include <unordered_set>
#include <vector>

using namespace kor;

namespace {
    Mat4 SomeAffine() { return Compose(Vec3(1.f, -2.f, 3.f), Quat::AngleAxis(0.7f, Vec3(1.f, 2.f, 3.f)), Vec3(2.f, 0.5f, 1.5f)); }
}

TEST(Math, VectorBasics) {
    constexpr Vec3 a{1.f, 2.f, 3.f}, b{4.f, 5.f, 6.f};
    static_assert(a + b == Vec3(5.f, 7.f, 9.f));
    static_assert(a * 2.f == Vec3(2.f, 4.f, 6.f));
    static_assert(2.f * a == a * 2.f);
    static_assert(Dot(a, b) == 32.f);
    static_assert(Cross(Vec3::UnitX(), Vec3::UnitY()) == Vec3::UnitZ());
    static_assert(-a == Vec3(-1.f, -2.f, -3.f));
    static_assert(Vec4(a, 1.f).w == 1.f && Vec3(Vec4(a, 1.f)) == a);
    static_assert(Min(a, b) == a && Max(a, b) == b);
    static_assert(Clamp(Vec3(-1.f, 0.5f, 9.f), 0.f, 1.f) == Vec3(0.f, 0.5f, 1.f));
    static_assert(All(LessThan(a, b)) && !Any(GreaterThan(a, b)));
    static_assert(IVec2(7, -3) % 4 == IVec2(3, -3));
    static_assert(UVec2(1u, 2u) << 3u == UVec2(8u, 16u));

    EXPECT_FLOAT_EQ(Length(Vec3(3.f, 4.f, 0.f)), 5.f);
    EXPECT_EQ(Normalize(Vec3(0.f)), Vec3(0.f));   // stays zero, not NaN
    EXPECT_TRUE(ApproxEqual(Normalize(Vec3(0.f, 0.f, 9.f)), Vec3::UnitZ()));
    EXPECT_TRUE(ApproxEqual(Reflect(Vec3(1.f, -1.f, 0.f), Vec3::UnitY()), Vec3(1.f, 1.f, 0.f)));
    EXPECT_NEAR(Angle(Vec3::UnitX(), Vec3::UnitY()), HalfPi<float>, 1e-6f);
    EXPECT_NEAR(OrientedAngle(Vec3::UnitX(), Vec3::UnitY(), Vec3::UnitZ()), HalfPi<float>, 1e-6f);
    EXPECT_NEAR(OrientedAngle(Vec3::UnitY(), Vec3::UnitX(), Vec3::UnitZ()), -HalfPi<float>, 1e-6f);

    Vec3 n{0.f, 2.f, 0.f}, t{1.f, 1.f, 0.f};
    n = Normalize(n);
    t = Orthonormalize(t, n);
    EXPECT_NEAR(Dot(n, t), 0.f, 1e-6f);
    EXPECT_NEAR(Length(t), 1.f, 1e-6f);

    EXPECT_EQ(std::format("{}", IVec3(1, -2, 3)), "(1, -2, 3)");
    EXPECT_EQ(std::format("{:.1f}", Vec2(0.25f, 1.f)), "(0.2, 1.0)");
    EXPECT_EQ(std::format("{}", U8Vec4(255, 0, 7, 1)), "(255, 0, 7, 1)");
    std::unordered_set<IVec2> cells{{0, 0}, {1, 0}, {0, 0}};
    EXPECT_EQ(cells.size(), 2u);
}

TEST(Math, Scalars) {
    static_assert(Mod(-1.f, 3.f) == 2.f && Mod(-1, 3) == 2 && Mod(7, 3) == 1);
    static_assert(CeilPowerOfTwo(17u) == 32u && CeilPowerOfTwo(32u) == 32u && IsPowerOfTwo(64u));
    static_assert(AlignUp(13u, 8u) == 16u && DivideRoundUp(13, 8) == 2);
    static_assert(Remap(5.f, 0.f, 10.f, 100.f, 200.f) == 150.f);
    static_assert(SmoothStep(0.f, 1.f, 0.5f) == 0.5f);
    EXPECT_NEAR(DeltaAngle(Radians(350.f), Radians(10.f)), Radians(20.f), 1e-5f);
    EXPECT_NEAR(WrapAngle(Radians(370.f)), Radians(10.f), 1e-5f);
    EXPECT_EQ(MoveTowards(0.f, 10.f, 3.f), 3.f);
    EXPECT_EQ(MoveTowards(9.f, 10.f, 3.f), 10.f);

    float velocity = 0.f, x = 0.f;
    for (int i = 0; i < 600; ++i) x = SmoothDamp(x, 10.f, velocity, 0.3f, 1.f / 60.f);
    EXPECT_NEAR(x, 10.f, 1e-3f);
    Vec3 v3velocity, p;
    for (int i = 0; i < 600; ++i) p = SmoothDamp(p, Vec3(1.f, 2.f, 3.f), v3velocity, 0.3f, 1.f / 60.f);
    EXPECT_TRUE(ApproxEqual(p, Vec3(1.f, 2.f, 3.f), 1e-3f));
}

TEST(Math, MatrixBasics) {
    static_assert(Mat4() == Mat4::Identity());
    static_assert(Mat4()[3][3] == 1.f && Mat4()[0][1] == 0.f);
    constexpr Mat2 m(1.f, 2.f, 3.f, 4.f);   // column-major: first column (1, 2)
    static_assert(m[0] == Vec2(1.f, 2.f) && m.Row(0) == Vec2(1.f, 3.f));
    static_assert(m * Vec2(1.f, 0.f) == Vec2(1.f, 2.f));
    static_assert(Vec2(1.f, 0.f) * m == Vec2(1.f, 3.f));
    static_assert(Determinant(m) == -2.f);
    static_assert(Transpose(m) == Mat2(1.f, 3.f, 2.f, 4.f));

    const Mat4 a = SomeAffine();
    EXPECT_TRUE(ApproxEqual(a * Inverse(a), Mat4::Identity(), 1e-5f));
    EXPECT_TRUE(ApproxEqual(Inverse(a) * a, Mat4::Identity(), 1e-5f));
    const Mat3 a3(a);
    EXPECT_TRUE(ApproxEqual(a3 * Inverse(a3), Mat3::Identity(), 1e-5f));
    EXPECT_NEAR(Determinant(a), 2.f * 0.5f * 1.5f, 1e-5f);
    EXPECT_NEAR(Determinant(a), Determinant(a3), 1e-5f);

    // A non-affine matrix too: the projective row matters.
    const Mat4 proj = Perspective(1.f, 1.5f, 0.1f, 100.f) * LookAt(Vec3(1.f, 2.f, 3.f), Vec3(0.f), Vec3::Up());
    EXPECT_TRUE(ApproxEqual(proj * Inverse(proj), Mat4::Identity(), 1e-4f));

    EXPECT_TRUE(ApproxEqual(Mat3(Translate(Mat4(), Vec3(1.f, 2.f, 3.f))), Mat3()));
    EXPECT_EQ(Translate(Mat4(), Vec3(1.f, 2.f, 3.f)), Translation(Vec3(1.f, 2.f, 3.f)));
    EXPECT_EQ(Scale(Mat4(), Vec3(2.f)), Scaling(Vec3(2.f)));
    EXPECT_TRUE(ApproxEqual(TransformPoint(Translation(Vec3(1.f, 0.f, 0.f)), Vec3(1.f)), Vec3(2.f, 1.f, 1.f)));
    EXPECT_TRUE(ApproxEqual(TransformDirection(Translation(Vec3(1.f, 0.f, 0.f)), Vec3(1.f)), Vec3(1.f)));
}

TEST(Math, Quaternions) {
    const Quat q = Quat::AngleAxis(HalfPi<float>, Vec3::UnitY());
    EXPECT_TRUE(ApproxEqual(q * Vec3::UnitX(), -Vec3::UnitZ()));   // counter-clockwise about +Y
    EXPECT_TRUE(ApproxEqual(ToMat3(q) * Vec3::UnitX(), q * Vec3::UnitX()));
    EXPECT_TRUE(SameRotation(Quat::FromMatrix(ToMat3(q)), q));

    const Quat a = Quat::AngleAxis(0.4f, Normalize(Vec3(1.f, 1.f, 0.f)));
    const Quat b = Quat::AngleAxis(-1.1f, Normalize(Vec3(0.f, 1.f, 2.f)));
    const Vec3 v{0.3f, -0.7f, 2.f};
    EXPECT_TRUE(ApproxEqual((a * b) * v, a * (b * v), 1e-5f));
    EXPECT_TRUE(ApproxEqual(ToMat4(a * b), ToMat4(a) * ToMat4(b), 1e-5f));
    EXPECT_TRUE(ApproxEqual(Inverse(a) * (a * v), v, 1e-5f));
    EXPECT_NEAR(Angle(a), 0.4f, 1e-5f);
    EXPECT_TRUE(ApproxEqual(Axis(a), Normalize(Vec3(1.f, 1.f, 0.f)), 1e-5f));

    const Vec3 euler{0.3f, -0.5f, 1.2f};
    EXPECT_TRUE(ApproxEqual(EulerAngles(Quat::FromEuler(euler)), euler, 1e-5f));

    EXPECT_TRUE(SameRotation(Slerp(a, b, 0.f), a));
    EXPECT_TRUE(SameRotation(Slerp(a, b, 1.f), b));
    const Quat mid = Slerp(Quat(), Quat::AngleAxis(1.f, Vec3::UnitZ()), 0.5f);
    EXPECT_TRUE(SameRotation(mid, Quat::AngleAxis(0.5f, Vec3::UnitZ())));

    const Quat look = Quat::LookRotation(Vec3(1.f, 0.f, 0.f));
    EXPECT_TRUE(ApproxEqual(look * Vec3::Forward(), Vec3::UnitX(), 1e-5f));
    EXPECT_TRUE(ApproxEqual(look * Vec3::Up(), Vec3::UnitY(), 1e-5f));

    for (const Vec3& to : {Vec3(1.f, 0.f, 0.f), Vec3(0.f, -1.f, 0.f), Vec3(-1.f, 2.f, 3.f), Vec3(0.f, 0.f, 1.f)}) {
        const Quat r = Quat::FromTo(Vec3::Forward(), to);
        EXPECT_TRUE(ApproxEqual(r * Vec3::Forward(), Normalize(to), 1e-5f)) << to;
    }
}

TEST(Math, Transforms) {
    const Vec3 t{1.f, -2.f, 3.f}, s{2.f, 0.5f, 1.5f};
    const Quat r = Quat::AngleAxis(0.7f, Normalize(Vec3(1.f, 2.f, 3.f)));
    const Mat4 m = Compose(t, r, s);
    EXPECT_TRUE(ApproxEqual(m, Translation(t) * ToMat4(r) * Scaling(s), 1e-5f));

    Vec3 t2, s2;
    Quat r2;
    ASSERT_TRUE(Decompose(m, t2, r2, s2));
    EXPECT_TRUE(ApproxEqual(t2, t, 1e-5f));
    EXPECT_TRUE(ApproxEqual(s2, s, 1e-5f));
    EXPECT_TRUE(SameRotation(r2, r, 1e-5f));

    const Transform tr(t, r, s);
    EXPECT_TRUE(ApproxEqual(tr.Matrix(), m));
    const Vec3 p{0.5f, 1.f, -2.f};
    EXPECT_TRUE(ApproxEqual(tr.TransformPoint(p), TransformPoint(m, p), 1e-5f));
    EXPECT_TRUE(ApproxEqual(tr.InverseTransformPoint(tr.TransformPoint(p)), p, 1e-5f));

    const Transform parent(Vec3(5.f, 0.f, 0.f), Quat::AngleAxis(1.f, Vec3::UnitY()), Vec3(2.f));
    EXPECT_TRUE(ApproxEqual((parent * tr).Matrix(), parent.Matrix() * tr.Matrix(), 1e-4f));
    EXPECT_TRUE(ApproxEqual((parent * parent.Inverse()).Matrix(), Mat4(), 1e-5f));

    // LookAt puts the target straight ahead, on -Z.
    const Mat4 view = LookAt(Vec3(3.f, 4.f, 5.f), Vec3(1.f, 1.f, 1.f));
    const Vec3 ahead = TransformPoint(view, Vec3(1.f, 1.f, 1.f));
    EXPECT_NEAR(ahead.x, 0.f, 1e-5f);
    EXPECT_NEAR(ahead.y, 0.f, 1e-5f);
    EXPECT_LT(ahead.z, 0.f);

    // Depth 0 at near, 1 at far.
    const Mat4 proj = Perspective(Radians(60.f), 16.f / 9.f, 0.5f, 50.f);
    EXPECT_NEAR(TransformPointProjective(proj, Vec3(0.f, 0.f, -0.5f)).z, 0.f, 1e-6f);
    EXPECT_NEAR(TransformPointProjective(proj, Vec3(0.f, 0.f, -50.f)).z, 1.f, 1e-6f);
    const Mat4 rev = PerspectiveReversedZ(Radians(60.f), 16.f / 9.f, 0.5f, 50.f);
    EXPECT_NEAR(TransformPointProjective(rev, Vec3(0.f, 0.f, -0.5f)).z, 1.f, 1e-6f);
    EXPECT_NEAR(TransformPointProjective(rev, Vec3(0.f, 0.f, -50.f)).z, 0.f, 1e-6f);
    const Mat4 inf = PerspectiveReversedZ(Radians(60.f), 1.f, 0.5f);
    EXPECT_NEAR(TransformPointProjective(inf, Vec3(0.f, 0.f, -0.5f)).z, 1.f, 1e-6f);
    const Mat4 ortho = Orthographic(-2.f, 2.f, -1.f, 1.f, 0.f, 10.f);
    EXPECT_TRUE(ApproxEqual(TransformPoint(ortho, Vec3(2.f, 1.f, -10.f)), Vec3(1.f, 1.f, 1.f)));
}

TEST(Math, Geometry) {
    const Aabb box{Vec3(-1.f), Vec3(1.f)};
    EXPECT_FALSE(Aabb().Valid());
    EXPECT_EQ(Aabb().Expand(Vec3(2.f)), (Aabb{Vec3(2.f), Vec3(2.f)}));
    EXPECT_FLOAT_EQ(box.Volume(), 8.f);
    EXPECT_FLOAT_EQ(box.SurfaceArea(), 24.f);

    EXPECT_FLOAT_EQ(*Raycast(Ray{Vec3(0.f, 0.f, 5.f), Vec3(0.f, 0.f, -1.f)}, box), 4.f);
    EXPECT_FLOAT_EQ(*Raycast(Ray{Vec3(0.f), Vec3(0.f, 0.f, -1.f)}, box), 0.f);   // from inside
    EXPECT_FALSE(Raycast(Ray{Vec3(0.f, 3.f, 5.f), Vec3(0.f, 0.f, -1.f)}, box));
    EXPECT_FALSE(Raycast(Ray{Vec3(0.f, 0.f, 5.f), Vec3(0.f, 0.f, -1.f)}, box, 3.f));   // beyond maxDistance
    EXPECT_FLOAT_EQ(*Raycast(Ray{Vec3(0.f, 0.f, 5.f), Vec3(0.f, 0.f, -1.f)}, Sphere{Vec3(0.f), 2.f}), 3.f);
    EXPECT_FLOAT_EQ(*Raycast(Ray{Vec3(0.f, 5.f, 0.f), Vec3(0.f, -1.f, 0.f)}, Plane{Vec3::UnitY(), 0.f}), 5.f);

    const Triangle tri{Vec3(0.f), Vec3(1.f, 0.f, 0.f), Vec3(0.f, 1.f, 0.f)};
    const auto hit = Raycast(Ray{Vec3(0.25f, 0.25f, 1.f), Vec3(0.f, 0.f, -1.f)}, tri);
    ASSERT_TRUE(hit);
    EXPECT_FLOAT_EQ(hit->distance, 1.f);
    EXPECT_FLOAT_EQ(hit->u, 0.25f);
    EXPECT_FLOAT_EQ(hit->v, 0.25f);
    EXPECT_FALSE(Raycast(Ray{Vec3(0.25f, 0.25f, -1.f), Vec3(0.f, 0.f, 1.f)}, tri, 10.f, true));   // its back
    EXPECT_TRUE(ApproxEqual(tri.Barycentric(Vec3(0.25f, 0.25f, 0.f)), Vec3(0.5f, 0.25f, 0.25f)));
    EXPECT_TRUE(ApproxEqual(ClosestPoint(tri, Vec3(2.f, 2.f, 1.f)), Vec3(0.5f, 0.5f, 0.f)));
    EXPECT_TRUE(ApproxEqual(ClosestPoint(tri, Vec3(-1.f, -1.f, 0.f)), Vec3(0.f)));

    const Obb rotated{Vec3(0.f), Vec3(1.f, 0.1f, 0.1f), Quat::AngleAxis(HalfPi<float>, Vec3::UnitZ())};
    EXPECT_TRUE(rotated.Contains(Vec3(0.f, 0.9f, 0.f)));
    EXPECT_FALSE(rotated.Contains(Vec3(0.9f, 0.f, 0.f)));
    EXPECT_TRUE(ApproxEqual(rotated.Bounds().max, Vec3(0.1f, 1.f, 0.1f), 1e-5f));
    EXPECT_FLOAT_EQ(*Raycast(Ray{Vec3(0.f, 5.f, 0.f), Vec3(0.f, -1.f, 0.f)}, rotated), 4.f);
    const Obb crossing{Vec3(0.f), Vec3(1.f, 0.1f, 0.1f), Quat()};
    const Obb apart{Vec3(3.f, 0.f, 0.f), Vec3(1.f, 0.1f, 0.1f), Quat::AngleAxis(0.3f, Vec3::UnitZ())};
    EXPECT_TRUE(Overlaps(rotated, crossing));
    EXPECT_FALSE(Overlaps(rotated, apart));

    const Mat4 m = Compose(Vec3(1.f, 2.f, 3.f), Quat::AngleAxis(0.6f, Vec3(1.f, 1.f, 0.f)), Vec3(1.f, 2.f, 3.f));
    const Aabb moved = box.Transformed(m);
    for (int i = 0; i < 8; ++i) EXPECT_TRUE(moved.Inflated(1e-5f).Contains(TransformPoint(m, box.Corner(i))));

    const std::vector<Vec3> pts{{1.f, 2.f, 3.f}, {-4.f, 0.f, 1.f}, {2.f, -2.f, 0.f}, {0.f, 5.f, -1.f}};
    const Sphere bound = Sphere::FromPoints(pts);
    for (const Vec3& q : pts) EXPECT_LE(Distance(bound.center, q), bound.radius + 1e-4f);
}

TEST(Math, Frustum) {
    const Mat4 viewProj = Perspective(Radians(90.f), 1.f, 1.f, 100.f) * LookAt(Vec3(0.f), Vec3(0.f, 0.f, -1.f));
    const Frustum f = Frustum::FromMatrix(viewProj);
    EXPECT_TRUE(f.Contains(Vec3(0.f, 0.f, -10.f)));
    EXPECT_FALSE(f.Contains(Vec3(0.f, 0.f, 10.f)));
    EXPECT_FALSE(f.Contains(Vec3(0.f, 0.f, -200.f)));
    EXPECT_FALSE(f.Contains(Vec3(20.f, 0.f, -10.f)));
    EXPECT_EQ(f.Classify(Aabb{Vec3(-1.f, -1.f, -11.f), Vec3(1.f, 1.f, -9.f)}), Containment::eInside);
    EXPECT_EQ(f.Classify(Aabb{Vec3(-1.f, -1.f, -1.5f), Vec3(1.f, 1.f, 0.f)}), Containment::eIntersects);
    EXPECT_EQ(f.Classify(Sphere{Vec3(0.f, 0.f, 10.f), 1.f}), Containment::eOutside);

    // Flipping Y (as cameras do for Vulkan) changes nothing about what is inside.
    Mat4 flipped = Perspective(Radians(90.f), 1.f, 1.f, 100.f);
    flipped[1][1] *= -1.f;
    EXPECT_TRUE(Frustum::FromMatrix(flipped).Contains(Vec3(0.f, 5.f, -10.f)));

    const auto corners = Frustum::Corners(viewProj);
    EXPECT_TRUE(ApproxEqual(corners[0], Vec3(-1.f, -1.f, -1.f), 1e-4f));
    EXPECT_TRUE(ApproxEqual(corners[7], Vec3(100.f, 100.f, -100.f), 1e-2f));
}

TEST(Math, Bulk) {
    Random random(7);
    std::vector<Vec3> points(103);
    for (Vec3& p : points) p = random.InsideBox(Vec3(-10.f), Vec3(10.f));
    const Mat4 m = SomeAffine();

    std::vector<Vec3> out(points.size());
    bulk::TransformPoints(m, points, out);
    for (std::size_t i = 0; i < points.size(); ++i) EXPECT_EQ(out[i], TransformPoint(m, points[i]));   // bit-identical
    bulk::TransformDirections(m, points, out);
    for (std::size_t i = 0; i < points.size(); ++i) EXPECT_EQ(out[i], TransformDirection(m, points[i]));

    std::vector<Vec4> v4(points.size()), v4out(points.size());
    for (std::size_t i = 0; i < points.size(); ++i) v4[i] = Vec4(points[i], random.NextFloat());
    bulk::Transform(m, v4, v4out);
    for (std::size_t i = 0; i < points.size(); ++i) EXPECT_EQ(v4out[i], m * v4[i]);

    std::vector<Mat4> mats(9), mats2(9), prod(9);
    for (std::size_t i = 0; i < mats.size(); ++i) {
        mats[i] = Compose(random.InsideUnitSphere(), random.Rotation(), Vec3(random.NextFloat(0.5f, 2.f)));
        mats2[i] = Compose(random.InsideUnitSphere(), random.Rotation(), Vec3(1.f));
    }
    bulk::Multiply(mats, mats2, prod);
    for (std::size_t i = 0; i < mats.size(); ++i) EXPECT_EQ(prod[i], mats[i] * mats2[i]);
    bulk::Multiply(m, mats, prod);
    for (std::size_t i = 0; i < mats.size(); ++i) EXPECT_EQ(prod[i], m * mats[i]);
    std::vector<Mat4> inPlace = mats;
    bulk::Multiply(m, inPlace, inPlace);
    EXPECT_EQ(inPlace, prod);

    EXPECT_EQ(bulk::Bounds(points), Aabb::FromPoints(points));
    EXPECT_EQ(bulk::Bounds({}), Aabb());

    std::vector<Aabb> boxes(points.size()), moved(points.size());
    for (std::size_t i = 0; i < points.size(); ++i) boxes[i] = Aabb::FromCenterExtents(points[i], Vec3(random.NextFloat(0.1f, 3.f)));
    boxes[5] = Aabb();
    bulk::TransformAabbs(m, boxes, moved);
    for (std::size_t i = 0; i < boxes.size(); ++i) EXPECT_EQ(moved[i], boxes[i].Transformed(m));

    const Frustum f = Frustum::FromMatrix(Perspective(Radians(70.f), 1.3f, 0.5f, 12.f) * LookAt(Vec3(0.f, 0.f, 8.f), Vec3(0.f)));
    std::vector<u8> visible(boxes.size());
    std::size_t expected = 0;
    const std::size_t count = bulk::Cull(f, boxes, visible);
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        EXPECT_EQ(bool(visible[i]), Overlaps(f, boxes[i])) << i;
        expected += Overlaps(f, boxes[i]);
    }
    EXPECT_EQ(count, expected);
    EXPECT_GT(count, 0u);
    EXPECT_LT(count, boxes.size());

    std::vector<Sphere> spheres(points.size());
    for (std::size_t i = 0; i < points.size(); ++i) spheres[i] = {points[i], random.NextFloat(0.1f, 2.f)};
    expected = 0;
    const std::size_t sphereCount = bulk::Cull(f, spheres, visible);
    for (std::size_t i = 0; i < spheres.size(); ++i) {
        EXPECT_EQ(bool(visible[i]), Overlaps(f, spheres[i])) << i;
        expected += Overlaps(f, spheres[i]);
    }
    EXPECT_EQ(sphereCount, expected);

    std::vector<Vec3> normals = points;
    bulk::Normalize(normals);
    for (std::size_t i = 0; i < points.size(); ++i) EXPECT_EQ(normals[i], Normalize(points[i]));
}

TEST(Math, Random) {
    // PCG32's published reference output for seed 42, stream 54 (pcg32-demo.c).
    Random pcg(42u, 54u);
    const u32 reference[] = {0xa15c02b7, 0x7b47f409, 0xba1d3330, 0x83d2f293, 0xbfa4784b, 0xcbed606e};
    for (const u32 expected : reference) EXPECT_EQ(pcg.NextU32(), expected);

    Random a(123), b(123), c(124);
    for (int i = 0; i < 100; ++i) {
        const u32 x = a.NextU32();
        EXPECT_EQ(x, b.NextU32());
        EXPECT_NE(x, c.NextU32());
    }

    Random r(1);
    for (int i = 0; i < 10000; ++i) {
        const float f = r.NextFloat();
        ASSERT_GE(f, 0.f);
        ASSERT_LT(f, 1.f);
        const i32 k = r.NextInt(-3, 4);
        ASSERT_GE(k, -3);
        ASSERT_LT(k, 4);
        ASSERT_LE(Length2(r.InsideUnitSphere()), 1.f);
        ASSERT_NEAR(Length(r.OnUnitSphere()), 1.f, 1e-5f);
        ASSERT_NEAR(Length(r.Rotation()), 1.f, 1e-5f);
    }
    EXPECT_EQ(r.NextInt(5, 5), 5);

    Random jumped(9), walked(9);
    jumped.Advance(1000);
    for (int i = 0; i < 1000; ++i) walked.NextU32();
    EXPECT_EQ(jumped.NextU32(), walked.NextU32());

    std::vector<int> deck{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    Random(5).Shuffle(std::span<int>(deck));
    std::vector<int> sorted = deck;
    std::ranges::sort(sorted);
    EXPECT_EQ(sorted, (std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9}));

    double sum = 0, sumSq = 0;
    Random g(77);
    for (int i = 0; i < 20000; ++i) { const double x = g.NextGaussian(2.f, 3.f); sum += x; sumSq += x * x; }
    const double mean = sum / 20000, var = sumSq / 20000 - mean * mean;
    EXPECT_NEAR(mean, 2.0, 0.1);
    EXPECT_NEAR(std::sqrt(var), 3.0, 0.1);

    EXPECT_NE(Hash(1u), Hash(2u));
    EXPECT_NE(HashCombine(1u, 2u), HashCombine(2u, 1u));
}

TEST(Math, Noise) {
    const Noise n(1234);
    float lo = 1e9f, hi = -1e9f;
    Random r(3);
    for (int i = 0; i < 20000; ++i) {
        const Vec3 p = r.InsideBox(Vec3(-50.f), Vec3(50.f));
        for (const float v : {n.Perlin(p), n.Simplex(p), n.Value(p), n.Perlin(Vec2(p)), n.Simplex(Vec2(p)), n.Value(Vec2(p))}) {
            lo = Min(lo, v);
            hi = Max(hi, v);
        }
        const float c = n.Cellular(p);
        ASSERT_GE(c, 0.f);
        ASSERT_LE(c, 1.8f);
    }
    EXPECT_GE(lo, -1.05f);
    EXPECT_LE(hi, 1.05f);
    EXPECT_LT(lo, -0.5f);   // and actually uses its range
    EXPECT_GT(hi, 0.5f);

    // Gradient noise is zero on the lattice; noise is continuous.
    EXPECT_EQ(n.Perlin(3.f, 4.f, 5.f), 0.f);
    EXPECT_NEAR(n.Simplex(1.3f, 2.7f), n.Simplex(1.3001f, 2.7f), 1e-2f);
    // Seeds give different fields, the same seed the same one.
    EXPECT_NE(Noise(1).Perlin(0.5f, 0.5f, 0.5f), Noise(2).Perlin(0.5f, 0.5f, 0.5f));
    EXPECT_EQ(Noise(1).Simplex(0.3f, 0.6f, 0.9f), Noise(1).Simplex(0.3f, 0.6f, 0.9f));

    const float fbm = n.Fractal(Noise::Kind::eSimplex, Vec3(0.3f, 0.2f, 0.1f));
    EXPECT_GE(fbm, -1.f);
    EXPECT_LE(fbm, 1.f);
    const float ridged = n.Fractal(Noise::Kind::ePerlin, Vec2(0.3f, 0.2f), {.type = FractalType::eRidged});
    EXPECT_GE(ridged, 0.f);
    EXPECT_LE(ridged, 1.f);
}

TEST(Math, Interpolation) {
    for (int e = 0; e <= int(Easing::eInOutBounce); ++e) {
        EXPECT_NEAR(Ease(Easing(e), 0.f), 0.f, 1e-5f) << e;
        EXPECT_NEAR(Ease(Easing(e), 1.f), 1.f, 1e-5f) << e;
    }
    EXPECT_FLOAT_EQ(Ease(Easing::eInQuad, 0.5f), 0.25f);
    EXPECT_FLOAT_EQ(Ease(Easing::eInOutCubic, 0.5f), 0.5f);
    EXPECT_FLOAT_EQ(Ease(Easing::eLinear, 2.f), 1.f);   // clamped
    EXPECT_TRUE(ApproxEqual(Ease(Easing::eOutQuad, Vec2(0.f), Vec2(4.f), 0.5f), Vec2(3.f)));

    const Vec2 p0{0.f, 0.f}, p1{1.f, 2.f}, p2{3.f, 2.f}, p3{4.f, 0.f};
    EXPECT_EQ(Bezier(p0, p1, p2, p3, 0.f), p0);
    EXPECT_EQ(Bezier(p0, p1, p2, p3, 1.f), p3);
    EXPECT_TRUE(ApproxEqual(Bezier(p0, p1, p2, p3, 0.5f), Vec2(2.f, 1.5f)));
    EXPECT_TRUE(ApproxEqual(CatmullRom(p0, p1, p2, p3, 0.f), p1));
    EXPECT_TRUE(ApproxEqual(CatmullRom(p0, p1, p2, p3, 1.f), p2));
    EXPECT_TRUE(ApproxEqual(Hermite(p0, Vec2(1.f, 0.f), p3, Vec2(1.f, 0.f), 1.f), p3));

    const std::vector<Vec3> path{{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {1.f, 1.f, 0.f}, {0.f, 1.f, 0.f}};
    EXPECT_TRUE(ApproxEqual(SamplePath<Vec3>(path, 0.f), path[0]));
    EXPECT_TRUE(ApproxEqual(SamplePath<Vec3>(path, 2.f), path[2]));
    EXPECT_TRUE(ApproxEqual(SamplePath<Vec3>(path, 3.f), path[3]));
    EXPECT_TRUE(ApproxEqual(SamplePath<Vec3>(path, 4.f, true), path[0]));
    EXPECT_TRUE(ApproxEqual(SamplePath<Vec3>(path, 99.f), path[3]));
}

TEST(Math, Color) {
    for (float c : {0.f, 0.02f, 0.2f, 0.5f, 1.f}) EXPECT_NEAR(LinearToSrgb(SrgbToLinear(c)), c, 1e-5f);
    EXPECT_NEAR(SrgbToLinear(0.5f), 0.21404f, 1e-4f);
    EXPECT_EQ(ColorFromHex(0xFF8000), Vec4(1.f, 128.f / 255.f, 0.f, 1.f));
    EXPECT_EQ(ColorFromHexA(0xFF800080).w, 128.f / 255.f);

    const Vec3 rgb{0.2f, 0.6f, 0.4f};
    EXPECT_TRUE(ApproxEqual(HsvToRgb(RgbToHsv(rgb)), rgb));
    EXPECT_TRUE(ApproxEqual(HslToRgb(RgbToHsl(rgb)), rgb));
    EXPECT_TRUE(ApproxEqual(RgbToHsv(Vec3(1.f, 0.f, 0.f)), Vec3(0.f, 1.f, 1.f)));
    EXPECT_TRUE(ApproxEqual(OklabToLinear(LinearToOklab(rgb)), rgb, 1e-4f));
    EXPECT_NEAR(LinearToOklab(Vec3(1.f)).x, 1.f, 1e-4f);
    EXPECT_NEAR(Luminance(Vec3(1.f)), 1.f, 1e-6f);
    const Vec3 warm = ColorTemperature(2000.f), cool = ColorTemperature(10000.f);
    EXPECT_GT(warm.x, warm.z);
    EXPECT_GT(cool.z, cool.x);

    EXPECT_EQ(PackUnorm4x8(Vec4(1.f, 0.f, 0.5f, 1.f)), 0xFF8000FFu);
    EXPECT_TRUE(ApproxEqual(UnpackUnorm4x8(0xFF8000FFu), Vec4(1.f, 0.f, 128.f / 255.f, 1.f)));
    EXPECT_EQ(ToU8Vec4(Vec4(1.f, 0.f, 0.5f, 1.f)), U8Vec4(255, 0, 128, 255));

    for (float f : {0.f, -0.f, 1.f, -2.5f, 65504.f, 0.333333f, 6.1e-5f, 1e-7f}) EXPECT_NEAR(HalfToFloat(FloatToHalf(f)), f, Abs(f) * 1e-3f + 6e-8f) << f;
    EXPECT_EQ(FloatToHalf(1.f), 0x3C00u);
    EXPECT_EQ(FloatToHalf(1e6f), 0x7C00u);   // overflow: infinity
    EXPECT_TRUE(std::isnan(HalfToFloat(FloatToHalf(std::numeric_limits<float>::quiet_NaN()))));
    EXPECT_EQ(UnpackHalf2x16(PackHalf2x16(Vec2(0.5f, -4.f))), Vec2(0.5f, -4.f));

    Random r(11);
    for (int i = 0; i < 1000; ++i) {
        const Vec3 normal = r.OnUnitSphere();
        ASSERT_GT(Dot(UnpackOctahedral(PackOctahedral(normal)), normal), 0.99999f) << normal;
    }
}

TEST(Math, GlslChapters) {
    // Swizzles read any 2-4 components, repeats allowed.
    constexpr Vec4 v{1.f, 2.f, 3.f, 4.f};
    static_assert(v.ZYX() == Vec3(3.f, 2.f, 1.f) && v.XX() == Vec2(1.f, 1.f) && v.WZYX() == Vec4(4.f, 3.f, 2.f, 1.f));

    // Trigonometric / exponential / common work on scalars and component-wise on vectors.
    EXPECT_TRUE(ApproxEqual(Sin(Vec2(0.f, HalfPi<float>)), Vec2(0.f, 1.f)));
    EXPECT_TRUE(ApproxEqual(Degrees(Radians(Vec3(10.f, 20.f, 30.f))), Vec3(10.f, 20.f, 30.f), 1e-4f));
    EXPECT_TRUE(ApproxEqual(Pow(Vec2(2.f, 3.f), Vec2(3.f, 2.f)), Vec2(8.f, 9.f)));
    EXPECT_TRUE(ApproxEqual(InverseSqrt(Vec2(4.f, 16.f)), Vec2(0.5f, 0.25f)));
    EXPECT_EQ(Floor(Vec2(-0.5f, 1.5f)), Vec2(-1.f, 1.f));
    EXPECT_EQ(Fract(1.25f), 0.25f);
    EXPECT_EQ(Mix(Vec2(0.f), Vec2(10.f, 20.f), 0.5f), Vec2(5.f, 10.f));
    EXPECT_EQ(Step(Vec2(0.5f), Vec2(0.f, 1.f)), Vec2(0.f, 1.f));
    EXPECT_EQ(Clamp(IVec3(-5, 5, 50), 0, 10), IVec3(0, 5, 10));

    // Relational returns bool vectors.
    EXPECT_TRUE(All(LessThan(Vec3(1.f), Vec3(2.f))));
    EXPECT_TRUE(Any(Equal(IVec2(1, 2), IVec2(0, 2))));
    EXPECT_FALSE(All(Not(BVec2(true, false))));

    // Integer and component-wise.
    static_assert(BitCount(0b1011u) == 3 && FindLSB(8) == 3 && FindMSB(8) == 3 && FindLSB(0) == -1);
    static_assert(BitfieldExtract(0xABCDu, 4, 8) == 0xBCu && BitfieldReverse(1u) == 0x80000000u);
    static_assert(CompAdd(IVec3(1, 2, 3)) == 6 && CompMul(IVec3(1, 2, 3)) == 6 && CompMax(IVec3(1, 9, 3)) == 9);
    static_assert(CeilMultiple(17, 8) == 24 && FloorPowerOfTwo(17u) == 16u && RoundPowerOfTwo(23u) == 16u);
    u32 carry = 0;
    EXPECT_EQ(UaddCarry(0xffffffffu, 2u, carry), 1u);
    EXPECT_EQ(carry, 1u);

    // Packing round trips.
    EXPECT_TRUE(ApproxEqual(UnpackSnorm2x16(PackSnorm2x16(Vec2(-0.5f, 0.25f))), Vec2(-0.5f, 0.25f), 1e-4f));
    EXPECT_TRUE(ApproxEqual(UnpackUnorm4x8(PackUnorm4x8(Vec4(0.f, 1.f, 0.5f, 0.25f))), Vec4(0.f, 1.f, 0.5f, 0.25f), 3e-3f));
    EXPECT_EQ(UnpackHalf2x16(PackHalf2x16(Vec2(1.5f, -2.f))), Vec2(1.5f, -2.f));
    EXPECT_EQ(UnpackDouble2x32(PackDouble2x32(UVec2(1u, 2u))), UVec2(1u, 2u));
}

TEST(Math, MatrixShapes) {
    const Mat2x3 a(Vec3(1.f, 2.f, 3.f), Vec3(4.f, 5.f, 6.f));   // 2 columns, 3 rows
    const Mat3x2 t = Transpose(a);
    EXPECT_EQ(Row(a, 1), Vec2(2.f, 5.f));
    EXPECT_EQ(Column(t, 2), Vec2(3.f, 6.f));
    const Mat3 p = a * t;
    EXPECT_EQ(p[0][0], 1.f * 1.f + 4.f * 4.f);
    EXPECT_EQ(OuterProduct(Vec3(1.f, 2.f, 3.f), Vec2(1.f, 10.f))[1], Vec3(10.f, 20.f, 30.f));
    EXPECT_EQ(MatrixCompMult(Mat2(2.f), Mat2(3.f))[0][0], 6.f);
    EXPECT_EQ(Diagonal(Vec3(4.f)), Mat3(4.f));
    const Mat3 o = Orthonormalize(Mat3(Vec3(2.f, 0.f, 0.f), Vec3(1.f, 1.f, 0.f), Vec3(1.f, 1.f, 1.f)));
    EXPECT_TRUE(ApproxEqual(Transpose(o) * o, Mat3(1.f), 1e-5f));
}

TEST(Math, Projections) {
    // Each convention maps the near and far planes onto its own depth range, and looks down its own axis.
    for (const ClipSpace clip : {ClipSpace::eRightHandedZeroToOne, ClipSpace::eRightHandedNegativeOneToOne,
                                 ClipSpace::eLeftHandedZeroToOne, ClipSpace::eLeftHandedNegativeOneToOne}) {
        const bool lh = clip == ClipSpace::eLeftHandedZeroToOne || clip == ClipSpace::eLeftHandedNegativeOneToOne;
        const float lo = (clip == ClipSpace::eRightHandedZeroToOne || clip == ClipSpace::eLeftHandedZeroToOne) ? 0.f : -1.f;
        const float forward = lh ? 1.f : -1.f;
        const auto depth = [&](const Mat4& m, float d) { const Vec4 c = m * Vec4(0.f, 0.f, forward * d, 1.f); return c.z / c.w; };
        for (const Mat4& m : {Perspective(1.f, 1.5f, 0.5f, 100.f, clip), PerspectiveFov(1.f, 300.f, 200.f, 0.5f, 100.f, clip),
                              FrustumProjection(-1.f, 2.f, -1.f, 1.f, 0.5f, 100.f, clip), Orthographic(-1.f, 1.f, -1.f, 1.f, 0.5f, 100.f, clip)}) {
            EXPECT_NEAR(depth(m, 0.5f), lo, 1e-5f);
            EXPECT_NEAR(depth(m, 100.f), 1.f, 1e-4f);
        }
        EXPECT_NEAR(depth(InfinitePerspective(1.f, 1.5f, 0.5f, clip), 0.5f), lo, 1e-5f);
        EXPECT_NEAR(depth(InfinitePerspective(1.f, 1.5f, 0.5f, clip), 1e7f), 1.f, 1e-4f);
    }
    EXPECT_TRUE(ApproxEqual(Perspective(1.f, 1.5f, 0.5f, 100.f), PerspectiveFov(1.f, 300.f, 200.f, 0.5f, 100.f), 1e-5f));
    EXPECT_TRUE(ApproxEqual(Perspective(1.f, 1.f, 0.5f, 100.f),
                            FrustumProjection(-0.5f * std::tan(0.5f), 0.5f * std::tan(0.5f), -0.5f * std::tan(0.5f), 0.5f * std::tan(0.5f), 0.5f, 100.f), 1e-5f));

    // LookAtLH looks down +Z: the target lands in front, on the axis.
    const Vec4 seen = LookAtLH(Vec3(1.f, 2.f, 3.f), Vec3(1.f, 2.f, 10.f)) * Vec4(1.f, 2.f, 10.f, 1.f);
    EXPECT_TRUE(ApproxEqual(Vec3(seen), Vec3(0.f, 0.f, 7.f), 1e-5f));

    // Project and UnProject invert each other; the window position matches the hand computation.
    const Vec4 viewport{10.f, 20.f, 800.f, 600.f};
    const Mat4 model = SomeAffine(), proj = Perspective(1.f, 800.f / 600.f, 0.1f, 50.f) * LookAt(Vec3(0.f, 0.f, 10.f), Vec3(0.f));
    const Vec3 point{0.3f, -0.2f, 0.5f};
    const Vec3 window = Project(point, model, proj, viewport);
    EXPECT_TRUE(ApproxEqual(UnProject(window, model, proj, viewport), point, 1e-4f));
    const Vec4 clipped = proj * (model * Vec4(point, 1.f));
    EXPECT_NEAR(window.x, (clipped.x / clipped.w * 0.5f + 0.5f) * 800.f + 10.f, 1e-3f);

    // PickMatrix blows the picked pixel region up to the whole clip square.
    const Mat4 pick = PickMatrix(Vec2(410.f, 320.f), Vec2(4.f, 4.f), viewport);
    EXPECT_TRUE(ApproxEqual(Vec2(pick * Vec4(0.f, 0.f, 0.f, 1.f)), Vec2(0.f), 1e-5f));
    EXPECT_TRUE(ApproxEqual(Vec2(pick * Vec4(4.f / 800.f, 0.f, 0.f, 1.f)), Vec2(1.f, 0.f), 1e-5f));
}

TEST(Math, EulerAngles) {
    EXPECT_TRUE(ApproxEqual(EulerAngleX(0.4f), Rotation(0.4f, Vec3::UnitX()), 1e-6f));
    EXPECT_TRUE(ApproxEqual(EulerAngleY(0.4f), Rotation(0.4f, Vec3::UnitY()), 1e-6f));
    EXPECT_TRUE(ApproxEqual(EulerAngleZ(0.4f), Rotation(0.4f, Vec3::UnitZ()), 1e-6f));
    EXPECT_TRUE(ApproxEqual(EulerAngleXY(0.3f, 0.5f), EulerAngleX(0.3f) * EulerAngleY(0.5f), 1e-6f));
    EXPECT_TRUE(ApproxEqual(YawPitchRoll(0.3f, 0.5f, 0.7f), EulerAngleY(0.3f) * EulerAngleX(0.5f) * EulerAngleZ(0.7f), 1e-6f));

    // Every order round-trips, quaternions agree, and gimbal lock still rebuilds the same rotation.
    for (int o = 0; o < 12; ++o) {
        const auto order = static_cast<EulerOrder>(o);
        const bool proper = o >= 6;
        for (const Vec3 angles : {Vec3(0.3f, 0.5f, -0.7f), Vec3(-2.f, 1.f, 2.5f), Vec3(0.4f, proper ? 0.f : HalfPi<float>, 0.f)}) {
            const Mat4 m = EulerAngles(order, angles);
            EXPECT_TRUE(ApproxEqual(EulerAngles(order, ExtractEulerAngles(order, m)), m, 1e-4f)) << "order " << o << " " << std::format("{}", angles);
            EXPECT_TRUE(ApproxEqual(ToMat4(QuatFromEuler(order, angles)), m, 1e-5f)) << "order " << o;
        }
        if (!proper) {
            const Vec3 a{0.3f, 0.5f, -0.7f};
            EXPECT_TRUE(ApproxEqual(ExtractEulerAngles(order, EulerAngles(order, a)), a, 1e-5f)) << "order " << o;
        }
    }

    const Quat q = Quat::FromEuler(Vec3(0.2f, 0.3f, 0.4f));
    EXPECT_NEAR(Pitch(q), EulerAngles(q).x, 1e-6f);
    EXPECT_NEAR(Yaw(q), EulerAngles(q).y, 1e-6f);
    EXPECT_NEAR(Roll(q), EulerAngles(q).z, 1e-6f);
    EXPECT_TRUE(ApproxEqual(Rotate(Quat::Identity(), 0.5f, Vec3::UnitY()), Quat::AngleAxis(0.5f, Vec3::UnitY())));
    EXPECT_TRUE(ApproxEqual(Rotate(Vec3::UnitX(), HalfPi<float>, Vec3::UnitZ()), Vec3::UnitY(), 1e-6f));
}

TEST(Math, MaterialPalette) {
    // The guidelines' values (as Flutter's colors.dart has them).
    static_assert(material::Red500 == ColorFromHex(0xF44336) && material::TealA400 == ColorFromHex(0x1DE9B6) && material::BlueGrey900 == ColorFromHex(0x263238));
    EXPECT_EQ(MaterialColor(MaterialHue::eIndigo), ColorFromHex(0x3F51B5));
    EXPECT_EQ(MaterialColor(MaterialHue::eIndigo, 520), ColorFromHex(0x3F51B5));   // the nearest shade
    EXPECT_EQ(MaterialColor(MaterialHue::eAmber, 50), ColorFromHex(0xFFF8E1));
    EXPECT_EQ(MaterialAccent(MaterialHue::eRed, 700), ColorFromHex(0xD50000));
    EXPECT_EQ(MaterialAccent(MaterialHue::eGrey, 100), MaterialColor(MaterialHue::eGrey, 100));   // no accents: the plain shade
}

TEST(Math, Material3) {
    // Google's own expectations (material-color-utilities' tones_test, scheme_monochrome_test, score_test).
    const TonalPalette blue = TonalPalette::FromColor(ColorFromHex(0x0000FF));
    EXPECT_EQ(blue.Tone(100.f), ColorFromHex(0xFFFFFF));
    EXPECT_EQ(blue.Tone(90.f), ColorFromHex(0xE0E0FF));
    EXPECT_EQ(blue.Tone(40.f), ColorFromHex(0x343DFF));

    const Hct hct = Hct::FromColor(ColorFromHex(0x0000FF));
    EXPECT_NEAR(hct.hue, 282.788f, 0.01f);
    EXPECT_NEAR(hct.chroma, 87.230f, 0.01f);
    EXPECT_NEAR(hct.tone, 32.302f, 0.01f);
    EXPECT_EQ(hct.ToColor(), ColorFromHex(0x0000FF));

    const auto tone = [](const Vec4& c) { return Hct::FromColor(c).tone; };
    const MaterialScheme dark = MaterialScheme::FromSeed(ColorFromHex(0x0000FF), true, SchemeVariant::eMonochrome);
    EXPECT_NEAR(tone(dark.primary), 100.f, 1.f);
    EXPECT_NEAR(tone(dark.onPrimary), 10.f, 1.f);
    const MaterialScheme light = MaterialScheme::FromSeed(ColorFromHex(0x0000FF), false, SchemeVariant::eMonochrome);
    EXPECT_NEAR(tone(light.primary), 0.f, 1.f);
    EXPECT_NEAR(tone(light.onPrimary), 90.f, 1.f);

    // Every variant gives text that reads on its container, light and dark.
    for (int v = 0; v <= int(SchemeVariant::eFruitSalad); ++v)
        for (const bool d : {false, true}) {
            const MaterialScheme s = MaterialScheme::FromSeed(ColorFromHex(0x6750A4), d, SchemeVariant(v));
            EXPECT_GE(ContrastRatio(s.onPrimary, s.primary), 4.5f) << v << d;
            EXPECT_GE(ContrastRatio(s.onSurface, s.surface), 4.5f) << v << d;
        }

    // Seeds from an image: red outnumbered by grey still wins on chroma; nothing colourful (all white) gives Google blue.
    std::vector<u8> pixels;
    for (int i = 0; i < 100; ++i) pixels.insert(pixels.end(), {128, 128, 128, 255});
    for (int i = 0; i < 20; ++i) pixels.insert(pixels.end(), {255, 0, 0, 255});
    const auto seeds = SeedColors(pixels);
    ASSERT_FALSE(seeds.empty());
    EXPECT_EQ(seeds[0], ColorFromHex(0xFF0000));
    EXPECT_EQ(SeedColors(std::vector<u8>(4 * 64, 255)).front(), ColorFromHex(0x4285F4));
    EXPECT_NEAR(ContrastRatio(ColorFromHex(0x000000), ColorFromHex(0xFFFFFF)), 21.f, 0.01f);
}
