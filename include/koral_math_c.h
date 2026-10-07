/*
 * kmath's C interface: Koral's mathematics (kmath.h) for C, and the reference the C# and Kotlin ports are
 * tested against.
 *
 * Plain C (C99). As in koral_c.h, every function is a C++ one, named `koral_<class>_<member>` in snake
 * case — koral_quat_angle_axis is kor::Quat::AngleAxis, koral_raycast_aabb is kor::Raycast(Ray, Aabb),
 * koral_bulk_transform_points is kor::bulk::TransformPoints — and the C++ headers are its documentation.
 *
 * Every type is a plain struct with the C++ type's exact layout, passed and returned by value: a
 * KoralMat4 is sixteen floats, column after column, the same bytes as a kor::Mat4 and as the `float[16]`
 * koral_c.h takes. Nothing here allocates or fails, except where noted.
 *
 * Vector arithmetic is small enough to be `static inline` here rather than a call into the library.
 */

#ifndef KORAL_MATH_C_H
#define KORAL_MATH_C_H

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==== types ========================================================================================== */

typedef struct KoralVec2 { float x, y; } KoralVec2;
typedef struct KoralVec3 { float x, y, z; } KoralVec3;
typedef struct KoralVec4 { float x, y, z, w; } KoralVec4;
typedef struct KoralIVec2 { int32_t x, y; } KoralIVec2;
typedef struct KoralIVec3 { int32_t x, y, z; } KoralIVec3;
typedef struct KoralIVec4 { int32_t x, y, z, w; } KoralIVec4;
typedef struct KoralUVec2 { uint32_t x, y; } KoralUVec2;
typedef struct KoralUVec3 { uint32_t x, y, z; } KoralUVec3;
typedef struct KoralUVec4 { uint32_t x, y, z, w; } KoralUVec4;
/** x, y, z, w — in memory and in koral_quat(); the identity is (0, 0, 0, 1). */
typedef struct KoralQuat { float x, y, z, w; } KoralQuat;
/** Column-major: m[column * 3 + row]. */
typedef struct KoralMat3 { float m[9]; } KoralMat3;
/** Column-major: m[column * 4 + row]. */
typedef struct KoralMat4 { float m[16]; } KoralMat4;

typedef struct KoralTransform { KoralVec3 position; KoralQuat rotation; KoralVec3 scale; } KoralTransform;
typedef struct KoralRay { KoralVec3 origin; KoralVec3 direction; } KoralRay;
typedef struct KoralPlane { KoralVec3 normal; float distance; } KoralPlane;
typedef struct KoralSphere { KoralVec3 center; float radius; } KoralSphere;
typedef struct KoralAabb { KoralVec3 min; KoralVec3 max; } KoralAabb;
typedef struct KoralObb { KoralVec3 center; KoralVec3 half_extents; KoralQuat rotation; } KoralObb;
typedef struct KoralTriangle { KoralVec3 a, b, c; } KoralTriangle;
typedef struct KoralAabb2 { KoralVec2 min; KoralVec2 max; } KoralAabb2;
/** Planes left, right, bottom, top, near, far. */
typedef struct KoralFrustum { KoralPlane planes[6]; } KoralFrustum;
typedef struct KoralTriangleHit { float distance; float u, v; } KoralTriangleHit;

/** kor::Containment. */
typedef enum KoralContainment { KORAL_CONTAINMENT_OUTSIDE = 0, KORAL_CONTAINMENT_INTERSECTS = 1, KORAL_CONTAINMENT_INSIDE = 2 } KoralContainment;

/** kor::Random's whole state: copy it to fork a generator, keep it to resume one. */
typedef struct KoralRandom { uint64_t state; uint64_t increment; } KoralRandom;
/** kor::Noise: a seed and its permutation table. Make one with koral_noise_init. */
typedef struct KoralNoise { uint32_t seed; uint8_t perm[512]; } KoralNoise;
/** kor::Noise::Kind. */
typedef enum KoralNoiseKind { KORAL_NOISE_PERLIN = 0, KORAL_NOISE_SIMPLEX, KORAL_NOISE_VALUE, KORAL_NOISE_CELLULAR } KoralNoiseKind;
/** kor::FractalType. */
typedef enum KoralFractalType { KORAL_FRACTAL_FBM = 0, KORAL_FRACTAL_RIDGED, KORAL_FRACTAL_TURBULENCE } KoralFractalType;
/** kor::FractalOptions; koral_fractal_options_default() gives the C++ defaults. */
typedef struct KoralFractalOptions { uint32_t type; int32_t octaves; float lacunarity; float gain; } KoralFractalOptions;

