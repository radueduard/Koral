#pragma once

// glm's gtx/component_wise: a vector folded into one value.

#include "common.h"

namespace kor {
    template<Scalar T, int N> constexpr T CompMin(const Vec<T, N>& v) { T m = v[0]; for (int i = 1; i < N; ++i) m = Min(m, v[i]); return m; }
    template<Scalar T, int N> constexpr T CompMax(const Vec<T, N>& v) { T m = v[0]; for (int i = 1; i < N; ++i) m = Max(m, v[i]); return m; }
    template<Scalar T, int N> constexpr T CompAdd(const Vec<T, N>& v) { T s = v[0]; for (int i = 1; i < N; ++i) s += v[i]; return s; }
    template<Scalar T, int N> constexpr T CompMul(const Vec<T, N>& v) { T s = v[0]; for (int i = 1; i < N; ++i) s *= v[i]; return s; }
}
