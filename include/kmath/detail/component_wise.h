#pragma once

// Lifting a scalar function to vectors: every function of GLSL's chapters is defined once for a scalar, and
// these make the vector form of it, component by component.

#include "vector_types.h"

/// `Fn(Vec)` from `Fn(scalar)`, for every T the scalar form accepts.
#define KOR_VEC_UNARY(Fn)                                                                                          \
    template<::kor::Scalar T, int N> constexpr auto Fn(const ::kor::Vec<T, N>& v) -> ::kor::Vec<decltype(Fn(T{})), N> { \
        return ::kor::Map([](T c) { return Fn(c); }, v);                                                            \
    }

/// `Fn(Vec, Vec)` and `Fn(Vec, scalar)` from `Fn(scalar, scalar)`.
#define KOR_VEC_BINARY(Fn)                                                                                          \
    template<::kor::Scalar T, int N>                                                                                \
    constexpr auto Fn(const ::kor::Vec<T, N>& a, const ::kor::Vec<T, N>& b) -> ::kor::Vec<decltype(Fn(T{}, T{})), N> { \
        return ::kor::Map([](T l, T r) { return Fn(l, r); }, a, b);                                                  \
    }                                                                                                               \
    template<::kor::Scalar T, int N>                                                                                \
    constexpr auto Fn(const ::kor::Vec<T, N>& a, std::type_identity_t<T> b) -> ::kor::Vec<decltype(Fn(T{}, T{})), N> { \
        return ::kor::Map([b](T l) { return Fn(l, b); }, a);                                                         \
    }
