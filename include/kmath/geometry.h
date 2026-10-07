#pragma once

// Shapes and the questions asked of them: does this ray hit that box, is this sphere inside the frustum,
// which point on the triangle is closest. All single precision, in whatever space the caller's shapes share.
//
// Rays report hits as distances along the ray (in units of its direction's length, so normalise the
// direction to get world units); a ray starting inside a solid shape hits it at distance 0.

#include "transform.h"

#include "api.h"

#include <array>
#include <optional>
#include <span>

namespace kor {
    struct Ray {
        Vec3 origin{};
        Vec3 direction{0.f, 0.f, -1.f};

        constexpr Vec3 At(float distance) const { return origin + direction * distance; }
        /// From `from` toward `to`, the direction normalised.
        static Ray Between(const Vec3& from, const Vec3& to) { return {from, Normalize(to - from)}; }
    };

    /// The points p with Dot(normal, p) + distance == 0; `normal` points to the plane's positive side.
    struct Plane {
        Vec3 normal{0.f, 1.f, 0.f};
        float distance = 0.f;

        static Plane FromPointNormal(const Vec3& point, const Vec3& normal) {
            const Vec3 n = Normalize(normal);
            return {n, -Dot(n, point)};
        }
        /// Through three points; the normal follows the right-hand rule a → b → c (counter-clockwise faces it).
        static Plane FromPoints(const Vec3& a, const Vec3& b, const Vec3& c) { return FromPointNormal(a, Cross(b - a, c - a)); }

        /// Positive on the side the normal points to.
        constexpr float SignedDistance(const Vec3& p) const { return Dot(normal, p) + distance; }
        /// The same plane with a unit normal.
        Plane Normalized() const {
            const float len = Length(normal);
            return len > 0.f ? Plane{normal / len, distance / len} : *this;
        }
    };

    struct Sphere {
        Vec3 center{};
        float radius = 0.f;

        constexpr bool Contains(const Vec3& p) const { return DistanceSquared(center, p) <= radius * radius; }
        /// A sphere around all the points (Ritter's: within ~5% of the smallest; empty span gives radius 0).
        KORAL_API static Sphere FromPoints(std::span<const Vec3> points);
    };

    /// An axis-aligned box. The default one is *empty* (min > max), so Expand-ing it by anything gives that thing.
    struct Aabb {
        Vec3 min{std::numeric_limits<float>::infinity()};
        Vec3 max{-std::numeric_limits<float>::infinity()};

        static constexpr Aabb Empty() { return {}; }
        static constexpr Aabb FromCenterExtents(const Vec3& center, const Vec3& halfExtents) { return {center - halfExtents, center + halfExtents}; }
        KORAL_API static Aabb FromPoints(std::span<const Vec3> points);

        /// min <= max on every axis (a single point is valid; Empty() is not).
        constexpr bool Valid() const { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }
        constexpr Vec3 Center() const { return (min + max) * 0.5f; }
        constexpr Vec3 Size() const { return max - min; }
        constexpr Vec3 HalfExtents() const { return (max - min) * 0.5f; }
        constexpr float Volume() const { return Valid() ? Product(Size()) : 0.f; }
        constexpr float SurfaceArea() const {
            if (!Valid()) return 0.f;
            const Vec3 s = Size();
            return 2.f * (s.x * s.y + s.y * s.z + s.z * s.x);
        }
        /// Corner `i` of 8: bit 0 picks max.x, bit 1 max.y, bit 2 max.z.
        constexpr Vec3 Corner(int i) const { return {(i & 1) ? max.x : min.x, (i & 2) ? max.y : min.y, (i & 4) ? max.z : min.z}; }

        constexpr bool Contains(const Vec3& p) const { return All(GreaterThanEqual(p, min)) && All(LessThanEqual(p, max)); }
        constexpr bool Contains(const Aabb& b) const { return All(GreaterThanEqual(b.min, min)) && All(LessThanEqual(b.max, max)); }

