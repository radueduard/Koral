#pragma once

// Koral's scalar vocabulary: the fixed-width aliases the whole API is written in, the constants, and the
// scalar half of every function that kmath/vector.h also defines component-wise.
//
// Everything here is constexpr where the standard library allows it, and nothing allocates or throws.

#include <cmath>
#include <concepts>
#include <cstdint>
#include <limits>
#include <numbers>
#include <type_traits>

namespace kor {
    using u8 = std::uint8_t;
    using u16 = std::uint16_t;
    using u32 = std::uint32_t;
    using u64 = std::uint64_t;
    using i8 = std::int8_t;
    using i16 = std::int16_t;
    using i32 = std::int32_t;
    using i64 = std::int64_t;
    using f32 = float;
    using f64 = double;

    /// The element types a kor::Vec / Mat may hold.
    template<class T>
    concept Scalar = std::is_arithmetic_v<T>;

    template<std::floating_point T = float> inline constexpr T Pi = std::numbers::pi_v<T>;
    template<std::floating_point T = float> inline constexpr T Tau = T(2) * std::numbers::pi_v<T>;
    template<std::floating_point T = float> inline constexpr T HalfPi = std::numbers::pi_v<T> / T(2);
    /// The tolerance ApproxEqual uses when given none: loose enough for the rounding of a few operations.
    template<std::floating_point T = float> inline constexpr T Epsilon = T(1e-5);

    template<std::floating_point T> constexpr T Radians(T degrees) { return degrees * (Pi<T> / T(180)); }
    template<std::floating_point T> constexpr T Degrees(T radians) { return radians * (T(180) / Pi<T>); }

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

    template<std::floating_point T> constexpr T Floor(T v) { return std::floor(v); }
    template<std::floating_point T> constexpr T Ceil(T v) { return std::ceil(v); }
    /// Halves away from zero, like std::round (glm::round's behaviour too).
    template<std::floating_point T> constexpr T Round(T v) { return std::round(v); }
    template<std::floating_point T> constexpr T Trunc(T v) { return std::trunc(v); }
    /// v - Floor(v): always in [0, 1), negative v included.
    template<std::floating_point T> constexpr T Fract(T v) { return v - std::floor(v); }
    /// The GLSL mod: the result has the sign of `m`, so Mod(-1, 3) is 2 (std::fmod would say -1).
    template<std::floating_point T> constexpr T Mod(T v, T m) { return v - m * std::floor(v / m); }
    /// Integer counterpart of Mod: never negative for a positive `m`.
    template<std::integral T> constexpr T Mod(T v, T m) {
        T r = v % m;
        return (r != 0 && ((r < 0) != (m < 0))) ? r + m : r;
    }

    template<std::floating_point T> T Sqrt(T v) { return std::sqrt(v); }
    template<std::floating_point T> T InverseSqrt(T v) { return T(1) / std::sqrt(v); }
    template<std::floating_point T> T Pow(T v, T e) { return std::pow(v, e); }
    template<std::floating_point T> T Exp(T v) { return std::exp(v); }
    template<std::floating_point T> T Log(T v) { return std::log(v); }
    template<std::floating_point T> T Sin(T v) { return std::sin(v); }
    template<std::floating_point T> T Cos(T v) { return std::cos(v); }
    template<std::floating_point T> T Tan(T v) { return std::tan(v); }
    template<std::floating_point T> T Asin(T v) { return std::asin(v); }
    template<std::floating_point T> T Acos(T v) { return std::acos(v); }
    template<std::floating_point T> T Atan(T v) { return std::atan(v); }
    template<std::floating_point T> T Atan2(T y, T x) { return std::atan2(y, x); }

    template<std::floating_point T> bool IsFinite(T v) { return std::isfinite(v); }
    template<std::floating_point T> bool IsNan(T v) { return std::isnan(v); }

    /// |a - b| <= epsilon.
    template<std::floating_point T> constexpr bool ApproxEqual(T a, T b, T epsilon = Epsilon<T>) { return Abs(a - b) <= epsilon; }

