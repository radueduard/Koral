// Compiled with -ffp-contract=off (see CMakeLists.txt): each result must equal the one-at-a-time function's,
// so the operations below are done in exactly the order those do theirs.

#include "kmath/bulk.h"

#include <algorithm>

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#  include <emmintrin.h>
#  define KOR_SIMD_SSE 1
#elif defined(__ARM_NEON) || defined(__aarch64__) || defined(_M_ARM64)
#  include <arm_neon.h>
#  define KOR_SIMD_NEON 1
#endif

namespace kor::bulk {
    namespace {
        // Four floats in a register, with just what the operations below need.
#if KOR_SIMD_SSE
        struct F4 {
            __m128 v;
            static F4 Load(const float* p) { return {_mm_loadu_ps(p)}; }
            static F4 Set(float x, float y, float z, float w) { return {_mm_set_ps(w, z, y, x)}; }
            static F4 Splat(float s) { return {_mm_set1_ps(s)}; }
            void Store(float* p) const { _mm_storeu_ps(p, v); }
            friend F4 operator+(F4 a, F4 b) { return {_mm_add_ps(a.v, b.v)}; }
            friend F4 operator-(F4 a, F4 b) { return {_mm_sub_ps(a.v, b.v)}; }
            friend F4 operator*(F4 a, F4 b) { return {_mm_mul_ps(a.v, b.v)}; }
            /// Lane-wise b < a ? b : a, which is kor::Min's choice on ties and signed zeros.
            friend F4 Min(F4 a, F4 b) { return {_mm_min_ps(b.v, a.v)}; }
            friend F4 Max(F4 a, F4 b) { return {_mm_max_ps(b.v, a.v)}; }
            /// Bit i set where a[i] < b[i].
            friend int LessMask(F4 a, F4 b) { return _mm_movemask_ps(_mm_cmplt_ps(a.v, b.v)); }
        };
#elif KOR_SIMD_NEON
        struct F4 {
            float32x4_t v;
            static F4 Load(const float* p) { return {vld1q_f32(p)}; }
            static F4 Set(float x, float y, float z, float w) { const float a[4] = {x, y, z, w}; return {vld1q_f32(a)}; }
            static F4 Splat(float s) { return {vdupq_n_f32(s)}; }
            void Store(float* p) const { vst1q_f32(p, v); }
            friend F4 operator+(F4 a, F4 b) { return {vaddq_f32(a.v, b.v)}; }
            friend F4 operator-(F4 a, F4 b) { return {vsubq_f32(a.v, b.v)}; }
            friend F4 operator*(F4 a, F4 b) { return {vmulq_f32(a.v, b.v)}; }
            friend F4 Min(F4 a, F4 b) { return {vbslq_f32(vcltq_f32(b.v, a.v), b.v, a.v)}; }
            friend F4 Max(F4 a, F4 b) { return {vbslq_f32(vcltq_f32(a.v, b.v), b.v, a.v)}; }
            friend int LessMask(F4 a, F4 b) {
                const uint32x4_t m = vcltq_f32(a.v, b.v);
                return int((vgetq_lane_u32(m, 0) & 1u) | (vgetq_lane_u32(m, 1) & 2u) | (vgetq_lane_u32(m, 2) & 4u) | (vgetq_lane_u32(m, 3) & 8u));
            }
        };
#else
        struct F4 {
            float v[4];
            static F4 Load(const float* p) { return {{p[0], p[1], p[2], p[3]}}; }
            static F4 Set(float x, float y, float z, float w) { return {{x, y, z, w}}; }
            static F4 Splat(float s) { return {{s, s, s, s}}; }
            void Store(float* p) const { std::copy(v, v + 4, p); }
            friend F4 operator+(F4 a, F4 b) { return {{a.v[0] + b.v[0], a.v[1] + b.v[1], a.v[2] + b.v[2], a.v[3] + b.v[3]}}; }
            friend F4 operator-(F4 a, F4 b) { return {{a.v[0] - b.v[0], a.v[1] - b.v[1], a.v[2] - b.v[2], a.v[3] - b.v[3]}}; }
            friend F4 operator*(F4 a, F4 b) { return {{a.v[0] * b.v[0], a.v[1] * b.v[1], a.v[2] * b.v[2], a.v[3] * b.v[3]}}; }
            friend F4 Min(F4 a, F4 b) { F4 r; for (int i = 0; i < 4; ++i) r.v[i] = kor::Min(a.v[i], b.v[i]); return r; }
            friend F4 Max(F4 a, F4 b) { F4 r; for (int i = 0; i < 4; ++i) r.v[i] = kor::Max(a.v[i], b.v[i]); return r; }
            friend int LessMask(F4 a, F4 b) { int m = 0; for (int i = 0; i < 4; ++i) m |= (a.v[i] < b.v[i]) << i; return m; }
        };
#endif

