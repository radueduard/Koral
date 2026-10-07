// koral_math_c.h over kmath. Every C struct is the C++ type's bytes, so a value crosses with a bit_cast
// and a pointer with a reinterpret_cast; the static_asserts below are what make that true.
//
// Compiled with -ffp-contract=off like the rest of kmath (CMakeLists.txt): what C# and Kotlin check
// themselves against must be the library's own results, not a fused variant of them.

#include "koral_math_c.h"

#include <kmath.h>

#include <bit>
#include <cstring>

namespace {
    using namespace kor;

    template<class C, class K> constexpr bool SameLayout = sizeof(C) == sizeof(K) && alignof(C) == alignof(K);
    static_assert(SameLayout<KoralVec2, Vec2> && SameLayout<KoralVec3, Vec3> && SameLayout<KoralVec4, Vec4>);
    static_assert(SameLayout<KoralQuat, Quat> && SameLayout<KoralMat3, Mat3> && SameLayout<KoralMat4, Mat4>);
    static_assert(SameLayout<KoralTransform, Transform> && SameLayout<KoralRay, Ray> && SameLayout<KoralPlane, Plane>);
    static_assert(SameLayout<KoralSphere, Sphere> && SameLayout<KoralAabb, Aabb> && SameLayout<KoralObb, Obb>);
    static_assert(SameLayout<KoralTriangle, Triangle> && SameLayout<KoralAabb2, Aabb2> && SameLayout<KoralFrustum, Frustum>);
    static_assert(SameLayout<KoralTriangleHit, TriangleHit> && SameLayout<KoralRandom, Random> && SameLayout<KoralNoise, Noise>);

    template<class To, class From> To As(const From& from) { return std::bit_cast<To>(from); }
    Vec2 K(KoralVec2 v) { return As<Vec2>(v); }
    Vec3 K(KoralVec3 v) { return As<Vec3>(v); }
    Vec4 K(KoralVec4 v) { return As<Vec4>(v); }
    Quat K(KoralQuat v) { return As<Quat>(v); }
    Mat3 K(KoralMat3 v) { return As<Mat3>(v); }
    Mat4 K(KoralMat4 v) { return As<Mat4>(v); }
    Transform K(KoralTransform v) { return As<Transform>(v); }
    Ray K(KoralRay v) { return As<Ray>(v); }
    Plane K(KoralPlane v) { return As<Plane>(v); }
    Sphere K(KoralSphere v) { return As<Sphere>(v); }
    Aabb K(KoralAabb v) { return As<Aabb>(v); }
    Obb K(KoralObb v) { return As<Obb>(v); }
    Triangle K(KoralTriangle v) { return As<Triangle>(v); }
    Aabb2 K(KoralAabb2 v) { return As<Aabb2>(v); }
    Frustum K(const KoralFrustum& v) { return As<Frustum>(v); }
    FractalOptions K(KoralFractalOptions o) { return {FractalType(o.type), o.octaves, o.lacunarity, o.gain}; }

    KoralVec2 C(const Vec2& v) { return As<KoralVec2>(v); }
    KoralVec3 C(const Vec3& v) { return As<KoralVec3>(v); }
    KoralVec4 C(const Vec4& v) { return As<KoralVec4>(v); }
    KoralQuat C(const Quat& v) { return As<KoralQuat>(v); }
    KoralMat3 C(const Mat3& v) { return As<KoralMat3>(v); }
    KoralMat4 C(const Mat4& v) { return As<KoralMat4>(v); }
    KoralTransform C(const Transform& v) { return As<KoralTransform>(v); }
    KoralPlane C(const Plane& v) { return As<KoralPlane>(v); }
    KoralSphere C(const Sphere& v) { return As<KoralSphere>(v); }
    KoralAabb C(const Aabb& v) { return As<KoralAabb>(v); }
    KoralObb C(const Obb& v) { return As<KoralObb>(v); }
    KoralFrustum C(const Frustum& v) { return As<KoralFrustum>(v); }

