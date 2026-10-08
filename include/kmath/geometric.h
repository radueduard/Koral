#pragma once

// GLSL 8.5, glm's geometric.hpp — and glm's gtx/norm (Length2, Distance2, the norms).

#include "exponential.h"
#include "common.h"

namespace kor {
    template<Scalar T, int N> constexpr T Dot(const Vec<T, N>& a, const Vec<T, N>& b) {
        T sum = a[0] * b[0];
        for (int i = 1; i < N; ++i) sum += a[i] * b[i];
        return sum;
    }
    template<Scalar T> constexpr Vec<T, 3> Cross(const Vec<T, 3>& a, const Vec<T, 3>& b) {
        return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    }
    /// The z of the 3D cross product: positive when b is counter-clockwise from a.
    template<Scalar T> constexpr T Cross(const Vec<T, 2>& a, const Vec<T, 2>& b) { return a.x * b.y - a.y * b.x; }
    template<std::floating_point T, int N> T Length(const Vec<T, N>& v) { return std::sqrt(Dot(v, v)); }
    /// The squared length: Length without its square root.
    template<Scalar T, int N> constexpr T Length2(const Vec<T, N>& v) { return Dot(v, v); }
    template<std::floating_point T, int N> T Distance(const Vec<T, N>& a, const Vec<T, N>& b) { return Length(b - a); }
    template<Scalar T, int N> constexpr T Distance2(const Vec<T, N>& a, const Vec<T, N>& b) { return Length2(b - a); }
    /// v / Length(v). A zero vector stays zero rather than becoming NaN.
    template<std::floating_point T, int N> Vec<T, N> Normalize(const Vec<T, N>& v) {
        const T len = Length(v);
        return len > T(0) ? v * (T(1) / len) : v;
    }
    /// n if Dot(nRef, i) < 0, else -n: a normal turned toward the viewer.
    template<std::floating_point T, int N> constexpr Vec<T, N> FaceForward(const Vec<T, N>& n, const Vec<T, N>& i, const Vec<T, N>& nRef) {
        return Dot(nRef, i) < T(0) ? n : -n;
    }
    /// The mirror of incident direction `i` about the surface with (normalised) normal `n`.
    template<std::floating_point T, int N> constexpr Vec<T, N> Reflect(const Vec<T, N>& i, const Vec<T, N>& n) { return i - n * (T(2) * Dot(n, i)); }
    /// The refraction of `i` through `n` with ratio of indices `eta`; zero on total internal reflection.
    template<std::floating_point T, int N> Vec<T, N> Refract(const Vec<T, N>& i, const Vec<T, N>& n, T eta) {
        const T d = Dot(n, i);
        const T k = T(1) - eta * eta * (T(1) - d * d);
        return k < T(0) ? Vec<T, N>() : i * eta - n * (eta * d + std::sqrt(k));
    }
    /// Σ|v|: the taxicab length.
    template<Scalar T, int N> constexpr T L1Norm(const Vec<T, N>& v) { T s = Abs(v[0]); for (int i = 1; i < N; ++i) s += Abs(v[i]); return s; }
    template<std::floating_point T, int N> T L2Norm(const Vec<T, N>& v) { return Length(v); }
    /// (Σ|v|^p)^(1/p).
    template<std::floating_point T, int N> T LxNorm(const Vec<T, N>& v, T p) {
        T s = T(0);
        for (int i = 0; i < N; ++i) s += std::pow(Abs(v[i]), p);
        return std::pow(s, T(1) / p);
    }
}