        F4 Load3(const Vec3& v) { return F4::Set(v.x, v.y, v.z, 0.f); }
        void Store3(F4 r, Vec3& out) {
            float tmp[4];
            r.Store(tmp);
            out = {tmp[0], tmp[1], tmp[2]};
        }

        struct Columns {
            F4 c[4];
            explicit Columns(const Mat4& m) : c{F4::Load(m[0].data()), F4::Load(m[1].data()), F4::Load(m[2].data()), F4::Load(m[3].data())} {}
            /// ((c0 * x + c1 * y) + c2 * z) + c3 * w: operator*(Mat, Vec)'s order.
            F4 Apply(F4 x, F4 y, F4 z, F4 w) const { return c[0] * x + c[1] * y + c[2] * z + c[3] * w; }
        };

        // Planes in structure-of-arrays form, to test four shapes against one plane at a time.
        struct PlaneLanes { F4 nx, ny, nz, d, ax, ay, az; };
        std::array<PlaneLanes, 6> Lanes(const Frustum& f) {
            std::array<PlaneLanes, 6> lanes;
            for (int i = 0; i < 6; ++i) {
                const Plane& p = f.planes[i];
                lanes[i] = {F4::Splat(p.normal.x), F4::Splat(p.normal.y), F4::Splat(p.normal.z), F4::Splat(p.distance),
                            F4::Splat(Abs(p.normal.x)), F4::Splat(Abs(p.normal.y)), F4::Splat(Abs(p.normal.z))};
            }
            return lanes;
        }
    }

    void TransformPoints(const Mat4& m, std::span<const Vec3> points, std::span<Vec3> out) {
        const Columns cols(m);
        for (std::size_t i = 0; i < points.size(); ++i) {
            const Vec3 p = points[i];
            // TransformPoint: ((c0 * x + c1 * y) + c2 * z) + c3
            Store3(cols.c[0] * F4::Splat(p.x) + cols.c[1] * F4::Splat(p.y) + cols.c[2] * F4::Splat(p.z) + cols.c[3], out[i]);
        }
    }

    void TransformDirections(const Mat4& m, std::span<const Vec3> directions, std::span<Vec3> out) {
        const Columns cols(m);
        for (std::size_t i = 0; i < directions.size(); ++i) {
            const Vec3 d = directions[i];
            Store3(cols.c[0] * F4::Splat(d.x) + cols.c[1] * F4::Splat(d.y) + cols.c[2] * F4::Splat(d.z), out[i]);
        }
    }

    void Transform(const Mat4& m, std::span<const Vec4> vectors, std::span<Vec4> out) {
        const Columns cols(m);
        for (std::size_t i = 0; i < vectors.size(); ++i) {
            const Vec4 v = vectors[i];
            cols.Apply(F4::Splat(v.x), F4::Splat(v.y), F4::Splat(v.z), F4::Splat(v.w)).Store(out[i].data());
        }
    }

    void Multiply(std::span<const Mat4> a, std::span<const Mat4> b, std::span<Mat4> out) {
        const std::size_t n = std::min(a.size(), b.size());
        for (std::size_t i = 0; i < n; ++i) {
            const Columns cols(a[i]);
            const Mat4 rhs = b[i];   // out may alias b
            for (int c = 0; c < 4; ++c)
                cols.Apply(F4::Splat(rhs[c].x), F4::Splat(rhs[c].y), F4::Splat(rhs[c].z), F4::Splat(rhs[c].w)).Store(out[i][c].data());
        }
    }

    void Multiply(const Mat4& parent, std::span<const Mat4> children, std::span<Mat4> out) {
        const Columns cols(parent);
        for (std::size_t i = 0; i < children.size(); ++i) {
            const Mat4 rhs = children[i];
            for (int c = 0; c < 4; ++c)
                cols.Apply(F4::Splat(rhs[c].x), F4::Splat(rhs[c].y), F4::Splat(rhs[c].z), F4::Splat(rhs[c].w)).Store(out[i][c].data());
        }
    }

    void TransformAabbs(const Mat4& m, std::span<const Aabb> boxes, std::span<Aabb> out) {
        const F4 c[3] = {Load3(Vec3(m[0])), Load3(Vec3(m[1])), Load3(Vec3(m[2]))};
        const F4 t = Load3(Vec3(m[3]));
        for (std::size_t i = 0; i < boxes.size(); ++i) {
            const Aabb box = boxes[i];
            if (!box.Valid()) { out[i] = box; continue; }
            F4 lo = t, hi = t;
            for (int k = 0; k < 3; ++k) {
                const F4 a = c[k] * F4::Splat(box.min[k]), b = c[k] * F4::Splat(box.max[k]);
                lo = lo + Min(a, b);
                hi = hi + Max(a, b);
            }
            Store3(lo, out[i].min);
            Store3(hi, out[i].max);
        }
    }

