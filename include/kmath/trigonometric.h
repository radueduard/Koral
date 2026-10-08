#pragma once

// GLSL 8.1, glm's trigonometric.hpp: angles and the trigonometric functions, for scalars and vectors.

#include "constants.h"
#include "detail/component_wise.h"

namespace kor {
    template<std::floating_point T> constexpr T Radians(T degrees) { return degrees * (Pi<T> / T(180)); }
    template<std::floating_point T> constexpr T Degrees(T radians) { return radians * (T(180) / Pi<T>); }
    template<std::floating_point T> T Sin(T v) { return std::sin(v); }
    template<std::floating_point T> T Cos(T v) { return std::cos(v); }
    template<std::floating_point T> T Tan(T v) { return std::tan(v); }
    template<std::floating_point T> T Asin(T v) { return std::asin(v); }
    template<std::floating_point T> T Acos(T v) { return std::acos(v); }
    template<std::floating_point T> T Atan(T v) { return std::atan(v); }
    /// The angle of (x, y): GLSL's atan(y, x).
    template<std::floating_point T> T Atan(T y, T x) { return std::atan2(y, x); }
    template<std::floating_point T> T Atan2(T y, T x) { return std::atan2(y, x); }
    template<std::floating_point T> T Sinh(T v) { return std::sinh(v); }
    template<std::floating_point T> T Cosh(T v) { return std::cosh(v); }
    template<std::floating_point T> T Tanh(T v) { return std::tanh(v); }
    template<std::floating_point T> T Asinh(T v) { return std::asinh(v); }
    template<std::floating_point T> T Acosh(T v) { return std::acosh(v); }
    template<std::floating_point T> T Atanh(T v) { return std::atanh(v); }

    KOR_VEC_UNARY(Radians)
    KOR_VEC_UNARY(Degrees)
    KOR_VEC_UNARY(Sin)
    KOR_VEC_UNARY(Cos)
    KOR_VEC_UNARY(Tan)
    KOR_VEC_UNARY(Asin)
    KOR_VEC_UNARY(Acos)
    KOR_VEC_UNARY(Atan)
    KOR_VEC_BINARY(Atan)
    KOR_VEC_BINARY(Atan2)
    KOR_VEC_UNARY(Sinh)
    KOR_VEC_UNARY(Cosh)
    KOR_VEC_UNARY(Tanh)
    KOR_VEC_UNARY(Asinh)
    KOR_VEC_UNARY(Acosh)
    KOR_VEC_UNARY(Atanh)
}
