#pragma once

// GLSL 8.3, glm's common.hpp: the component-wise basics — and Koral's few additions to them (Saturate,
// SmootherStep, InverseLerp, Remap).

#include "detail/component_wise.h"

namespace kor {
    template<Scalar T> constexpr T Min(T a, T b) { return b < a ? b : a; }
    template<Scalar T> constexpr T Max(T a, T b) { return a < b ? b : a; }
    template<Scalar T> constexpr T Clamp(T v, T lo, T hi) { return Min(Max(v, lo), hi); }
    template<std::floating_point T> constexpr T Saturate(T v) { return Clamp(v, T(0), T(1)); }
    template<Scalar T> constexpr T Abs(T v) {
        if constexpr (std::is_unsigned_v<T>) return v;
        else return v < T(0) ? -v : v;
    }
    /// -1, 0 or 1.
    template<Scalar T> constexpr T Sign(T v) { return T((T(0) < v) - (v < T(0))); }
    template<std::floating_point T> T Floor(T v) { return std::floor(v); }
    template<std::floating_point T> T Ceil(T v) { return std::ceil(v); }
    template<std::floating_point T> T Trunc(T v) { return std::trunc(v); }
    /// Halves away from zero (GLSL leaves halves to the implementation; this is C's round).
    template<std::floating_point T> T Round(T v) { return std::round(v); }
    /// Halves to the even neighbour: 0.5 to 0, 1.5 to 2.
    template<std::floating_point T> T RoundEven(T v) { return std::nearbyint(v); }   // the default rounding mode: to nearest, ties to even
    /// v - Floor(v): in [0, 1), negative v included.
    template<std::floating_point T> T Fract(T v) { return v - std::floor(v); }
    /// GLSL's mod: v - m * floor(v / m), the sign of m (std::fmod would keep v's).
    template<std::floating_point T> constexpr T Mod(T v, T m) { return v - m * std::floor(v / m); }
    /// Mod for integers: never negative for a positive m.
    template<std::integral T> constexpr T Mod(T v, T m) {
        T r = v % m;
        return (r != 0 && ((r < 0) != (m < 0))) ? r + m : r;
    }
    /// The fractional part, the whole part in `whole` (both with v's sign).
    template<std::floating_point T> T Modf(T v, T& whole) { return std::modf(v, &whole); }
    /// a + (b - a) * t: GLSL's mix. Not clamped.
    template<std::floating_point T> constexpr T Mix(T a, T b, T t) { return a + (b - a) * t; }
    /// Mix by its other name.
    template<std::floating_point T> constexpr T Lerp(T a, T b, T t) { return a + (b - a) * t; }
    /// GLSL's mix(a, b, bool): b where `pick`, a elsewhere.
    template<Scalar T> constexpr T Mix(T a, T b, bool pick) { return pick ? b : a; }
    /// 0 below edge, 1 from it on.
    template<std::floating_point T> constexpr T Step(T edge, T v) { return v < edge ? T(0) : T(1); }
    /// Hermite 3t² - 2t³ between the edges, clamped.
    template<std::floating_point T> constexpr T SmoothStep(T edge0, T edge1, T v) {
        const T t = Saturate((v - edge0) / (edge1 - edge0));
        return t * t * (T(3) - T(2) * t);
    }
    /// Perlin's 6t⁵ - 15t⁴ + 10t³: flat first and second derivatives at both edges.
    template<std::floating_point T> constexpr T SmootherStep(T edge0, T edge1, T v) {
        const T t = Saturate((v - edge0) / (edge1 - edge0));
        return t * t * t * (t * (t * T(6) - T(15)) + T(10));
    }
    /// The t for which Mix(a, b, t) is v; 0 when a == b.
    template<std::floating_point T> constexpr T InverseLerp(T a, T b, T v) { return a == b ? T(0) : (v - a) / (b - a); }
    /// v from [inMin, inMax] onto [outMin, outMax], not clamped.
    template<std::floating_point T> constexpr T Remap(T v, T inMin, T inMax, T outMin, T outMax) {
        return Mix(outMin, outMax, InverseLerp(inMin, inMax, v));
    }
    template<std::floating_point T> bool IsNan(T v) { return std::isnan(v); }
    template<std::floating_point T> bool IsInf(T v) { return std::isinf(v); }
    template<std::floating_point T> bool IsFinite(T v) { return std::isfinite(v); }
    /// a * b + c, rounded once.
    template<std::floating_point T> T Fma(T a, T b, T c) { return std::fma(a, b, c); }
    /// v = mantissa * 2^exponent, the mantissa in [0.5, 1).
    template<std::floating_point T> T Frexp(T v, int& exponent) { return std::frexp(v, &exponent); }
    template<std::floating_point T> T Ldexp(T v, int exponent) { return std::ldexp(v, exponent); }
    // Bit reinterpretations: of exactly these types (no conversion first, which would change the bits).
    template<std::same_as<float> T> constexpr i32 FloatBitsToInt(T v) { return std::bit_cast<i32>(v); }
    template<std::same_as<float> T> constexpr u32 FloatBitsToUint(T v) { return std::bit_cast<u32>(v); }
    template<std::same_as<i32> T> constexpr float IntBitsToFloat(T v) { return std::bit_cast<float>(v); }
    template<std::same_as<u32> T> constexpr float UintBitsToFloat(T v) { return std::bit_cast<float>(v); }

