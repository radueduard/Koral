#pragma once

// The same operations over many values at once, vectorised (SSE2 on x86-64, NEON on ARM64, plain loops
// elsewhere): transforming a mesh's vertices, a hierarchy's matrices, culling thousands of bounds.
//
// Every result is bit-identical to doing the same thing one value at a time with the functions in the
// other kmath headers (the operations are done in the same order, without fused multiply-adds). Input and
// output may be the same span; otherwise they must not overlap. `out` must be at least as long as the input.

#include "geometry.h"

#include <span>

#include "api.h"

namespace kor::bulk {
    /// out[i] = TransformPoint(m, points[i]).
    KORAL_API void TransformPoints(const Mat4& m, std::span<const Vec3> points, std::span<Vec3> out);
    /// out[i] = TransformDirection(m, directions[i]).
    KORAL_API void TransformDirections(const Mat4& m, std::span<const Vec3> directions, std::span<Vec3> out);
    /// out[i] = m * vectors[i].
    KORAL_API void Transform(const Mat4& m, std::span<const Vec4> vectors, std::span<Vec4> out);
    /// out[i] = a[i] * b[i].
    KORAL_API void Multiply(std::span<const Mat4> a, std::span<const Mat4> b, std::span<Mat4> out);
    /// out[i] = parent * children[i] (placing many children under one parent).
    KORAL_API void Multiply(const Mat4& parent, std::span<const Mat4> children, std::span<Mat4> out);
    /// out[i] = boxes[i].Transformed(m).
    KORAL_API void TransformAabbs(const Mat4& m, std::span<const Aabb> boxes, std::span<Aabb> out);
    /// Aabb::FromPoints, vectorised.
    KORAL_API Aabb Bounds(std::span<const Vec3> points);
    /// visible[i] = Overlaps(frustum, spheres[i]); returns how many are visible.
    KORAL_API std::size_t Cull(const Frustum& frustum, std::span<const Sphere> spheres, std::span<u8> visible);
    /// visible[i] = Overlaps(frustum, boxes[i]); returns how many are visible.
    KORAL_API std::size_t Cull(const Frustum& frustum, std::span<const Aabb> boxes, std::span<u8> visible);
    /// Normalize, in place.
    KORAL_API void Normalize(std::span<Vec3> vectors);
}