    /// a + (b - a) * t. Not clamped: t outside [0, 1] extrapolates.
    template<std::floating_point T> constexpr T Lerp(T a, T b, T t) { return a + (b - a) * t; }
    /// The t for which Lerp(a, b, t) is v. Not clamped; 0 when a == b.
    template<std::floating_point T> constexpr T InverseLerp(T a, T b, T v) { return a == b ? T(0) : (v - a) / (b - a); }
    /// Maps v from [inMin, inMax] onto [outMin, outMax]. Not clamped.
    template<std::floating_point T> constexpr T Remap(T v, T inMin, T inMax, T outMin, T outMax) {
        return Lerp(outMin, outMax, InverseLerp(inMin, inMax, v));
    }
    /// 0 below edge, 1 from it on.
    template<std::floating_point T> constexpr T Step(T edge, T v) { return v < edge ? T(0) : T(1); }
    /// Hermite 3t² - 2t³ between the edges, clamped.
    template<std::floating_point T> constexpr T SmoothStep(T edge0, T edge1, T v) {
        const T t = Saturate((v - edge0) / (edge1 - edge0));
        return t * t * (T(3) - T(2) * t);
    }
    /// Perlin's 6t⁵ - 15t⁴ + 10t³ between the edges, clamped: flat first *and* second derivative at both ends.
    template<std::floating_point T> constexpr T SmootherStep(T edge0, T edge1, T v) {
        const T t = Saturate((v - edge0) / (edge1 - edge0));
        return t * t * t * (t * (t * T(6) - T(15)) + T(10));
    }
    /// Moves `current` toward `target` by at most `maxDelta`, never past it.
    template<std::floating_point T> constexpr T MoveTowards(T current, T target, T maxDelta) {
        return Abs(target - current) <= maxDelta ? target : current + Sign(target - current) * maxDelta;
    }
    /// The shortest signed difference between two angles in radians, in [-Pi, Pi].
    template<std::floating_point T> T DeltaAngle(T from, T to) {
        T d = Mod(to - from, Tau<T>);
        return d > Pi<T> ? d - Tau<T> : d;
    }
    /// Wraps an angle in radians into [-Pi, Pi).
    template<std::floating_point T> T WrapAngle(T radians) { return Mod(radians + Pi<T>, Tau<T>) - Pi<T>; }

    /**
     * A critically damped spring toward `target`: frame-rate independent, never overshoots. `velocity` is the
     * spring's state, kept by the caller between calls; `smoothTime` is roughly the time to reach the target.
     * (Game Programming Gems 4, 1.10 — the same curve as Unity's SmoothDamp.)
     */
    template<std::floating_point T>
    constexpr T SmoothDamp(T current, T target, T& velocity, T smoothTime, T deltaTime,
                           T maxSpeed = std::numeric_limits<T>::infinity()) {
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
        if ((target - current > T(0)) == (output > target)) {   // overshot: settle on the target
            output = target;
            velocity = (output - target) / deltaTime;
        }
        return output;
    }

    /// Smallest power of two >= v (1 for 0).
    template<std::unsigned_integral T> constexpr T NextPowerOfTwo(T v) {
        if (v <= 1) return 1;
        --v;
        for (unsigned s = 1; s < sizeof(T) * 8; s <<= 1) v |= v >> s;
        return v + 1;
    }
    template<std::unsigned_integral T> constexpr bool IsPowerOfTwo(T v) { return v != 0 && (v & (v - 1)) == 0; }
    /// v rounded up to a multiple of `alignment`, which must be a power of two.
    template<std::unsigned_integral T> constexpr T AlignUp(T v, T alignment) { return (v + alignment - 1) & ~(alignment - 1); }
    /// a / b rounded up: how many groups of `b` cover `a` (dispatch sizes).
    template<std::integral T> constexpr T DivideRoundUp(T a, T b) { return (a + b - 1) / b; }
}