    KOR_VEC_BINARY(Min)
    KOR_VEC_BINARY(Max)
    KOR_VEC_UNARY(Abs)
    KOR_VEC_UNARY(Sign)
    KOR_VEC_UNARY(Floor)
    KOR_VEC_UNARY(Ceil)
    KOR_VEC_UNARY(Trunc)
    KOR_VEC_UNARY(Round)
    KOR_VEC_UNARY(RoundEven)
    KOR_VEC_UNARY(Fract)
    KOR_VEC_UNARY(Saturate)
    KOR_VEC_BINARY(Mod)
    KOR_VEC_BINARY(Step)
    KOR_VEC_UNARY(IsNan)
    KOR_VEC_UNARY(IsInf)
    KOR_VEC_UNARY(IsFinite)
    KOR_VEC_UNARY(FloatBitsToInt)
    KOR_VEC_UNARY(FloatBitsToUint)
    KOR_VEC_UNARY(IntBitsToFloat)
    KOR_VEC_UNARY(UintBitsToFloat)

    template<Scalar T, int N> constexpr Vec<T, N> Clamp(const Vec<T, N>& v, const Vec<T, N>& lo, const Vec<T, N>& hi) { return Min(Max(v, lo), hi); }
    template<Scalar T, int N> constexpr Vec<T, N> Clamp(const Vec<T, N>& v, std::type_identity_t<T> lo, std::type_identity_t<T> hi) { return Clamp(v, Vec<T, N>(lo), Vec<T, N>(hi)); }
    template<std::floating_point T, int N> constexpr Vec<T, N> Mix(const Vec<T, N>& a, const Vec<T, N>& b, std::type_identity_t<T> t) { return a + (b - a) * t; }
    template<std::floating_point T, int N> constexpr Vec<T, N> Mix(const Vec<T, N>& a, const Vec<T, N>& b, const Vec<T, N>& t) { return a + (b - a) * t; }
    template<Scalar T, int N> constexpr Vec<T, N> Mix(const Vec<T, N>& a, const Vec<T, N>& b, const Vec<bool, N>& pick) {
        return Map([](T l, T r, bool p) { return p ? r : l; }, a, b, pick);
    }
    template<std::floating_point T, int N> constexpr Vec<T, N> Lerp(const Vec<T, N>& a, const Vec<T, N>& b, std::type_identity_t<T> t) { return a + (b - a) * t; }
    template<std::floating_point T, int N> constexpr Vec<T, N> Lerp(const Vec<T, N>& a, const Vec<T, N>& b, const Vec<T, N>& t) { return a + (b - a) * t; }
    template<std::floating_point T, int N> constexpr Vec<T, N> Step(std::type_identity_t<T> edge, const Vec<T, N>& v) { return Step(Vec<T, N>(edge), v); }
    template<std::floating_point T, int N> constexpr Vec<T, N> SmoothStep(const Vec<T, N>& e0, const Vec<T, N>& e1, const Vec<T, N>& v) {
        return Map([](T a, T b, T c) { return SmoothStep(a, b, c); }, e0, e1, v);
    }
    template<std::floating_point T, int N> constexpr Vec<T, N> SmoothStep(std::type_identity_t<T> e0, std::type_identity_t<T> e1, const Vec<T, N>& v) {
        return Map([e0, e1](T c) { return SmoothStep(e0, e1, c); }, v);
    }
    template<std::floating_point T, int N> constexpr Vec<T, N> SmootherStep(const Vec<T, N>& e0, const Vec<T, N>& e1, const Vec<T, N>& v) {
        return Map([](T a, T b, T c) { return SmootherStep(a, b, c); }, e0, e1, v);
    }
    template<std::floating_point T, int N> constexpr Vec<T, N> SmootherStep(std::type_identity_t<T> e0, std::type_identity_t<T> e1, const Vec<T, N>& v) {
        return Map([e0, e1](T c) { return SmootherStep(e0, e1, c); }, v);
    }
    template<std::floating_point T, int N> Vec<T, N> Fma(const Vec<T, N>& a, const Vec<T, N>& b, const Vec<T, N>& c) {
        return Map([](T x, T y, T z) { return std::fma(x, y, z); }, a, b, c);
    }
    template<std::floating_point T, int N> Vec<T, N> Modf(const Vec<T, N>& v, Vec<T, N>& whole) {
        Vec<T, N> f;
        for (int i = 0; i < N; ++i) f[i] = std::modf(v[i], &whole[i]);
        return f;
    }
    template<std::floating_point T, int N> Vec<T, N> Frexp(const Vec<T, N>& v, Vec<int, N>& exponent) {
        Vec<T, N> m;
        for (int i = 0; i < N; ++i) m[i] = std::frexp(v[i], &exponent[i]);
        return m;
    }
    template<std::floating_point T, int N> Vec<T, N> Ldexp(const Vec<T, N>& v, const Vec<int, N>& exponent) {
        return Map([](T x, int e) { return std::ldexp(x, e); }, v, exponent);
    }
}