        constexpr Aabb& Expand(const Vec3& p) { min = kor::Min(min, p); max = kor::Max(max, p); return *this; }
        constexpr Aabb& Expand(const Aabb& b) { min = kor::Min(min, b.min); max = kor::Max(max, b.max); return *this; }
        /// Grown by `amount` on every side (shrunk for a negative one).
        constexpr Aabb Inflated(float amount) const { return {min - Vec3(amount), max + Vec3(amount)}; }
        /// The box around this box after `m` (Arvo's method: exact for the transformed box's bounds).
        KORAL_API Aabb Transformed(const Mat4& m) const;
    };

    constexpr bool operator==(const Aabb& a, const Aabb& b) { return a.min == b.min && a.max == b.max; }
    constexpr Aabb Union(const Aabb& a, const Aabb& b) { return {Min(a.min, b.min), Max(a.max, b.max)}; }
    /// The overlap (invalid when there is none).
    constexpr Aabb Intersection(const Aabb& a, const Aabb& b) { return {Max(a.min, b.min), Min(a.max, b.max)}; }

    /// An oriented box: a box of `halfExtents` around `center`, turned by `rotation`.
    struct Obb {
        Vec3 center{};
        Vec3 halfExtents{0.5f};
        Quat rotation{};

        /// The box `local` placed by `m` (whose scale goes into the extents; shear is lost).
        KORAL_API static Obb FromAabb(const Aabb& local, const Mat4& m);
        /// The world box around it.
        KORAL_API Aabb Bounds() const;
        KORAL_API bool Contains(const Vec3& p) const;
        /// Corner `i` of 8 (bit 0: +x, bit 1: +y, bit 2: +z).
        Vec3 Corner(int i) const {
            const Vec3 local{(i & 1) ? halfExtents.x : -halfExtents.x, (i & 2) ? halfExtents.y : -halfExtents.y, (i & 4) ? halfExtents.z : -halfExtents.z};
            return center + rotation * local;
        }
    };

    struct Triangle {
        Vec3 a{}, b{}, c{};

        /// Counter-clockwise (a → b → c) faces it; unnormalised its length is twice the area.
        Vec3 Normal() const { return Normalize(Cross(b - a, c - a)); }
        float Area() const { return 0.5f * Length(Cross(b - a, c - a)); }
        constexpr Vec3 Centroid() const { return (a + b + c) * (1.f / 3.f); }
        /// (u, v, w) with p = u*a + v*b + w*c, for p in the triangle's plane.
        KORAL_API Vec3 Barycentric(const Vec3& p) const;
    };

    /// Aabb in 2D: an axis-aligned rectangle by its corners; like Aabb, the default one is empty.
    struct Aabb2 {
        Vec2 min{std::numeric_limits<float>::infinity()};
        Vec2 max{-std::numeric_limits<float>::infinity()};

        static constexpr Aabb2 FromPositionSize(const Vec2& position, const Vec2& size) { return {position, position + size}; }
        constexpr bool Valid() const { return min.x <= max.x && min.y <= max.y; }
        constexpr Vec2 Size() const { return max - min; }
        constexpr Vec2 Center() const { return (min + max) * 0.5f; }
        constexpr float Area() const { return Valid() ? Product(Size()) : 0.f; }
        constexpr bool Contains(const Vec2& p) const { return p.x >= min.x && p.y >= min.y && p.x <= max.x && p.y <= max.y; }
        constexpr bool Contains(const Aabb2& r) const { return r.min.x >= min.x && r.min.y >= min.y && r.max.x <= max.x && r.max.y <= max.y; }
        constexpr Aabb2& Expand(const Vec2& p) { min = kor::Min(min, p); max = kor::Max(max, p); return *this; }
        constexpr Aabb2& Expand(const Aabb2& r) { min = kor::Min(min, r.min); max = kor::Max(max, r.max); return *this; }
        constexpr Aabb2 Inflated(float amount) const { return {min - Vec2(amount), max + Vec2(amount)}; }
    };
    constexpr bool operator==(const Aabb2& a, const Aabb2& b) { return a.min == b.min && a.max == b.max; }
    constexpr Aabb2 Union(const Aabb2& a, const Aabb2& b) { return {Min(a.min, b.min), Max(a.max, b.max)}; }
    constexpr Aabb2 Intersection(const Aabb2& a, const Aabb2& b) { return {Max(a.min, b.min), Min(a.max, b.max)}; }

    /// Where a shape is relative to a volume.
    enum class Containment : u8 { eOutside, eIntersects, eInside };