    Random& R(KoralRandom* r) { return *reinterpret_cast<Random*>(r); }
    const Noise& N(const KoralNoise* n) { return *reinterpret_cast<const Noise*>(n); }
    std::span<const Vec3> Points(const KoralVec3* p, size_t count) { return {reinterpret_cast<const Vec3*>(p), count}; }

    bool Hit(std::optional<float> t, float* distance) {
        if (t && distance) *distance = *t;
        return t.has_value();
    }
}

extern "C" {

KoralMat4 koral_mat4_identity(void) { return C(Mat4()); }
KoralMat4 koral_mat4_mul(KoralMat4 a, KoralMat4 b) { return C(K(a) * K(b)); }
KoralVec4 koral_mat4_mul_vec4(KoralMat4 m, KoralVec4 v) { return C(K(m) * K(v)); }
KoralMat4 koral_mat4_inverse(KoralMat4 m) { return C(Inverse(K(m))); }
KoralMat4 koral_mat4_transpose(KoralMat4 m) { return C(Transpose(K(m))); }
float koral_mat4_determinant(KoralMat4 m) { return Determinant(K(m)); }
KoralMat3 koral_mat4_normal_matrix(KoralMat4 model) { return C(NormalMatrix(K(model))); }
KoralMat3 koral_mat3_mul(KoralMat3 a, KoralMat3 b) { return C(K(a) * K(b)); }
KoralVec3 koral_mat3_mul_vec3(KoralMat3 m, KoralVec3 v) { return C(K(m) * K(v)); }
KoralMat3 koral_mat3_inverse(KoralMat3 m) { return C(Inverse(K(m))); }
KoralMat3 koral_mat3_transpose(KoralMat3 m) { return C(Transpose(K(m))); }
float koral_mat3_determinant(KoralMat3 m) { return Determinant(K(m)); }

KoralMat4 koral_translation(KoralVec3 by) { return C(Translation(K(by))); }
KoralMat4 koral_scaling(KoralVec3 by) { return C(Scaling(K(by))); }
KoralMat4 koral_rotation(float angle, KoralVec3 axis) { return C(Rotation(angle, K(axis))); }
KoralMat4 koral_rotation_quat(KoralQuat q) { return C(Rotation(K(q))); }
KoralMat4 koral_compose(KoralVec3 t, KoralQuat r, KoralVec3 s) { return C(Compose(K(t), K(r), K(s))); }
bool koral_decompose(KoralMat4 m, KoralVec3* translation, KoralQuat* rotation, KoralVec3* scale) {
    Vec3 t, s;
    Quat r;
    const bool ok = Decompose(K(m), t, r, s);
    if (translation) *translation = C(t);
    if (rotation) *rotation = C(r);
    if (scale) *scale = C(s);
    return ok;
}
KoralMat4 koral_translate(KoralMat4 m, KoralVec3 by) { return C(Translate(K(m), K(by))); }
KoralMat4 koral_rotate(KoralMat4 m, float angle, KoralVec3 axis) { return C(Rotate(K(m), angle, K(axis))); }
KoralMat4 koral_scale(KoralMat4 m, KoralVec3 by) { return C(Scale(K(m), K(by))); }
KoralMat4 koral_look_at(KoralVec3 eye, KoralVec3 target, KoralVec3 up) { return C(LookAt(K(eye), K(target), K(up))); }
KoralMat4 koral_perspective(float fovY, float aspect, float n, float f) { return C(Perspective(fovY, aspect, n, f)); }
KoralMat4 koral_perspective_reversed_z(float fovY, float aspect, float n, float f) { return C(PerspectiveReversedZ(fovY, aspect, n, f)); }
KoralMat4 koral_orthographic(float l, float r, float b, float t, float n, float f) { return C(Orthographic(l, r, b, t, n, f)); }
KoralVec3 koral_transform_point(KoralMat4 m, KoralVec3 p) { return C(TransformPoint(K(m), K(p))); }
KoralVec3 koral_transform_point_projective(KoralMat4 m, KoralVec3 p) { return C(TransformPointProjective(K(m), K(p))); }
KoralVec3 koral_transform_direction(KoralMat4 m, KoralVec3 d) { return C(TransformDirection(K(m), K(d))); }

KoralQuat koral_quat_identity(void) { return C(Quat()); }
KoralQuat koral_quat_angle_axis(float angle, KoralVec3 axis) { return C(Quat::AngleAxis(angle, K(axis))); }
KoralQuat koral_quat_from_euler(KoralVec3 euler) { return C(Quat::FromEuler(K(euler))); }
KoralQuat koral_quat_from_matrix(KoralMat3 m) { return C(Quat::FromMatrix(K(m))); }
KoralQuat koral_quat_look_rotation(KoralVec3 direction, KoralVec3 up) { return C(Quat::LookRotation(K(direction), K(up))); }
KoralQuat koral_quat_from_to(KoralVec3 from, KoralVec3 to) { return C(Quat::FromTo(K(from), K(to))); }
KoralQuat koral_quat_mul(KoralQuat a, KoralQuat b) { return C(K(a) * K(b)); }
KoralVec3 koral_quat_rotate(KoralQuat q, KoralVec3 v) { return C(K(q) * K(v)); }
KoralQuat koral_quat_conjugate(KoralQuat q) { return C(Conjugate(K(q))); }
KoralQuat koral_quat_inverse(KoralQuat q) { return C(Inverse(K(q))); }
KoralQuat koral_quat_normalize(KoralQuat q) { return C(Normalize(K(q))); }
float koral_quat_dot(KoralQuat a, KoralQuat b) { return Dot(K(a), K(b)); }
float koral_quat_angle(KoralQuat q) { return Angle(K(q)); }
KoralVec3 koral_quat_axis(KoralQuat q) { return C(Axis(K(q))); }
KoralVec3 koral_quat_euler_angles(KoralQuat q) { return C(EulerAngles(K(q))); }
KoralMat3 koral_quat_to_mat3(KoralQuat q) { return C(ToMat3(K(q))); }
KoralMat4 koral_quat_to_mat4(KoralQuat q) { return C(ToMat4(K(q))); }
KoralQuat koral_quat_slerp(KoralQuat a, KoralQuat b, float t) { return C(Slerp(K(a), K(b), t)); }
KoralQuat koral_quat_nlerp(KoralQuat a, KoralQuat b, float t) { return C(Nlerp(K(a), K(b), t)); }
KoralQuat koral_quat_rotate_towards(KoralQuat from, KoralQuat to, float maxRadians) { return C(RotateTowards(K(from), K(to), maxRadians)); }

KoralTransform koral_transform_identity(void) { return C(Transform()); }
KoralTransform koral_transform_from_matrix(KoralMat4 m) { return C(Transform(K(m))); }
KoralMat4 koral_transform_matrix(KoralTransform t) { return C(K(t).Matrix()); }
KoralTransform koral_transform_mul(KoralTransform parent, KoralTransform child) { return C(K(parent) * K(child)); }
KoralTransform koral_transform_inverse(KoralTransform t) { return C(K(t).Inverse()); }
KoralTransform koral_transform_lerp(KoralTransform a, KoralTransform b, float t) { return C(Lerp(K(a), K(b), t)); }
KoralVec3 koral_transform_transform_point(KoralTransform t, KoralVec3 p) { return C(K(t).TransformPoint(K(p))); }
KoralVec3 koral_transform_transform_direction(KoralTransform t, KoralVec3 d) { return C(K(t).TransformDirection(K(d))); }
KoralVec3 koral_transform_inverse_transform_point(KoralTransform t, KoralVec3 p) { return C(K(t).InverseTransformPoint(K(p))); }
KoralVec3 koral_transform_forward(KoralTransform t) { return C(K(t).Forward()); }
KoralVec3 koral_transform_right(KoralTransform t) { return C(K(t).Right()); }
KoralVec3 koral_transform_up(KoralTransform t) { return C(K(t).Up()); }
KoralTransform koral_transform_look_at(KoralTransform t, KoralVec3 target, KoralVec3 up) {
    Transform k = K(t);
    k.LookAt(K(target), K(up));
    return C(k);
}

KoralPlane koral_plane_from_point_normal(KoralVec3 point, KoralVec3 normal) { return C(Plane::FromPointNormal(K(point), K(normal))); }
KoralPlane koral_plane_from_points(KoralVec3 a, KoralVec3 b, KoralVec3 c) { return C(Plane::FromPoints(K(a), K(b), K(c))); }
float koral_plane_signed_distance(KoralPlane plane, KoralVec3 p) { return K(plane).SignedDistance(K(p)); }
KoralSphere koral_sphere_from_points(const KoralVec3* points, size_t count) { return C(Sphere::FromPoints(Points(points, count))); }
KoralAabb koral_aabb_empty(void) { return C(Aabb()); }
KoralAabb koral_aabb_from_points(const KoralVec3* points, size_t count) { return C(Aabb::FromPoints(Points(points, count))); }
KoralAabb koral_aabb_expand(KoralAabb box, KoralVec3 p) { return C(K(box).Expand(K(p))); }
KoralAabb koral_aabb_transformed(KoralAabb box, KoralMat4 m) { return C(K(box).Transformed(K(m))); }
bool koral_aabb_contains(KoralAabb box, KoralVec3 p) { return K(box).Contains(K(p)); }
KoralObb koral_obb_from_aabb(KoralAabb local, KoralMat4 m) { return C(Obb::FromAabb(K(local), K(m))); }
KoralAabb koral_obb_bounds(KoralObb box) { return C(K(box).Bounds()); }
bool koral_obb_contains(KoralObb box, KoralVec3 p) { return K(box).Contains(K(p)); }
KoralVec3 koral_triangle_barycentric(KoralTriangle triangle, KoralVec3 p) { return C(K(triangle).Barycentric(K(p))); }
KoralFrustum koral_frustum_from_matrix(KoralMat4 viewProjection) { return C(Frustum::FromMatrix(K(viewProjection))); }
void koral_frustum_corners(KoralMat4 viewProjection, KoralVec3* corners) {
    const auto all = Frustum::Corners(K(viewProjection));
    std::memcpy(corners, all.data(), sizeof(all));
}
bool koral_frustum_contains(KoralFrustum frustum, KoralVec3 p) { return K(frustum).Contains(K(p)); }
KoralContainment koral_frustum_classify_aabb(KoralFrustum frustum, KoralAabb box) { return KoralContainment(K(frustum).Classify(K(box))); }
KoralContainment koral_frustum_classify_sphere(KoralFrustum frustum, KoralSphere sphere) { return KoralContainment(K(frustum).Classify(K(sphere))); }

bool koral_raycast_plane(KoralRay ray, KoralPlane plane, float maxDistance, float* distance) { return Hit(Raycast(K(ray), K(plane), maxDistance), distance); }
bool koral_raycast_sphere(KoralRay ray, KoralSphere sphere, float maxDistance, float* distance) { return Hit(Raycast(K(ray), K(sphere), maxDistance), distance); }
bool koral_raycast_aabb(KoralRay ray, KoralAabb box, float maxDistance, float* distance) { return Hit(Raycast(K(ray), K(box), maxDistance), distance); }
bool koral_raycast_obb(KoralRay ray, KoralObb box, float maxDistance, float* distance) { return Hit(Raycast(K(ray), K(box), maxDistance), distance); }
bool koral_raycast_triangle(KoralRay ray, KoralTriangle triangle, float maxDistance, bool cullBackFaces, KoralTriangleHit* hit) {
    const auto h = Raycast(K(ray), K(triangle), maxDistance, cullBackFaces);
    if (h && hit) *hit = As<KoralTriangleHit>(*h);
    return h.has_value();
}

bool koral_overlaps_aabb_aabb(KoralAabb a, KoralAabb b) { return Overlaps(K(a), K(b)); }
bool koral_overlaps_sphere_sphere(KoralSphere a, KoralSphere b) { return Overlaps(K(a), K(b)); }
bool koral_overlaps_aabb_sphere(KoralAabb box, KoralSphere sphere) { return Overlaps(K(box), K(sphere)); }
bool koral_overlaps_obb_obb(KoralObb a, KoralObb b) { return Overlaps(K(a), K(b)); }
bool koral_overlaps_aabb2_aabb2(KoralAabb2 a, KoralAabb2 b) { return Overlaps(K(a), K(b)); }
bool koral_overlaps_frustum_aabb(KoralFrustum frustum, KoralAabb box) { return Overlaps(K(frustum), K(box)); }
bool koral_overlaps_frustum_sphere(KoralFrustum frustum, KoralSphere sphere) { return Overlaps(K(frustum), K(sphere)); }

KoralVec3 koral_closest_point_aabb(KoralAabb box, KoralVec3 p) { return C(ClosestPoint(K(box), K(p))); }
KoralVec3 koral_closest_point_plane(KoralPlane plane, KoralVec3 p) { return C(ClosestPoint(K(plane), K(p))); }
KoralVec3 koral_closest_point_sphere(KoralSphere sphere, KoralVec3 p) { return C(ClosestPoint(K(sphere), K(p))); }
KoralVec3 koral_closest_point_obb(KoralObb box, KoralVec3 p) { return C(ClosestPoint(K(box), K(p))); }
KoralVec3 koral_closest_point_triangle(KoralTriangle triangle, KoralVec3 p) { return C(ClosestPoint(K(triangle), K(p))); }
KoralVec3 koral_closest_point_on_segment(KoralVec3 a, KoralVec3 b, KoralVec3 p) { return C(ClosestPointOnSegment(K(a), K(b), K(p))); }

KoralRandom koral_random_new(uint64_t seed, uint64_t stream) { return As<KoralRandom>(Random(seed, stream)); }
KoralRandom koral_random_from_entropy(void) { return As<KoralRandom>(Random::FromEntropy()); }
uint32_t koral_random_next_u32(KoralRandom* r) { return R(r).NextU32(); }
uint64_t koral_random_next_u64(KoralRandom* r) { return R(r).NextU64(); }
uint32_t koral_random_next_u32_below(KoralRandom* r, uint32_t bound) { return R(r).NextU32(bound); }
int32_t koral_random_next_int(KoralRandom* r, int32_t min, int32_t maxExclusive) { return R(r).NextInt(min, maxExclusive); }
float koral_random_next_float(KoralRandom* r) { return R(r).NextFloat(); }
float koral_random_next_float_range(KoralRandom* r, float min, float max) { return R(r).NextFloat(min, max); }
double koral_random_next_double(KoralRandom* r) { return R(r).NextDouble(); }
bool koral_random_next_bool(KoralRandom* r, float probability) { return R(r).NextBool(probability); }
float koral_random_next_gaussian(KoralRandom* r, float mean, float sd) { return R(r).NextGaussian(mean, sd); }
KoralVec2 koral_random_inside_unit_circle(KoralRandom* r) { return C(R(r).InsideUnitCircle()); }
KoralVec3 koral_random_inside_unit_sphere(KoralRandom* r) { return C(R(r).InsideUnitSphere()); }
KoralVec2 koral_random_on_unit_circle(KoralRandom* r) { return C(R(r).OnUnitCircle()); }
KoralVec3 koral_random_on_unit_sphere(KoralRandom* r) { return C(R(r).OnUnitSphere()); }
KoralQuat koral_random_rotation(KoralRandom* r) { return C(R(r).Rotation()); }
KoralVec3 koral_random_inside_box(KoralRandom* r, KoralVec3 min, KoralVec3 max) { return C(R(r).InsideBox(K(min), K(max))); }
void koral_random_shuffle(KoralRandom* r, void* items, size_t count, size_t size) {
    // Random::Shuffle's own loop, swapping bytes: the same sequence of draws whatever the element.
    auto* bytes = static_cast<unsigned char*>(items);
    for (size_t i = count; i > 1; --i) {
        const size_t j = R(r).NextU32(u32(i));
        if (j != i - 1)
            for (size_t b = 0; b < size; ++b) std::swap(bytes[(i - 1) * size + b], bytes[j * size + b]);
    }
}
void koral_random_advance(KoralRandom* r, uint64_t count) { R(r).Advance(count); }
uint32_t koral_hash(uint32_t v) { return Hash(v); }
uint32_t koral_hash_combine(uint32_t seed, uint32_t v) { return HashCombine(seed, v); }
float koral_hash_to_float(uint32_t v) { return HashToFloat(v); }

void koral_noise_init(KoralNoise* noise, uint32_t seed) { std::construct_at(reinterpret_cast<Noise*>(noise), seed); }
float koral_noise_perlin2(const KoralNoise* n, float x, float y) { return N(n).Perlin(x, y); }
float koral_noise_perlin3(const KoralNoise* n, float x, float y, float z) { return N(n).Perlin(x, y, z); }
float koral_noise_simplex2(const KoralNoise* n, float x, float y) { return N(n).Simplex(x, y); }
float koral_noise_simplex3(const KoralNoise* n, float x, float y, float z) { return N(n).Simplex(x, y, z); }
float koral_noise_value2(const KoralNoise* n, float x, float y) { return N(n).Value(x, y); }
float koral_noise_value3(const KoralNoise* n, float x, float y, float z) { return N(n).Value(x, y, z); }
float koral_noise_cellular2(const KoralNoise* n, float x, float y) { return N(n).Cellular(x, y); }
float koral_noise_cellular3(const KoralNoise* n, float x, float y, float z) { return N(n).Cellular(x, y, z); }
KoralFractalOptions koral_fractal_options_default(void) {
    const FractalOptions o;
    return {u32(o.type), o.octaves, o.lacunarity, o.gain};
}
float koral_noise_fractal2(const KoralNoise* n, KoralNoiseKind kind, KoralVec2 p, KoralFractalOptions options) {
    return N(n).Fractal(Noise::Kind(kind), K(p), K(options));
}
float koral_noise_fractal3(const KoralNoise* n, KoralNoiseKind kind, KoralVec3 p, KoralFractalOptions options) {
    return N(n).Fractal(Noise::Kind(kind), K(p), K(options));
}

float koral_ease(KoralEasing easing, float t) { return Ease(Easing(easing), t); }
KoralVec3 koral_bezier_quadratic(KoralVec3 p0, KoralVec3 p1, KoralVec3 p2, float t) { return C(Bezier(K(p0), K(p1), K(p2), t)); }
KoralVec3 koral_bezier_cubic(KoralVec3 p0, KoralVec3 p1, KoralVec3 p2, KoralVec3 p3, float t) { return C(Bezier(K(p0), K(p1), K(p2), K(p3), t)); }
KoralVec3 koral_bezier_tangent(KoralVec3 p0, KoralVec3 p1, KoralVec3 p2, KoralVec3 p3, float t) { return C(BezierTangent(K(p0), K(p1), K(p2), K(p3), t)); }
KoralVec3 koral_hermite(KoralVec3 p0, KoralVec3 m0, KoralVec3 p1, KoralVec3 m1, float t) { return C(Hermite(K(p0), K(m0), K(p1), K(m1), t)); }
KoralVec3 koral_catmull_rom(KoralVec3 p0, KoralVec3 p1, KoralVec3 p2, KoralVec3 p3, float t) { return C(CatmullRom(K(p0), K(p1), K(p2), K(p3), t)); }
KoralVec3 koral_sample_path(const KoralVec3* points, size_t count, float t, bool closed) { return C(SamplePath(Points(points, count), t, closed)); }

float koral_srgb_to_linear(float c) { return SrgbToLinear(c); }
float koral_linear_to_srgb(float c) { return LinearToSrgb(c); }
KoralVec4 koral_color_from_hex(uint32_t rgb) { return C(ColorFromHex(rgb)); }
KoralVec4 koral_color_from_hex_a(uint32_t rgba) { return C(ColorFromHexA(rgba)); }
float koral_luminance(KoralVec3 linear) { return Luminance(K(linear)); }
KoralVec3 koral_rgb_to_hsv(KoralVec3 rgb) { return C(RgbToHsv(K(rgb))); }
KoralVec3 koral_hsv_to_rgb(KoralVec3 hsv) { return C(HsvToRgb(K(hsv))); }
KoralVec3 koral_rgb_to_hsl(KoralVec3 rgb) { return C(RgbToHsl(K(rgb))); }
KoralVec3 koral_hsl_to_rgb(KoralVec3 hsl) { return C(HslToRgb(K(hsl))); }
KoralVec3 koral_linear_to_oklab(KoralVec3 linear) { return C(LinearToOklab(K(linear))); }
KoralVec3 koral_oklab_to_linear(KoralVec3 lab) { return C(OklabToLinear(K(lab))); }
KoralVec3 koral_mix_oklab(KoralVec3 a, KoralVec3 b, float t) { return C(MixOklab(K(a), K(b), t)); }
KoralVec3 koral_color_temperature(float kelvin) { return C(ColorTemperature(kelvin)); }
uint32_t koral_pack_unorm4x8(KoralVec4 c) { return PackUnorm4x8(K(c)); }
KoralVec4 koral_unpack_unorm4x8(uint32_t packed) { return C(UnpackUnorm4x8(packed)); }
uint16_t koral_float_to_half(float value) { return FloatToHalf(value); }
float koral_half_to_float(uint16_t half) { return HalfToFloat(half); }
uint32_t koral_pack_half2x16(KoralVec2 v) { return PackHalf2x16(K(v)); }
KoralVec2 koral_unpack_half2x16(uint32_t packed) { return C(UnpackHalf2x16(packed)); }
uint32_t koral_pack_octahedral(KoralVec3 n) { return PackOctahedral(K(n)); }
KoralVec3 koral_unpack_octahedral(uint32_t packed) { return C(UnpackOctahedral(packed)); }

void koral_bulk_transform_points(KoralMat4 m, const KoralVec3* points, KoralVec3* out, size_t count) {
    bulk::TransformPoints(K(m), Points(points, count), {reinterpret_cast<Vec3*>(out), count});
}
void koral_bulk_transform_directions(KoralMat4 m, const KoralVec3* directions, KoralVec3* out, size_t count) {
    bulk::TransformDirections(K(m), Points(directions, count), {reinterpret_cast<Vec3*>(out), count});
}
void koral_bulk_transform(KoralMat4 m, const KoralVec4* vectors, KoralVec4* out, size_t count) {
    bulk::Transform(K(m), {reinterpret_cast<const Vec4*>(vectors), count}, {reinterpret_cast<Vec4*>(out), count});
}
void koral_bulk_multiply(const KoralMat4* a, const KoralMat4* b, KoralMat4* out, size_t count) {
    bulk::Multiply({reinterpret_cast<const Mat4*>(a), count}, {reinterpret_cast<const Mat4*>(b), count}, {reinterpret_cast<Mat4*>(out), count});
}
void koral_bulk_multiply_parent(KoralMat4 parent, const KoralMat4* children, KoralMat4* out, size_t count) {
    bulk::Multiply(K(parent), {reinterpret_cast<const Mat4*>(children), count}, {reinterpret_cast<Mat4*>(out), count});
}
void koral_bulk_transform_aabbs(KoralMat4 m, const KoralAabb* boxes, KoralAabb* out, size_t count) {
    bulk::TransformAabbs(K(m), {reinterpret_cast<const Aabb*>(boxes), count}, {reinterpret_cast<Aabb*>(out), count});
}
KoralAabb koral_bulk_bounds(const KoralVec3* points, size_t count) { return C(bulk::Bounds(Points(points, count))); }
size_t koral_bulk_cull_spheres(KoralFrustum frustum, const KoralSphere* spheres, uint8_t* visible, size_t count) {
    return bulk::Cull(K(frustum), std::span{reinterpret_cast<const Sphere*>(spheres), count}, {visible, count});
}
size_t koral_bulk_cull_aabbs(KoralFrustum frustum, const KoralAabb* boxes, uint8_t* visible, size_t count) {
    return bulk::Cull(K(frustum), std::span{reinterpret_cast<const Aabb*>(boxes), count}, {visible, count});
}
void koral_bulk_normalize(KoralVec3* vectors, size_t count) { bulk::Normalize({reinterpret_cast<Vec3*>(vectors), count}); }

}
