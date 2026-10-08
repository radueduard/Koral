/* kmath from C (koral_math_c.h): which is also the check that the header is C99. */

#include <math.h>
#include <stdio.h>

#include <koral_math_c.h>

static int failures = 0;
#define CHECK(cond, what) do { if (!(cond)) { fprintf(stderr, "FAILED: %s (%s:%d)\n", what, __FILE__, __LINE__); ++failures; } } while (0)
#define NEAR(a, b, eps) (fabsf((a) - (b)) <= (eps))

int main(void) {
    /* PCG32's published reference output for seed 42, stream 54. */
    KoralRandom random = koral_random_new(42u, 54u);
    CHECK(koral_random_next_u32(&random) == 0xa15c02b7u, "pcg32 reference 0");
    CHECK(koral_random_next_u32(&random) == 0x7b47f409u, "pcg32 reference 1");

    KoralVec3 axis = koral_vec3(0.f, 1.f, 0.f);
    KoralQuat q = koral_quat_angle_axis(1.5707963f, axis);
    KoralVec3 turned = koral_quat_rotate(q, koral_vec3(1.f, 0.f, 0.f));
    CHECK(NEAR(turned.x, 0.f, 1e-6f) && NEAR(turned.z, -1.f, 1e-6f), "quat rotates +X to -Z about +Y");

    KoralMat4 m = koral_compose(koral_vec3(1.f, 2.f, 3.f), q, koral_vec3(2.f, 2.f, 2.f));
    KoralMat4 identity = koral_mat4_mul(m, koral_mat4_inverse(m));
    int i;
    for (i = 0; i < 16; ++i) CHECK(NEAR(identity.m[i], (i % 5 == 0) ? 1.f : 0.f, 1e-5f), "m * inverse(m) is the identity");

    KoralAabb box = {{-1.f, -1.f, -1.f}, {1.f, 1.f, 1.f}};
    KoralRay ray = {{0.f, 0.f, 5.f}, {0.f, 0.f, -1.f}};
    float distance = 0.f;
    CHECK(koral_raycast_aabb(ray, box, INFINITY, &distance) && distance == 4.f, "ray hits the box at 4");

    KoralMat4 viewProjection = koral_mat4_mul(koral_perspective(1.5f, 1.f, 0.1f, 100.f, KORAL_CLIP_SPACE_RIGHT_HANDED_ZERO_TO_ONE), koral_look_at(koral_vec3(0.f, 0.f, 5.f), koral_vec3(0.f, 0.f, 0.f), axis));
    KoralFrustum frustum = koral_frustum_from_matrix(viewProjection);
    CHECK(koral_frustum_classify_aabb(frustum, box) == KORAL_CONTAINMENT_INSIDE, "the box is in view");
    KoralAabb boxes[5] = {box, box, box, box, {{50.f, 50.f, 50.f}, {51.f, 51.f, 51.f}}};
    uint8_t visible[5];
    CHECK(koral_bulk_cull_aabbs(frustum, boxes, visible, 5) == 4 && visible[4] == 0, "bulk culling drops the far box");

    KoralNoise noise;
    koral_noise_init(&noise, 7u);
    CHECK(koral_noise_perlin3(&noise, 1.f, 2.f, 3.f) == 0.f, "gradient noise is zero on the lattice");
    float fbm = koral_noise_fractal3(&noise, KORAL_NOISE_SIMPLEX, koral_vec3(0.3f, 0.2f, 0.1f), koral_fractal_options_default());
    CHECK(fbm >= -1.f && fbm <= 1.f, "fbm in range");

    CHECK(koral_ease(KORAL_EASING_IN_QUAD, 0.5f) == 0.25f, "InQuad(0.5)");
    CHECK(koral_pack_unorm4x8(koral_vec4(1.f, 0.f, 0.5f, 1.f)) == 0xFF8000FFu, "packUnorm4x8");
    CHECK(koral_float_to_half(1.f) == 0x3C00u, "half of 1");

    /* The GLSL chapters, every type: named koral_<type>_<function>, _s where a vector takes a scalar. */
    KoralVec3 f = koral_vec3_floor(koral_vec3(-0.5f, 1.5f, 2.f));
    CHECK(f.x == -1.f && f.y == 1.f && f.z == 2.f, "vec3 floor");
    KoralDVec2 mixed = koral_dvec2_mix_s(koral_dvec2(0.0, 10.0), koral_dvec2(10.0, 20.0), 0.5);
    CHECK(mixed.x == 5.0 && mixed.y == 15.0, "dvec2 mix with a scalar t");
    KoralIVec3 c = koral_ivec3_clamp_s(koral_ivec3(-5, 5, 50), 0, 10);
    CHECK(c.x == 0 && c.y == 5 && c.z == 10, "ivec3 clamp");
    CHECK(koral_bvec3_all(koral_vec3_less_than(koral_vec3_splat(1.f), koral_vec3_splat(2.f))), "lessThan + all");
    CHECK(koral_uint_bit_count(0xF0u) == 4 && koral_int_find_msb(8) == 3, "bit counting");
    CHECK(NEAR(koral_vec2_length(koral_vec2(3.f, 4.f)), 5.f, 1e-6f), "length");
    KoralVec3 whole;
    KoralVec3 frac = koral_vec3_modf(koral_vec3(1.25f, -2.5f, 3.f), &whole);
    CHECK(frac.x == 0.25f && whole.y == -2.f && frac.z == 0.f, "modf with an out vector");
    KoralMat2x3 a = koral_mat2x3_outer_product(koral_vec3(1.f, 2.f, 3.f), koral_vec2(1.f, 10.f));
    KoralMat3x2 at = koral_mat2x3_transpose(a);
    CHECK(at.m[1] == 10.f && a.m[5] == 30.f, "outer product and transpose");
    KoralMat4 e = koral_euler_angles(KORAL_EULER_YXZ, koral_vec3(0.3f, 0.5f, 0.7f));
    KoralVec3 back = koral_extract_euler_angles(KORAL_EULER_YXZ, e);
    CHECK(NEAR(back.x, 0.3f, 1e-5f) && NEAR(back.y, 0.5f, 1e-5f) && NEAR(back.z, 0.7f, 1e-5f), "euler round trip");
    CHECK(KORAL_PI_F == 3.14159265f, "constants");
    KoralVec4 red = koral_material_color(KORAL_MATERIAL_HUE_RED, 500), hex = koral_color_from_hex(KORAL_MATERIAL_RED_500);
    CHECK(red.x == hex.x && red.y == hex.y && red.z == hex.z, "Material palette");
    KoralMaterialScheme scheme = koral_material_scheme_from_seed(koral_color_from_hex(0x6750A4u), false, KORAL_SCHEME_TONAL_SPOT, 0.f);
    CHECK(koral_contrast_ratio(scheme.on_primary, scheme.primary) >= 4.5f, "Material 3 scheme");

    if (failures == 0) printf("kmath from C: all good\n");
    return failures == 0 ? 0 : 1;
}
