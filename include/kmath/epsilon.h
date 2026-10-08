#pragma once

// glm's ext/scalar_relational, vector_relational: comparing floats within a tolerance.

#include "relational.h"
#include "common.h"
#include "constants.h"

namespace kor {
    /// |a - b| <= epsilon.
    template<std::floating_point T> constexpr bool EpsilonEqual(T a, T b, T epsilon = Epsilon<T>) { return Abs(a - b) <= epsilon; }
    template<std::floating_point T> constexpr bool EpsilonNotEqual(T a, T b, T epsilon = Epsilon<T>) { return Abs(a - b) > epsilon; }
    template<std::floating_point T, int N> constexpr Vec<bool, N> EpsilonEqual(const Vec<T, N>& a, const Vec<T, N>& b, std::type_identity_t<T> epsilon = Epsilon<T>) {
        return Map([epsilon](T l, T r) { return Abs(l - r) <= epsilon; }, a, b);
    }
    template<std::floating_point T, int N> constexpr Vec<bool, N> EpsilonNotEqual(const Vec<T, N>& a, const Vec<T, N>& b, std::type_identity_t<T> epsilon = Epsilon<T>) {
        return Not(EpsilonEqual(a, b, epsilon));
    }
    /// Every component within epsilon: one bool, for checks and tests.
    template<std::floating_point T> constexpr bool ApproxEqual(T a, T b, T epsilon = Epsilon<T>) { return Abs(a - b) <= epsilon; }
    template<std::floating_point T, int N> constexpr bool ApproxEqual(const Vec<T, N>& a, const Vec<T, N>& b, std::type_identity_t<T> epsilon = Epsilon<T>) {
        return All(EpsilonEqual(a, b, epsilon));
    }
}
