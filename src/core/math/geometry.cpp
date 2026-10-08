#include "kmath/geometry.h"

namespace kor {
    Sphere Sphere::FromPoints(std::span<const Vec3> points) {
        if (points.empty()) return {};
        // Ritter: start from two far-apart points, then grow to take in any point left outside.
        auto farthestFrom = [&](const Vec3& from) {
            const Vec3* best = &points[0];
            float bestSq = -1.f;
            for (const Vec3& p : points)
                if (const float sq = Distance2(from, p); sq > bestSq) { bestSq = sq; best = &p; }
            return *best;
        };
        const Vec3 a = farthestFrom(points[0]);
        const Vec3 b = farthestFrom(a);
        Sphere s{(a + b) * 0.5f, Distance(a, b) * 0.5f};
        for (const Vec3& p : points) {
            const float d = Distance(s.center, p);
            if (d > s.radius) {
                const float r = (s.radius + d) * 0.5f;
                s.center += (p - s.center) * ((r - s.radius) / d);
                s.radius = r;
            }
        }
        return s;
    }

    Aabb Aabb::FromPoints(std::span<const Vec3> points) {
        Aabb box;
        for (const Vec3& p : points) box.Expand(p);
        return box;
    }

    Aabb Aabb::Transformed(const Mat4& m) const {
        if (!Valid()) return *this;
        Aabb out{Vec3(m[3]), Vec3(m[3])};
        for (int c = 0; c < 3; ++c) {
            const Vec3 a = Vec3(m[c]) * min[c], b = Vec3(m[c]) * max[c];
            out.min += Min(a, b);
            out.max += Max(a, b);
        }
        return out;
    }

    Obb Obb::FromAabb(const Aabb& local, const Mat4& m) {
        Vec3 translation, scale;
        Quat rotation;
        if (!Decompose(m, translation, rotation, scale)) return {TransformPoint(m, local.Center()), Vec3(0.f), {}};
        return {TransformPoint(m, local.Center()), Abs(local.HalfExtents() * scale), rotation};
    }

    Aabb Obb::Bounds() const {
        const Mat3 r = ToMat3(rotation);
        Vec3 extent;
        for (int i = 0; i < 3; ++i)
            extent[i] = Abs(r[0][i]) * halfExtents.x + Abs(r[1][i]) * halfExtents.y + Abs(r[2][i]) * halfExtents.z;
        return {center - extent, center + extent};
    }

    bool Obb::Contains(const Vec3& p) const {
        const Vec3 local = Conjugate(rotation) * (p - center);
        return All(LessThanEqual(Abs(local), halfExtents));
    }

    Vec3 Triangle::Barycentric(const Vec3& p) const {
        const Vec3 v0 = b - a, v1 = c - a, v2 = p - a;
        const float d00 = Dot(v0, v0), d01 = Dot(v0, v1), d11 = Dot(v1, v1);
        const float d20 = Dot(v2, v0), d21 = Dot(v2, v1);
        const float denom = d00 * d11 - d01 * d01;
        if (denom == 0.f) return {1.f, 0.f, 0.f};
        const float v = (d11 * d20 - d01 * d21) / denom;
        const float w = (d00 * d21 - d01 * d20) / denom;
        return {1.f - v - w, v, w};
    }

    Frustum Frustum::FromMatrix(const Mat4& m) {
        // Gribb & Hartmann, with Vulkan's 0 <= z <= w for the near plane.
        const Vec4 r0 = m.Row(0), r1 = m.Row(1), r2 = m.Row(2), r3 = m.Row(3);
        const Vec4 raw[6] = {r3 + r0, r3 - r0, r3 + r1, r3 - r1, r2, r3 - r2};
        Frustum f;
        for (int i = 0; i < 6; ++i) f.planes[i] = Plane{Vec3(raw[i]), raw[i].w}.Normalized();
        // A Y-flipped projection swaps which of bottom/top is which; the set of planes is the same, which is all culling needs.
        return f;
    }

    std::array<Vec3, 8> Frustum::Corners(const Mat4& viewProjection) {
        const Mat4 inv = Inverse(viewProjection);
        std::array<Vec3, 8> corners;
        for (int i = 0; i < 8; ++i)
            corners[i] = TransformPointProjective(inv, Vec3((i & 1) ? 1.f : -1.f, (i & 2) ? 1.f : -1.f, (i & 4) ? 1.f : 0.f));
        return corners;
    }