    /// The volume a camera sees: six inward-facing planes (left, right, bottom, top, near, far).
    struct Frustum {
        enum Side : u8 { eLeft, eRight, eBottom, eTop, eNear, eFar };
        std::array<Plane, 6> planes{};

        /// From a projection * view matrix with Vulkan's 0..1 depth (anything kmath/transform.h builds, Y-flipped or not).
        KORAL_API static Frustum FromMatrix(const Mat4& viewProjection);
        /// The 8 corners of the frustum of `viewProjection`, in world space (Aabb::Corner's numbering, z = near/far).
        KORAL_API static std::array<Vec3, 8> Corners(const Mat4& viewProjection);

        KORAL_API bool Contains(const Vec3& p) const;
        KORAL_API Containment Classify(const Aabb& box) const;
        KORAL_API Containment Classify(const Sphere& sphere) const;
    };

    // ---- ray casts ---------------------------------------------------------------------------------------

    struct TriangleHit {
        float distance;
        /// Barycentric weights of b and c at the hit (a's is 1 - u - v): interpolate vertex data with them.
        float u, v;
    };

    KORAL_API std::optional<float> Raycast(const Ray& ray, const Plane& plane, float maxDistance = std::numeric_limits<float>::infinity());
    KORAL_API std::optional<float> Raycast(const Ray& ray, const Sphere& sphere, float maxDistance = std::numeric_limits<float>::infinity());
    KORAL_API std::optional<float> Raycast(const Ray& ray, const Aabb& box, float maxDistance = std::numeric_limits<float>::infinity());
    KORAL_API std::optional<float> Raycast(const Ray& ray, const Obb& box, float maxDistance = std::numeric_limits<float>::infinity());
    /// Möller–Trumbore. Both faces hit unless `cullBackFaces` (then only counter-clockwise ones, seen from the ray).
    KORAL_API std::optional<TriangleHit> Raycast(const Ray& ray, const Triangle& triangle,
                                                      float maxDistance = std::numeric_limits<float>::infinity(), bool cullBackFaces = false);

    // ---- overlap tests ---------------------------------------------------------------------------------

    constexpr bool Overlaps(const Aabb& a, const Aabb& b) { return All(LessThanEqual(a.min, b.max)) && All(LessThanEqual(b.min, a.max)); }
    constexpr bool Overlaps(const Aabb2& a, const Aabb2& b) { return a.min.x <= b.max.x && b.min.x <= a.max.x && a.min.y <= b.max.y && b.min.y <= a.max.y; }
    constexpr bool Overlaps(const Sphere& a, const Sphere& b) { const float r = a.radius + b.radius; return DistanceSquared(a.center, b.center) <= r * r; }
    KORAL_API bool Overlaps(const Aabb& box, const Sphere& sphere);
    inline bool Overlaps(const Sphere& sphere, const Aabb& box) { return Overlaps(box, sphere); }
    /// Separating-axis test over the 15 candidate axes.
    KORAL_API bool Overlaps(const Obb& a, const Obb& b);
    KORAL_API bool Overlaps(const Frustum& frustum, const Aabb& box);
    KORAL_API bool Overlaps(const Frustum& frustum, const Sphere& sphere);

    // ---- closest points --------------------------------------------------------------------------------

    constexpr Vec3 ClosestPoint(const Aabb& box, const Vec3& p) { return Clamp(p, box.min, box.max); }
    constexpr Vec3 ClosestPoint(const Plane& plane, const Vec3& p) { return p - plane.normal * plane.SignedDistance(p); }
    KORAL_API Vec3 ClosestPoint(const Sphere& sphere, const Vec3& p);
    KORAL_API Vec3 ClosestPoint(const Obb& box, const Vec3& p);
    KORAL_API Vec3 ClosestPoint(const Triangle& triangle, const Vec3& p);
    /// On the segment from `a` to `b`.
    KORAL_API Vec3 ClosestPointOnSegment(const Vec3& a, const Vec3& b, const Vec3& p);
    /// The distance from `p` to the box's surface, or 0 inside it.
    inline float Distance(const Aabb& box, const Vec3& p) { return Distance(ClosestPoint(box, p), p); }
}
