#pragma once

// glm's gtx extensions over vectors: vector_angle, perpendicular, projection, orthonormalize (of two vectors),
// normal, polar_coordinates, rotate_vector (2D) — and Koral's own few (NormalizeOr, ClampLength,
// AnyPerpendicular, MoveTowards, SmoothDamp, DeltaAngle, WrapAngle).

#include "geometric.h"
#include "trigonometric.h"
#include "constants.h"

namespace kor {
    /// The unsigned angle between two vectors, in radians.
    template<std::floating_point T, int N> T Angle(const Vec<T, N>& a, const Vec<T, N>& b) {
        const T denom = std::sqrt(Length2(a) * Length2(b));
        return denom > T(0) ? std::acos(Clamp(Dot(a, b) / denom, T(-1), T(1))) : T(0);
    }
    /// The angle from a to b, positive counter-clockwise.
    template<std::floating_point T> T OrientedAngle(const Vec<T, 2>& a, const Vec<T, 2>& b) { return std::atan2(Cross(a, b), Dot(a, b)); }
    /// The angle from a to b around `axis`: positive when counter-clockwise looking down the axis.
    template<std::floating_point T> T OrientedAngle(const Vec<T, 3>& a, const Vec<T, 3>& b, const Vec<T, 3>& axis) {
        return std::atan2(Dot(Cross(a, b), axis), Dot(a, b));
    }
    /// The part of x along `direction`.
    template<std::floating_point T, int N> constexpr Vec<T, N> Proj(const Vec<T, N>& x, const Vec<T, N>& direction) {
        const T sq = Dot(direction, direction);
        return sq > T(0) ? direction * (Dot(x, direction) / sq) : Vec<T, N>();
    }
    /// The part of x perpendicular to `direction`: x - Proj(x, direction).
    template<std::floating_point T, int N> constexpr Vec<T, N> Perp(const Vec<T, N>& x, const Vec<T, N>& direction) { return x - Proj(x, direction); }
    /// x made perpendicular to (normalised) y, and normalised.
    template<std::floating_point T, int N> Vec<T, N> Orthonormalize(const Vec<T, N>& x, const Vec<T, N>& y) { return Normalize(x - y * Dot(y, x)); }
    /// The unit normal of a triangle, counter-clockwise front.
    template<std::floating_point T> Vec<T, 3> TriangleNormal(const Vec<T, 3>& a, const Vec<T, 3>& b, const Vec<T, 3>& c) {
        return Normalize(Cross(b - a, c - a));
    }
    /// A Euclidean vector as (latitude, longitude, length) — latitude from the XZ plane toward +Y, longitude from +Z toward +X.
    template<std::floating_point T> Vec<T, 3> Polar(const Vec<T, 3>& euclidean) {
        const T len = Length(euclidean);
        const Vec<T, 3> n = euclidean / len;
        return {std::asin(Clamp(n.y, T(-1), T(1))), std::atan2(n.x, n.z), len};
    }
    /// A unit vector from (latitude, longitude).
    template<std::floating_point T> Vec<T, 3> Euclidean(const Vec<T, 2>& polar) {
        const T lat = polar.x, lon = polar.y;
        return {std::cos(lat) * std::sin(lon), std::sin(lat), std::cos(lat) * std::cos(lon)};
    }
    /// v turned `angle` radians counter-clockwise.
    template<std::floating_point T> Vec<T, 2> Rotate(const Vec<T, 2>& v, T angle) {
        const T c = std::cos(angle), s = std::sin(angle);
        return {v.x * c - v.y * s, v.x * s + v.y * c};
    }
    /// v turned about the X axis.
    template<std::floating_point T> Vec<T, 3> RotateX(const Vec<T, 3>& v, T angle) {
        const T c = std::cos(angle), s = std::sin(angle);
        return {v.x, v.y * c - v.z * s, v.y * s + v.z * c};
    }
    template<std::floating_point T> Vec<T, 3> RotateY(const Vec<T, 3>& v, T angle) {
        const T c = std::cos(angle), s = std::sin(angle);
        return {v.x * c + v.z * s, v.y, -v.x * s + v.z * c};
    }
    template<std::floating_point T> Vec<T, 3> RotateZ(const Vec<T, 3>& v, T angle) {
        const T c = std::cos(angle), s = std::sin(angle);
        return {v.x * c - v.y * s, v.x * s + v.y * c, v.z};
    }