    Aabb Bounds(std::span<const Vec3> points) {
        if (points.empty()) return {};
        F4 lo = Load3(points[0]), hi = lo;
        for (std::size_t i = 1; i < points.size(); ++i) {
            const F4 p = Load3(points[i]);
            lo = Min(lo, p);
            hi = Max(hi, p);
        }
        Aabb box;
        Store3(lo, box.min);
        Store3(hi, box.max);
        return box;
    }

    std::size_t Cull(const Frustum& frustum, std::span<const Sphere> spheres, std::span<u8> visible) {
        const auto planes = Lanes(frustum);
        std::size_t count = 0;
        std::size_t i = 0;
        for (; i + 4 <= spheres.size(); i += 4) {
            const Sphere* s = &spheres[i];
            const F4 cx = F4::Set(s[0].center.x, s[1].center.x, s[2].center.x, s[3].center.x);
            const F4 cy = F4::Set(s[0].center.y, s[1].center.y, s[2].center.y, s[3].center.y);
            const F4 cz = F4::Set(s[0].center.z, s[1].center.z, s[2].center.z, s[3].center.z);
            const F4 negR = F4::Set(-s[0].radius, -s[1].radius, -s[2].radius, -s[3].radius);
            int outside = 0;
            for (const PlaneLanes& p : planes) {
                const F4 d = p.nx * cx + p.ny * cy + p.nz * cz + p.d;   // Plane::SignedDistance's order
                outside |= LessMask(d, negR);
            }
            for (int k = 0; k < 4; ++k) {
                const bool in = !(outside & (1 << k));
                visible[i + k] = in;
                count += in;
            }
        }
        for (; i < spheres.size(); ++i) {
            const bool in = frustum.Classify(spheres[i]) != Containment::eOutside;
            visible[i] = in;
            count += in;
        }
        return count;
    }

    std::size_t Cull(const Frustum& frustum, std::span<const Aabb> boxes, std::span<u8> visible) {
        const auto planes = Lanes(frustum);
        const F4 half = F4::Splat(0.5f);
        std::size_t count = 0;
        std::size_t i = 0;
        for (; i + 4 <= boxes.size(); i += 4) {
            const Aabb* b = &boxes[i];
            if (!(b[0].Valid() && b[1].Valid() && b[2].Valid() && b[3].Valid())) {   // rare: let the scalar path say "outside"
                for (int k = 0; k < 4; ++k) {
                    const bool in = frustum.Classify(b[k]) != Containment::eOutside;
                    visible[i + k] = in;
                    count += in;
                }
                continue;
            }
            const F4 minX = F4::Set(b[0].min.x, b[1].min.x, b[2].min.x, b[3].min.x), maxX = F4::Set(b[0].max.x, b[1].max.x, b[2].max.x, b[3].max.x);
            const F4 minY = F4::Set(b[0].min.y, b[1].min.y, b[2].min.y, b[3].min.y), maxY = F4::Set(b[0].max.y, b[1].max.y, b[2].max.y, b[3].max.y);
            const F4 minZ = F4::Set(b[0].min.z, b[1].min.z, b[2].min.z, b[3].min.z), maxZ = F4::Set(b[0].max.z, b[1].max.z, b[2].max.z, b[3].max.z);
            // Aabb::Center() and HalfExtents(): (min + max) * 0.5, (max - min) * 0.5.
            const F4 cx = (minX + maxX) * half, cy = (minY + maxY) * half, cz = (minZ + maxZ) * half;
            const F4 ex = (maxX - minX) * half, ey = (maxY - minY) * half, ez = (maxZ - minZ) * half;
            const F4 zero = F4::Splat(0.f);
            int outside = 0;
            for (const PlaneLanes& p : planes) {
                const F4 d = p.nx * cx + p.ny * cy + p.nz * cz + p.d;
                const F4 r = ex * p.ax + ey * p.ay + ez * p.az;
                outside |= LessMask(d, zero - r);
            }
            for (int k = 0; k < 4; ++k) {
                const bool in = !(outside & (1 << k));
                visible[i + k] = in;
                count += in;
            }
        }
        for (; i < boxes.size(); ++i) {
            const bool in = frustum.Classify(boxes[i]) != Containment::eOutside;
            visible[i] = in;
            count += in;
        }
        return count;
    }

    void Normalize(std::span<Vec3> vectors) {
        for (Vec3& v : vectors) v = kor::Normalize(v);
    }
}
