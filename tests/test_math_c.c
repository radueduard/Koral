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

    KoralMat4 viewProjection = koral_mat4_mul(koral_perspective(1.5f, 1.f, 0.1f, 100.f), koral_look_at(koral_vec3(0.f, 0.f, 5.f), koral_vec3(0.f, 0.f, 0.f), axis));
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

    if (failures == 0) printf("kmath from C: all good\n");
    return failures == 0 ? 0 : 1;
}