    bool Frustum::Contains(const Vec3& p) const {
        for (const Plane& plane : planes)
            if (plane.SignedDistance(p) < 0.f) return false;
        return true;
    }

    Containment Frustum::Classify(const Aabb& box) const {
        if (!box.Valid()) return Containment::eOutside;
        const Vec3 center = box.Center(), extent = box.HalfExtents();
        Containment result = Containment::eInside;
        for (const Plane& plane : planes) {
            const float d = plane.SignedDistance(center);
            const float r = Dot(extent, Abs(plane.normal));
            if (d < -r) return Containment::eOutside;
            if (d < r) result = Containment::eIntersects;
        }
        return result;
    }

    Containment Frustum::Classify(const Sphere& sphere) const {
        Containment result = Containment::eInside;
        for (const Plane& plane : planes) {
            const float d = plane.SignedDistance(sphere.center);
            if (d < -sphere.radius) return Containment::eOutside;
            if (d < sphere.radius) result = Containment::eIntersects;
        }
        return result;
    }

    std::optional<float> Raycast(const Ray& ray, const Plane& plane, float maxDistance) {
        const float denom = Dot(plane.normal, ray.direction);
        if (Abs(denom) < 1e-8f) return std::nullopt;
        const float t = -plane.SignedDistance(ray.origin) / denom;
        if (t < 0.f || t > maxDistance) return std::nullopt;
        return t;
    }

    std::optional<float> Raycast(const Ray& ray, const Sphere& sphere, float maxDistance) {
        const Vec3 m = ray.origin - sphere.center;
        const float a = Dot(ray.direction, ray.direction);
        const float b = Dot(m, ray.direction);
        const float c = Dot(m, m) - sphere.radius * sphere.radius;
        if (c <= 0.f) return 0.f;   // starts inside
        if (b > 0.f || a == 0.f) return std::nullopt;
        const float disc = b * b - a * c;
        if (disc < 0.f) return std::nullopt;
        const float t = (-b - std::sqrt(disc)) / a;
        if (t > maxDistance) return std::nullopt;
        return t;
    }

    std::optional<float> Raycast(const Ray& ray, const Aabb& box, float maxDistance) {
        float tMin = 0.f, tMax = maxDistance;
        for (int i = 0; i < 3; ++i) {
            if (Abs(ray.direction[i]) < 1e-12f) {
                if (ray.origin[i] < box.min[i] || ray.origin[i] > box.max[i]) return std::nullopt;
                continue;
            }
            const float inv = 1.f / ray.direction[i];
            float t0 = (box.min[i] - ray.origin[i]) * inv, t1 = (box.max[i] - ray.origin[i]) * inv;
            if (t0 > t1) std::swap(t0, t1);
            tMin = Max(tMin, t0);
            tMax = Min(tMax, t1);
            if (tMin > tMax) return std::nullopt;
        }
        return tMin;
    }

    std::optional<float> Raycast(const Ray& ray, const Obb& box, float maxDistance) {
        const Quat inv = Conjugate(box.rotation);
        const Ray local{inv * (ray.origin - box.center), inv * ray.direction};
        return Raycast(local, Aabb{-box.halfExtents, box.halfExtents}, maxDistance);
    }

    std::optional<TriangleHit> Raycast(const Ray& ray, const Triangle& tri, float maxDistance, bool cullBackFaces) {
        constexpr float eps = 1e-8f;
        const Vec3 e1 = tri.b - tri.a, e2 = tri.c - tri.a;
        const Vec3 p = Cross(ray.direction, e2);
        const float det = Dot(e1, p);
        if (cullBackFaces ? det < eps : Abs(det) < eps) return std::nullopt;
        const float invDet = 1.f / det;
        const Vec3 s = ray.origin - tri.a;
        const float u = Dot(s, p) * invDet;
        if (u < 0.f || u > 1.f) return std::nullopt;
        const Vec3 q = Cross(s, e1);
        const float v = Dot(ray.direction, q) * invDet;
        if (v < 0.f || u + v > 1.f) return std::nullopt;
        const float t = Dot(e2, q) * invDet;
        if (t < 0.f || t > maxDistance) return std::nullopt;
        return TriangleHit{t, u, v};
    }

    bool Overlaps(const Aabb& box, const Sphere& sphere) { return Distance2(ClosestPoint(box, sphere.center), sphere.center) <= sphere.radius * sphere.radius; }

