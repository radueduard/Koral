#pragma once

// GLSL 8.7, glm's vector_relational.hpp: comparisons component by component, giving bool vectors.

#include "detail/vector_types.h"

namespace kor {
    template<Scalar T, int N> constexpr Vec<bool, N> LessThan(const Vec<T, N>& a, const Vec<T, N>& b) { return Map([](T l, T r) { return l < r; }, a, b); }
    template<Scalar T, int N> constexpr Vec<bool, N> LessThanEqual(const Vec<T, N>& a, const Vec<T, N>& b) { return Map([](T l, T r) { return l <= r; }, a, b); }
    template<Scalar T, int N> constexpr Vec<bool, N> GreaterThan(const Vec<T, N>& a, const Vec<T, N>& b) { return Map([](T l, T r) { return l > r; }, a, b); }
    template<Scalar T, int N> constexpr Vec<bool, N> GreaterThanEqual(const Vec<T, N>& a, const Vec<T, N>& b) { return Map([](T l, T r) { return l >= r; }, a, b); }
    template<Scalar T, int N> constexpr Vec<bool, N> Equal(const Vec<T, N>& a, const Vec<T, N>& b) { return Map([](T l, T r) { return l == r; }, a, b); }
    template<Scalar T, int N> constexpr Vec<bool, N> NotEqual(const Vec<T, N>& a, const Vec<T, N>& b) { return Map([](T l, T r) { return l != r; }, a, b); }
    template<int N> constexpr bool Any(const Vec<bool, N>& v) { for (int i = 0; i < N; ++i) if (v[i]) return true; return false; }
    template<int N> constexpr bool All(const Vec<bool, N>& v) { for (int i = 0; i < N; ++i) if (!v[i]) return false; return true; }
    template<int N> constexpr Vec<bool, N> Not(const Vec<bool, N>& v) { return Map([](bool b) { return !b; }, v); }
    /// Per component: `ifTrue` where the mask is set, `ifFalse` elsewhere (GLSL's mix(ifFalse, ifTrue, mask)).
    template<Scalar T, int N> constexpr Vec<T, N> Select(const Vec<bool, N>& mask, const Vec<T, N>& ifTrue, const Vec<T, N>& ifFalse) {
        return Map([](bool m, T t, T f) { return m ? t : f; }, mask, ifTrue, ifFalse);
    }
}