/** kor::Easing, in its order: KORAL_EASING_LINEAR, then In/Out/InOut of Sine, Quad, Cubic, Quart, Quint, Expo, Circ, Back, Elastic, Bounce. */
typedef enum KoralEasing {
    KORAL_EASING_LINEAR = 0,
    KORAL_EASING_IN_SINE, KORAL_EASING_OUT_SINE, KORAL_EASING_IN_OUT_SINE,
    KORAL_EASING_IN_QUAD, KORAL_EASING_OUT_QUAD, KORAL_EASING_IN_OUT_QUAD,
    KORAL_EASING_IN_CUBIC, KORAL_EASING_OUT_CUBIC, KORAL_EASING_IN_OUT_CUBIC,
    KORAL_EASING_IN_QUART, KORAL_EASING_OUT_QUART, KORAL_EASING_IN_OUT_QUART,
    KORAL_EASING_IN_QUINT, KORAL_EASING_OUT_QUINT, KORAL_EASING_IN_OUT_QUINT,
    KORAL_EASING_IN_EXPO, KORAL_EASING_OUT_EXPO, KORAL_EASING_IN_OUT_EXPO,
    KORAL_EASING_IN_CIRC, KORAL_EASING_OUT_CIRC, KORAL_EASING_IN_OUT_CIRC,
    KORAL_EASING_IN_BACK, KORAL_EASING_OUT_BACK, KORAL_EASING_IN_OUT_BACK,
    KORAL_EASING_IN_ELASTIC, KORAL_EASING_OUT_ELASTIC, KORAL_EASING_IN_OUT_ELASTIC,
    KORAL_EASING_IN_BOUNCE, KORAL_EASING_OUT_BOUNCE, KORAL_EASING_IN_OUT_BOUNCE
} KoralEasing;

/* ==== vectors (inline) ================================================================================ */

static inline KoralVec2 koral_vec2(float x, float y) { KoralVec2 r = {x, y}; return r; }
static inline KoralVec3 koral_vec3(float x, float y, float z) { KoralVec3 r = {x, y, z}; return r; }
static inline KoralVec4 koral_vec4(float x, float y, float z, float w) { KoralVec4 r = {x, y, z, w}; return r; }
static inline KoralQuat koral_quat(float x, float y, float z, float w) { KoralQuat r = {x, y, z, w}; return r; }

static inline KoralVec2 koral_vec2_add(KoralVec2 a, KoralVec2 b) { return koral_vec2(a.x + b.x, a.y + b.y); }
static inline KoralVec2 koral_vec2_sub(KoralVec2 a, KoralVec2 b) { return koral_vec2(a.x - b.x, a.y - b.y); }
static inline KoralVec2 koral_vec2_mul(KoralVec2 a, KoralVec2 b) { return koral_vec2(a.x * b.x, a.y * b.y); }
static inline KoralVec2 koral_vec2_scale(KoralVec2 a, float s) { return koral_vec2(a.x * s, a.y * s); }
static inline float koral_vec2_dot(KoralVec2 a, KoralVec2 b) { return a.x * b.x + a.y * b.y; }
static inline float koral_vec2_length(KoralVec2 a) { return sqrtf(koral_vec2_dot(a, a)); }
static inline KoralVec2 koral_vec2_normalize(KoralVec2 a) { float l = koral_vec2_length(a); return l > 0.f ? koral_vec2_scale(a, 1.f / l) : a; }
static inline KoralVec2 koral_vec2_lerp(KoralVec2 a, KoralVec2 b, float t) { return koral_vec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t); }