    bool Overlaps(const Obb& a, const Obb& b) {
        // Ericson, Real-Time Collision Detection 4.4.1.
        const Mat3 ra = ToMat3(a.rotation), rb = ToMat3(b.rotation);
        float r[3][3], absR[3][3];
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) {
                r[i][j] = Dot(ra[i], rb[j]);
                absR[i][j] = Abs(r[i][j]) + 1e-6f;   // parallel edges make the cross axes near zero
            }
        const Vec3 d = b.center - a.center;
        const Vec3 t{Dot(d, ra[0]), Dot(d, ra[1]), Dot(d, ra[2])};
        const Vec3& ea = a.halfExtents;
        const Vec3& eb = b.halfExtents;

        for (int i = 0; i < 3; ++i)
            if (Abs(t[i]) > ea[i] + eb[0] * absR[i][0] + eb[1] * absR[i][1] + eb[2] * absR[i][2]) return false;
        for (int j = 0; j < 3; ++j)
            if (Abs(t[0] * r[0][j] + t[1] * r[1][j] + t[2] * r[2][j]) > ea[0] * absR[0][j] + ea[1] * absR[1][j] + ea[2] * absR[2][j] + eb[j]) return false;
        for (int i = 0; i < 3; ++i) {
            const int i1 = (i + 1) % 3, i2 = (i + 2) % 3;
            for (int j = 0; j < 3; ++j) {
                const int j1 = (j + 1) % 3, j2 = (j + 2) % 3;
                const float rA = ea[i1] * absR[i2][j] + ea[i2] * absR[i1][j];
                const float rB = eb[j1] * absR[i][j2] + eb[j2] * absR[i][j1];
                if (Abs(t[i2] * r[i1][j] - t[i1] * r[i2][j]) > rA + rB) return false;
            }
        }
        return true;
    }

    bool Overlaps(const Frustum& frustum, const Aabb& box) { return frustum.Classify(box) != Containment::eOutside; }
    bool Overlaps(const Frustum& frustum, const Sphere& sphere) { return frustum.Classify(sphere) != Containment::eOutside; }

    Vec3 ClosestPoint(const Sphere& sphere, const Vec3& p) {
        const Vec3 d = p - sphere.center;
        const float sq = Dot(d, d);
        if (sq <= sphere.radius * sphere.radius) return p;
        return sphere.center + d * (sphere.radius / std::sqrt(sq));
    }

    Vec3 ClosestPoint(const Obb& box, const Vec3& p) {
        const Vec3 local = Conjugate(box.rotation) * (p - box.center);
        return box.center + box.rotation * Clamp(local, -box.halfExtents, box.halfExtents);
    }

    Vec3 ClosestPoint(const Triangle& tri, const Vec3& p) {
        // Ericson 5.1.5: find the Voronoi region of p.
        const Vec3 &a = tri.a, &b = tri.b, &c = tri.c;
        const Vec3 ab = b - a, ac = c - a, ap = p - a;
        const float d1 = Dot(ab, ap), d2 = Dot(ac, ap);
        if (d1 <= 0.f && d2 <= 0.f) return a;
        const Vec3 bp = p - b;
        const float d3 = Dot(ab, bp), d4 = Dot(ac, bp);
        if (d3 >= 0.f && d4 <= d3) return b;
        const float vc = d1 * d4 - d3 * d2;
        if (vc <= 0.f && d1 >= 0.f && d3 <= 0.f) return a + ab * (d1 / (d1 - d3));
        const Vec3 cp = p - c;
        const float d5 = Dot(ab, cp), d6 = Dot(ac, cp);
        if (d6 >= 0.f && d5 <= d6) return c;
        const float vb = d5 * d2 - d1 * d6;
        if (vb <= 0.f && d2 >= 0.f && d6 <= 0.f) return a + ac * (d2 / (d2 - d6));
        const float va = d3 * d6 - d5 * d4;
        if (va <= 0.f && (d4 - d3) >= 0.f && (d5 - d6) >= 0.f) return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
        const float denom = 1.f / (va + vb + vc);
        return a + ab * (vb * denom) + ac * (vc * denom);
    }

    Vec3 ClosestPointOnSegment(const Vec3& a, const Vec3& b, const Vec3& p) {
        const Vec3 ab = b - a;
        const float sq = Dot(ab, ab);
        if (sq == 0.f) return a;
        return a + ab * Saturate(Dot(p - a, ab) / sq);
    }
}
