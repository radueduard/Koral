#pragma once

// GLSL 8.2, glm's exponential.hpp.

#include "detail/component_wise.h"

namespace kor {
    template<std::floating_point T> T Pow(T v, T e) { return std::pow(v, e); }
    template<std::floating_point T> T Exp(T v) { return std::exp(v); }
    template<std::floating_point T> T Log(T v) { return std::log(v); }
    template<std::floating_point T> T Exp2(T v) { return std::exp2(v); }
    template<std::floating_point T> T Log2(T v) { return std::log2(v); }
    template<std::floating_point T> T Sqrt(T v) { return std::sqrt(v); }
    template<std::floating_point T> T InverseSqrt(T v) { return T(1) / std::sqrt(v); }

    KOR_VEC_BINARY(Pow)
    KOR_VEC_UNARY(Exp)
    KOR_VEC_UNARY(Log)
    KOR_VEC_UNARY(Exp2)
    KOR_VEC_UNARY(Log2)
    KOR_VEC_UNARY(Sqrt)
    KOR_VEC_UNARY(InverseSqrt)
}