static inline KoralVec3 koral_vec3_add(KoralVec3 a, KoralVec3 b) { return koral_vec3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline KoralVec3 koral_vec3_sub(KoralVec3 a, KoralVec3 b) { return koral_vec3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline KoralVec3 koral_vec3_mul(KoralVec3 a, KoralVec3 b) { return koral_vec3(a.x * b.x, a.y * b.y, a.z * b.z); }
static inline KoralVec3 koral_vec3_scale(KoralVec3 a, float s) { return koral_vec3(a.x * s, a.y * s, a.z * s); }
static inline KoralVec3 koral_vec3_negate(KoralVec3 a) { return koral_vec3(-a.x, -a.y, -a.z); }
static inline float koral_vec3_dot(KoralVec3 a, KoralVec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline KoralVec3 koral_vec3_cross(KoralVec3 a, KoralVec3 b) { return koral_vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static inline float koral_vec3_length(KoralVec3 a) { return sqrtf(koral_vec3_dot(a, a)); }
static inline float koral_vec3_distance(KoralVec3 a, KoralVec3 b) { return koral_vec3_length(koral_vec3_sub(b, a)); }
static inline KoralVec3 koral_vec3_normalize(KoralVec3 a) { float l = koral_vec3_length(a); return l > 0.f ? koral_vec3_scale(a, 1.f / l) : a; }
static inline KoralVec3 koral_vec3_lerp(KoralVec3 a, KoralVec3 b, float t) { return koral_vec3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t); }
static inline KoralVec3 koral_vec3_min(KoralVec3 a, KoralVec3 b) { return koral_vec3(b.x < a.x ? b.x : a.x, b.y < a.y ? b.y : a.y, b.z < a.z ? b.z : a.z); }
static inline KoralVec3 koral_vec3_max(KoralVec3 a, KoralVec3 b) { return koral_vec3(a.x < b.x ? b.x : a.x, a.y < b.y ? b.y : a.y, a.z < b.z ? b.z : a.z); }
static inline KoralVec3 koral_vec3_reflect(KoralVec3 i, KoralVec3 n) { return koral_vec3_sub(i, koral_vec3_scale(n, 2.f * koral_vec3_dot(n, i))); }

static inline KoralVec4 koral_vec4_add(KoralVec4 a, KoralVec4 b) { return koral_vec4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w); }
static inline KoralVec4 koral_vec4_sub(KoralVec4 a, KoralVec4 b) { return koral_vec4(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w); }
static inline KoralVec4 koral_vec4_scale(KoralVec4 a, float s) { return koral_vec4(a.x * s, a.y * s, a.z * s, a.w * s); }
static inline float koral_vec4_dot(KoralVec4 a, KoralVec4 b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
static inline KoralVec4 koral_vec4_lerp(KoralVec4 a, KoralVec4 b, float t) { return koral_vec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t); }

/* ==== matrices ======================================================================================= */

KORAL_API KoralMat4 koral_mat4_identity(void);
KORAL_API KoralMat4 koral_mat4_mul(KoralMat4 a, KoralMat4 b);
KORAL_API KoralVec4 koral_mat4_mul_vec4(KoralMat4 m, KoralVec4 v);
KORAL_API KoralMat4 koral_mat4_inverse(KoralMat4 m);
KORAL_API KoralMat4 koral_mat4_transpose(KoralMat4 m);
KORAL_API float koral_mat4_determinant(KoralMat4 m);
/** kor::NormalMatrix. */
KORAL_API KoralMat3 koral_mat4_normal_matrix(KoralMat4 model);
KORAL_API KoralMat3 koral_mat3_mul(KoralMat3 a, KoralMat3 b);
KORAL_API KoralVec3 koral_mat3_mul_vec3(KoralMat3 m, KoralVec3 v);
KORAL_API KoralMat3 koral_mat3_inverse(KoralMat3 m);
KORAL_API KoralMat3 koral_mat3_transpose(KoralMat3 m);
KORAL_API float koral_mat3_determinant(KoralMat3 m);

/* kmath/transform.h */
KORAL_API KoralMat4 koral_translation(KoralVec3 by);
KORAL_API KoralMat4 koral_scaling(KoralVec3 by);
KORAL_API KoralMat4 koral_rotation(float angle, KoralVec3 axis);
KORAL_API KoralMat4 koral_rotation_quat(KoralQuat q);
KORAL_API KoralMat4 koral_compose(KoralVec3 translation, KoralQuat rotation, KoralVec3 scale);
/** False for a degenerate matrix (and the outputs are then unspecified). */
KORAL_API bool koral_decompose(KoralMat4 m, KoralVec3* translation, KoralQuat* rotation, KoralVec3* scale);
KORAL_API KoralMat4 koral_translate(KoralMat4 m, KoralVec3 by);
KORAL_API KoralMat4 koral_rotate(KoralMat4 m, float angle, KoralVec3 axis);
KORAL_API KoralMat4 koral_scale(KoralMat4 m, KoralVec3 by);
KORAL_API KoralMat4 koral_look_at(KoralVec3 eye, KoralVec3 target, KoralVec3 up);
KORAL_API KoralMat4 koral_perspective(float fov_y, float aspect, float near_plane, float far_plane);
/** Pass INFINITY as far_plane for no far plane. */
KORAL_API KoralMat4 koral_perspective_reversed_z(float fov_y, float aspect, float near_plane, float far_plane);
KORAL_API KoralMat4 koral_orthographic(float left, float right, float bottom, float top, float near_plane, float far_plane);
KORAL_API KoralVec3 koral_transform_point(KoralMat4 m, KoralVec3 p);
KORAL_API KoralVec3 koral_transform_point_projective(KoralMat4 m, KoralVec3 p);
KORAL_API KoralVec3 koral_transform_direction(KoralMat4 m, KoralVec3 d);

/* ==== quaternions ==================================================================================== */

KORAL_API KoralQuat koral_quat_identity(void);
KORAL_API KoralQuat koral_quat_angle_axis(float angle, KoralVec3 axis);
KORAL_API KoralQuat koral_quat_from_euler(KoralVec3 euler);
KORAL_API KoralQuat koral_quat_from_matrix(KoralMat3 m);
KORAL_API KoralQuat koral_quat_look_rotation(KoralVec3 direction, KoralVec3 up);
KORAL_API KoralQuat koral_quat_from_to(KoralVec3 from, KoralVec3 to);
KORAL_API KoralQuat koral_quat_mul(KoralQuat a, KoralQuat b);
/** q * v. */
KORAL_API KoralVec3 koral_quat_rotate(KoralQuat q, KoralVec3 v);
KORAL_API KoralQuat koral_quat_conjugate(KoralQuat q);
KORAL_API KoralQuat koral_quat_inverse(KoralQuat q);
KORAL_API KoralQuat koral_quat_normalize(KoralQuat q);
KORAL_API float koral_quat_dot(KoralQuat a, KoralQuat b);
KORAL_API float koral_quat_angle(KoralQuat q);
KORAL_API KoralVec3 koral_quat_axis(KoralQuat q);
KORAL_API KoralVec3 koral_quat_euler_angles(KoralQuat q);
KORAL_API KoralMat3 koral_quat_to_mat3(KoralQuat q);
KORAL_API KoralMat4 koral_quat_to_mat4(KoralQuat q);
KORAL_API KoralQuat koral_quat_slerp(KoralQuat a, KoralQuat b, float t);
KORAL_API KoralQuat koral_quat_nlerp(KoralQuat a, KoralQuat b, float t);
KORAL_API KoralQuat koral_quat_rotate_towards(KoralQuat from, KoralQuat to, float max_radians);

/* ==== kor::Transform ================================================================================= */

KORAL_API KoralTransform koral_transform_identity(void);
KORAL_API KoralTransform koral_transform_from_matrix(KoralMat4 m);
KORAL_API KoralMat4 koral_transform_matrix(KoralTransform t);
/** parent * child. */
KORAL_API KoralTransform koral_transform_mul(KoralTransform parent, KoralTransform child);
KORAL_API KoralTransform koral_transform_inverse(KoralTransform t);
KORAL_API KoralTransform koral_transform_lerp(KoralTransform a, KoralTransform b, float t);
KORAL_API KoralVec3 koral_transform_transform_point(KoralTransform t, KoralVec3 p);
KORAL_API KoralVec3 koral_transform_transform_direction(KoralTransform t, KoralVec3 d);
KORAL_API KoralVec3 koral_transform_inverse_transform_point(KoralTransform t, KoralVec3 p);
KORAL_API KoralVec3 koral_transform_forward(KoralTransform t);
KORAL_API KoralVec3 koral_transform_right(KoralTransform t);
KORAL_API KoralVec3 koral_transform_up(KoralTransform t);
KORAL_API KoralTransform koral_transform_look_at(KoralTransform t, KoralVec3 target, KoralVec3 up);

/* ==== geometry ======================================================================================= */

KORAL_API KoralPlane koral_plane_from_point_normal(KoralVec3 point, KoralVec3 normal);
KORAL_API KoralPlane koral_plane_from_points(KoralVec3 a, KoralVec3 b, KoralVec3 c);
KORAL_API float koral_plane_signed_distance(KoralPlane plane, KoralVec3 p);
KORAL_API KoralSphere koral_sphere_from_points(const KoralVec3* points, size_t count);
/** The empty box (min +inf, max -inf), which koral_aabb_expand turns into the box around what it is given. */
KORAL_API KoralAabb koral_aabb_empty(void);
KORAL_API KoralAabb koral_aabb_from_points(const KoralVec3* points, size_t count);
KORAL_API KoralAabb koral_aabb_expand(KoralAabb box, KoralVec3 p);
KORAL_API KoralAabb koral_aabb_transformed(KoralAabb box, KoralMat4 m);
KORAL_API bool koral_aabb_contains(KoralAabb box, KoralVec3 p);
KORAL_API KoralObb koral_obb_from_aabb(KoralAabb local, KoralMat4 m);
KORAL_API KoralAabb koral_obb_bounds(KoralObb box);
KORAL_API bool koral_obb_contains(KoralObb box, KoralVec3 p);
KORAL_API KoralVec3 koral_triangle_barycentric(KoralTriangle triangle, KoralVec3 p);
KORAL_API KoralFrustum koral_frustum_from_matrix(KoralMat4 view_projection);
/** Writes 8 corners. */
KORAL_API void koral_frustum_corners(KoralMat4 view_projection, KoralVec3* corners);
KORAL_API bool koral_frustum_contains(KoralFrustum frustum, KoralVec3 p);
KORAL_API KoralContainment koral_frustum_classify_aabb(KoralFrustum frustum, KoralAabb box);
KORAL_API KoralContainment koral_frustum_classify_sphere(KoralFrustum frustum, KoralSphere sphere);

/* Ray casts: true and the distance in *distance on a hit (pass INFINITY as max_distance for no limit). */
KORAL_API bool koral_raycast_plane(KoralRay ray, KoralPlane plane, float max_distance, float* distance);
KORAL_API bool koral_raycast_sphere(KoralRay ray, KoralSphere sphere, float max_distance, float* distance);
KORAL_API bool koral_raycast_aabb(KoralRay ray, KoralAabb box, float max_distance, float* distance);
KORAL_API bool koral_raycast_obb(KoralRay ray, KoralObb box, float max_distance, float* distance);
KORAL_API bool koral_raycast_triangle(KoralRay ray, KoralTriangle triangle, float max_distance, bool cull_back_faces, KoralTriangleHit* hit);

KORAL_API bool koral_overlaps_aabb_aabb(KoralAabb a, KoralAabb b);
KORAL_API bool koral_overlaps_sphere_sphere(KoralSphere a, KoralSphere b);
KORAL_API bool koral_overlaps_aabb_sphere(KoralAabb box, KoralSphere sphere);
KORAL_API bool koral_overlaps_obb_obb(KoralObb a, KoralObb b);
KORAL_API bool koral_overlaps_aabb2_aabb2(KoralAabb2 a, KoralAabb2 b);
KORAL_API bool koral_overlaps_frustum_aabb(KoralFrustum frustum, KoralAabb box);
KORAL_API bool koral_overlaps_frustum_sphere(KoralFrustum frustum, KoralSphere sphere);

KORAL_API KoralVec3 koral_closest_point_aabb(KoralAabb box, KoralVec3 p);
KORAL_API KoralVec3 koral_closest_point_plane(KoralPlane plane, KoralVec3 p);
KORAL_API KoralVec3 koral_closest_point_sphere(KoralSphere sphere, KoralVec3 p);
KORAL_API KoralVec3 koral_closest_point_obb(KoralObb box, KoralVec3 p);
KORAL_API KoralVec3 koral_closest_point_triangle(KoralTriangle triangle, KoralVec3 p);
KORAL_API KoralVec3 koral_closest_point_on_segment(KoralVec3 a, KoralVec3 b, KoralVec3 p);

/* ==== kor::Random ==================================================================================== */

KORAL_API KoralRandom koral_random_new(uint64_t seed, uint64_t stream);
/** Seeded from the operating system's entropy. */
KORAL_API KoralRandom koral_random_from_entropy(void);
KORAL_API uint32_t koral_random_next_u32(KoralRandom* random);
KORAL_API uint64_t koral_random_next_u64(KoralRandom* random);
KORAL_API uint32_t koral_random_next_u32_below(KoralRandom* random, uint32_t bound);
KORAL_API int32_t koral_random_next_int(KoralRandom* random, int32_t min, int32_t max_exclusive);
KORAL_API float koral_random_next_float(KoralRandom* random);
KORAL_API float koral_random_next_float_range(KoralRandom* random, float min, float max);
KORAL_API double koral_random_next_double(KoralRandom* random);
KORAL_API bool koral_random_next_bool(KoralRandom* random, float probability);
KORAL_API float koral_random_next_gaussian(KoralRandom* random, float mean, float standard_deviation);
KORAL_API KoralVec2 koral_random_inside_unit_circle(KoralRandom* random);
KORAL_API KoralVec3 koral_random_inside_unit_sphere(KoralRandom* random);
KORAL_API KoralVec2 koral_random_on_unit_circle(KoralRandom* random);
KORAL_API KoralVec3 koral_random_on_unit_sphere(KoralRandom* random);
KORAL_API KoralQuat koral_random_rotation(KoralRandom* random);
KORAL_API KoralVec3 koral_random_inside_box(KoralRandom* random, KoralVec3 min, KoralVec3 max);
/** Fisher–Yates over `count` elements of `size` bytes each. */
KORAL_API void koral_random_shuffle(KoralRandom* random, void* items, size_t count, size_t size);
KORAL_API void koral_random_advance(KoralRandom* random, uint64_t count);
KORAL_API uint32_t koral_hash(uint32_t v);
KORAL_API uint32_t koral_hash_combine(uint32_t seed, uint32_t v);
KORAL_API float koral_hash_to_float(uint32_t v);

/* ==== kor::Noise ===================================================================================== */

KORAL_API void koral_noise_init(KoralNoise* noise, uint32_t seed);
KORAL_API float koral_noise_perlin2(const KoralNoise* noise, float x, float y);
KORAL_API float koral_noise_perlin3(const KoralNoise* noise, float x, float y, float z);
KORAL_API float koral_noise_simplex2(const KoralNoise* noise, float x, float y);
KORAL_API float koral_noise_simplex3(const KoralNoise* noise, float x, float y, float z);
KORAL_API float koral_noise_value2(const KoralNoise* noise, float x, float y);
KORAL_API float koral_noise_value3(const KoralNoise* noise, float x, float y, float z);
KORAL_API float koral_noise_cellular2(const KoralNoise* noise, float x, float y);
KORAL_API float koral_noise_cellular3(const KoralNoise* noise, float x, float y, float z);
KORAL_API KoralFractalOptions koral_fractal_options_default(void);
KORAL_API float koral_noise_fractal2(const KoralNoise* noise, KoralNoiseKind kind, KoralVec2 p, KoralFractalOptions options);
KORAL_API float koral_noise_fractal3(const KoralNoise* noise, KoralNoiseKind kind, KoralVec3 p, KoralFractalOptions options);

/* ==== easing and curves ============================================================================= */

KORAL_API float koral_ease(KoralEasing easing, float t);
KORAL_API KoralVec3 koral_bezier_quadratic(KoralVec3 p0, KoralVec3 p1, KoralVec3 p2, float t);
KORAL_API KoralVec3 koral_bezier_cubic(KoralVec3 p0, KoralVec3 p1, KoralVec3 p2, KoralVec3 p3, float t);
KORAL_API KoralVec3 koral_bezier_tangent(KoralVec3 p0, KoralVec3 p1, KoralVec3 p2, KoralVec3 p3, float t);
KORAL_API KoralVec3 koral_hermite(KoralVec3 p0, KoralVec3 m0, KoralVec3 p1, KoralVec3 m1, float t);
KORAL_API KoralVec3 koral_catmull_rom(KoralVec3 p0, KoralVec3 p1, KoralVec3 p2, KoralVec3 p3, float t);
KORAL_API KoralVec3 koral_sample_path(const KoralVec3* points, size_t count, float t, bool closed);

/* ==== colour ======================================================================================== */

KORAL_API float koral_srgb_to_linear(float c);
KORAL_API float koral_linear_to_srgb(float c);
KORAL_API KoralVec4 koral_color_from_hex(uint32_t rgb);
KORAL_API KoralVec4 koral_color_from_hex_a(uint32_t rgba);
KORAL_API float koral_luminance(KoralVec3 linear);
KORAL_API KoralVec3 koral_rgb_to_hsv(KoralVec3 rgb);
KORAL_API KoralVec3 koral_hsv_to_rgb(KoralVec3 hsv);
KORAL_API KoralVec3 koral_rgb_to_hsl(KoralVec3 rgb);
KORAL_API KoralVec3 koral_hsl_to_rgb(KoralVec3 hsl);
KORAL_API KoralVec3 koral_linear_to_oklab(KoralVec3 linear);
KORAL_API KoralVec3 koral_oklab_to_linear(KoralVec3 lab);
KORAL_API KoralVec3 koral_mix_oklab(KoralVec3 a, KoralVec3 b, float t);
KORAL_API KoralVec3 koral_color_temperature(float kelvin);
KORAL_API uint32_t koral_pack_unorm4x8(KoralVec4 c);
KORAL_API KoralVec4 koral_unpack_unorm4x8(uint32_t packed);
KORAL_API uint16_t koral_float_to_half(float value);
KORAL_API float koral_half_to_float(uint16_t half);
KORAL_API uint32_t koral_pack_half2x16(KoralVec2 v);
KORAL_API KoralVec2 koral_unpack_half2x16(uint32_t packed);
KORAL_API uint32_t koral_pack_octahedral(KoralVec3 n);
KORAL_API KoralVec3 koral_unpack_octahedral(uint32_t packed);

/* ==== kor::bulk ====================================================================================== */
/* `out` holds at least `count` elements and may be the input itself. */

KORAL_API void koral_bulk_transform_points(KoralMat4 m, const KoralVec3* points, KoralVec3* out, size_t count);
KORAL_API void koral_bulk_transform_directions(KoralMat4 m, const KoralVec3* directions, KoralVec3* out, size_t count);
KORAL_API void koral_bulk_transform(KoralMat4 m, const KoralVec4* vectors, KoralVec4* out, size_t count);
KORAL_API void koral_bulk_multiply(const KoralMat4* a, const KoralMat4* b, KoralMat4* out, size_t count);
KORAL_API void koral_bulk_multiply_parent(KoralMat4 parent, const KoralMat4* children, KoralMat4* out, size_t count);
KORAL_API void koral_bulk_transform_aabbs(KoralMat4 m, const KoralAabb* boxes, KoralAabb* out, size_t count);
KORAL_API KoralAabb koral_bulk_bounds(const KoralVec3* points, size_t count);
KORAL_API size_t koral_bulk_cull_spheres(KoralFrustum frustum, const KoralSphere* spheres, uint8_t* visible, size_t count);
KORAL_API size_t koral_bulk_cull_aabbs(KoralFrustum frustum, const KoralAabb* boxes, uint8_t* visible, size_t count);
KORAL_API void koral_bulk_normalize(KoralVec3* vectors, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* KORAL_MATH_C_H */