    /// Normalize, or `fallback` when v is (nearly) zero.
    template<std::floating_point T, int N> Vec<T, N> NormalizeOr(const Vec<T, N>& v, const Vec<T, N>& fallback) {
        const T len = Length(v);
        return len > std::numeric_limits<T>::epsilon() ? v * (T(1) / len) : fallback;
    }
    /// v with its length capped at `maxLength`.
    template<std::floating_point T, int N> Vec<T, N> ClampLength(const Vec<T, N>& v, T maxLength) {
        const T sq = Length2(v);
        return sq > maxLength * maxLength ? v * (maxLength / std::sqrt(sq)) : v;
    }
    /// A unit vector perpendicular to v (any one), for building a basis around a direction.
    template<std::floating_point T> Vec<T, 3> AnyPerpendicular(const Vec<T, 3>& v) {
        const Vec<T, 3> other = Abs(v.x) < T(0.9) ? Vec<T, 3>::UnitX() : Vec<T, 3>::UnitY();
        return Normalize(Cross(v, other));
    }
    /// Moves `current` toward `target` by at most `maxDelta`, never past it.
    template<std::floating_point T> constexpr T MoveTowards(T current, T target, T maxDelta) {
        return Abs(target - current) <= maxDelta ? target : current + Sign(target - current) * maxDelta;
    }
    /// The vector form: a straight line, not per component.
    template<std::floating_point T, int N> Vec<T, N> MoveTowards(const Vec<T, N>& current, const Vec<T, N>& target, T maxDistance) {
        const Vec<T, N> d = target - current;
        const T len = Length(d);
        return len <= maxDistance || len == T(0) ? target : current + d * (maxDistance / len);
    }
    /// The shortest signed difference between two angles in radians, in [-Pi, Pi].
    template<std::floating_point T> T DeltaAngle(T from, T to) {
        const T d = Mod(to - from, Tau<T>);
        return d > Pi<T> ? d - Tau<T> : d;
    }
    /// An angle wrapped into [-Pi, Pi).
    template<std::floating_point T> T WrapAngle(T radians) { return Mod(radians + Pi<T>, Tau<T>) - Pi<T>; }

    /**
     * A critically damped spring toward `target`: frame-rate independent, never overshoots. `velocity` is the
     * spring's state, kept by the caller between calls (Game Programming Gems 4, 1.10).
     */
    template<std::floating_point T>
    constexpr T SmoothDamp(T current, T target, T& velocity, T smoothTime, T deltaTime, T maxSpeed = std::numeric_limits<T>::infinity()) {
        smoothTime = Max(T(0.0001), smoothTime);
        const T omega = T(2) / smoothTime;
        const T x = omega * deltaTime;
        const T exp = T(1) / (T(1) + x + T(0.48) * x * x + T(0.235) * x * x * x);
        const T maxChange = maxSpeed * smoothTime;
        const T change = Clamp(current - target, -maxChange, maxChange);
        const T clampedTarget = current - change;
        const T temp = (velocity + omega * change) * deltaTime;
        velocity = (velocity - omega * temp) * exp;
        T output = clampedTarget + (change + temp) * exp;
        if ((target - current > T(0)) == (output > target)) {
            output = target;
            velocity = (output - target) / deltaTime;
        }
        return output;
    }
    template<std::floating_point T, int N>
    Vec<T, N> SmoothDamp(const Vec<T, N>& current, const Vec<T, N>& target, Vec<T, N>& velocity, T smoothTime, T deltaTime,
                         T maxSpeed = std::numeric_limits<T>::infinity()) {
        smoothTime = Max(T(0.0001), smoothTime);
        const T omega = T(2) / smoothTime;
        const T x = omega * deltaTime;
        const T exp = T(1) / (T(1) + x + T(0.48) * x * x + T(0.235) * x * x * x);
        const Vec<T, N> change = ClampLength(current - target, maxSpeed * smoothTime);
        const Vec<T, N> clampedTarget = current - change;
        const Vec<T, N> temp = (velocity + change * omega) * deltaTime;
        velocity = (velocity - temp * omega) * exp;
        Vec<T, N> output = clampedTarget + (change + temp) * exp;
        if (Dot(target - current, output - target) > T(0)) {
            output = target;
            velocity = (output - target) / deltaTime;
        }
        return output;
    }
}
