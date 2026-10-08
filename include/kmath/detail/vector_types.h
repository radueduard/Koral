#pragma once

// kor::Vec<T, N>, the type: 2, 3 and 4 components of any arithmetic type, laid out exactly as a T[N] — no
// padding, no alignment beyond T's — so a Vec3 is the 12 bytes a vertex attribute expects. Its arithmetic is
// component-wise, as in GLSL; the functions over it are in the headers by GLSL's chapters (common.h,
// geometric.h, ...), all gathered by kmath/vector.h.

#include "../setup.h"

#include <cstddef>
#include <format>
#include <functional>
#include <ostream>
#include <utility>

namespace kor {
    template<Scalar T, int N> struct Vec;
    template<Scalar T> struct Vec<T, 2>;
    template<Scalar T> struct Vec<T, 3>;
    template<Scalar T> struct Vec<T, 4>;

    template<Scalar T>
    struct Vec<T, 2> {
        using value_type = T;
        static constexpr int Size = 2;
        T x{}, y{};

        constexpr Vec() = default;
        constexpr Vec(T x, T y) : x(x), y(y) {}
        /// From components of other arithmetic types, converted (as glm's constructors do).
        template<Scalar A, Scalar B> requires (!std::is_same_v<A, T> || !std::is_same_v<B, T>)
        constexpr Vec(A x, B y) : x(T(x)), y(T(y)) {}
        constexpr explicit Vec(T s) : x(s), y(s) {}
        /// From a vector of another type, or with more components (which are dropped).
        template<Scalar U, int M> requires (M >= 2)
        constexpr explicit Vec(const Vec<U, M>& v) : x(T(v.x)), y(T(v.y)) {}

        constexpr T& operator[](int i) { return i == 0 ? x : y; }
        constexpr const T& operator[](int i) const { return i == 0 ? x : y; }
        constexpr T* data() { return &x; }
        constexpr const T* data() const { return &x; }

        static constexpr Vec Zero() { return Vec(T(0)); }
        static constexpr Vec One() { return Vec(T(1)); }
        static constexpr Vec UnitX() { return {T(1), T(0)}; }
        static constexpr Vec UnitY() { return {T(0), T(1)}; }

#include "swizzle2.inl"
    };

    template<Scalar T>
    struct Vec<T, 3> {
        using value_type = T;
        static constexpr int Size = 3;
        T x{}, y{}, z{};

        constexpr Vec() = default;
        constexpr Vec(T x, T y, T z) : x(x), y(y), z(z) {}
        template<Scalar A, Scalar B, Scalar C> requires (!std::is_same_v<A, T> || !std::is_same_v<B, T> || !std::is_same_v<C, T>)
        constexpr Vec(A x, B y, C z) : x(T(x)), y(T(y)), z(T(z)) {}
        constexpr explicit Vec(T s) : x(s), y(s), z(s) {}
        constexpr Vec(const Vec<T, 2>& xy, T z) : x(xy.x), y(xy.y), z(z) {}
        constexpr Vec(T x, const Vec<T, 2>& yz) : x(x), y(yz.x), z(yz.y) {}
        template<Scalar U, int M> requires (M >= 3)
        constexpr explicit Vec(const Vec<U, M>& v) : x(T(v.x)), y(T(v.y)), z(T(v.z)) {}

        constexpr T& operator[](int i) { return i == 0 ? x : i == 1 ? y : z; }
        constexpr const T& operator[](int i) const { return i == 0 ? x : i == 1 ? y : z; }
        constexpr T* data() { return &x; }
        constexpr const T* data() const { return &x; }

        static constexpr Vec Zero() { return Vec(T(0)); }
        static constexpr Vec One() { return Vec(T(1)); }
        static constexpr Vec UnitX() { return {T(1), T(0), T(0)}; }
        static constexpr Vec UnitY() { return {T(0), T(1), T(0)}; }
        static constexpr Vec UnitZ() { return {T(0), T(0), T(1)}; }
        /// Koral's world is right-handed with +Y up, and a camera looks down -Z.
        static constexpr Vec Up() requires std::is_signed_v<T> { return {T(0), T(1), T(0)}; }
        static constexpr Vec Down() requires std::is_signed_v<T> { return {T(0), T(-1), T(0)}; }
        static constexpr Vec Right() requires std::is_signed_v<T> { return {T(1), T(0), T(0)}; }
        static constexpr Vec Left() requires std::is_signed_v<T> { return {T(-1), T(0), T(0)}; }
        static constexpr Vec Forward() requires std::is_signed_v<T> { return {T(0), T(0), T(-1)}; }
        static constexpr Vec Back() requires std::is_signed_v<T> { return {T(0), T(0), T(1)}; }

#include "swizzle3.inl"
    };

    template<Scalar T>
    struct Vec<T, 4> {
        using value_type = T;
        static constexpr int Size = 4;
        T x{}, y{}, z{}, w{};

        constexpr Vec() = default;
        constexpr Vec(T x, T y, T z, T w) : x(x), y(y), z(z), w(w) {}
        template<Scalar A, Scalar B, Scalar C, Scalar D> requires (!std::is_same_v<A, T> || !std::is_same_v<B, T> || !std::is_same_v<C, T> || !std::is_same_v<D, T>)
        constexpr Vec(A x, B y, C z, D w) : x(T(x)), y(T(y)), z(T(z)), w(T(w)) {}
        constexpr explicit Vec(T s) : x(s), y(s), z(s), w(s) {}
        constexpr Vec(const Vec<T, 3>& xyz, T w) : x(xyz.x), y(xyz.y), z(xyz.z), w(w) {}
        constexpr Vec(T x, const Vec<T, 3>& yzw) : x(x), y(yzw.x), z(yzw.y), w(yzw.z) {}
        constexpr Vec(const Vec<T, 2>& xy, T z, T w) : x(xy.x), y(xy.y), z(z), w(w) {}
        constexpr Vec(const Vec<T, 2>& xy, const Vec<T, 2>& zw) : x(xy.x), y(xy.y), z(zw.x), w(zw.y) {}
        template<Scalar U>
        constexpr explicit Vec(const Vec<U, 4>& v) : x(T(v.x)), y(T(v.y)), z(T(v.z)), w(T(v.w)) {}

        constexpr T& operator[](int i) { return i == 0 ? x : i == 1 ? y : i == 2 ? z : w; }
        constexpr const T& operator[](int i) const { return i == 0 ? x : i == 1 ? y : i == 2 ? z : w; }
        constexpr T* data() { return &x; }
        constexpr const T* data() const { return &x; }

        static constexpr Vec Zero() { return Vec(T(0)); }
        static constexpr Vec One() { return Vec(T(1)); }
        static constexpr Vec UnitX() { return {T(1), T(0), T(0), T(0)}; }
        static constexpr Vec UnitY() { return {T(0), T(1), T(0), T(0)}; }
        static constexpr Vec UnitZ() { return {T(0), T(0), T(1), T(0)}; }
        static constexpr Vec UnitW() { return {T(0), T(0), T(0), T(1)}; }

#include "swizzle4.inl"
    };

#define KOR_VEC_ALIASES(Prefix, T)          \
    using Prefix##Vec2 = Vec<T, 2>;         \
    using Prefix##Vec3 = Vec<T, 3>;         \
    using Prefix##Vec4 = Vec<T, 4>;
    KOR_VEC_ALIASES(, float)
    KOR_VEC_ALIASES(D, double)
    KOR_VEC_ALIASES(I, i32)
    KOR_VEC_ALIASES(U, u32)
    KOR_VEC_ALIASES(B, bool)
    // glm's sized vectors (ext/vector_int*_sized, vector_uint*_sized).
    KOR_VEC_ALIASES(I8, i8)
    KOR_VEC_ALIASES(I16, i16)
    KOR_VEC_ALIASES(I64, i64)
    KOR_VEC_ALIASES(U8, u8)
    KOR_VEC_ALIASES(U16, u16)
    KOR_VEC_ALIASES(U64, u64)
#undef KOR_VEC_ALIASES

    static_assert(sizeof(Vec3) == 12 && alignof(Vec3) == 4);
    static_assert(sizeof(U8Vec4) == 4);
    static_assert(std::is_trivially_copyable_v<Vec4> && std::is_standard_layout_v<Vec4>);

    namespace detail {
        template<std::size_t I, class F, class... V>
        constexpr auto Component(F& f, const V&... v) { return f(v[int(I)]...); }

        template<class F, class... V>
        constexpr auto MapComponents(F&& f, const V&... v) {
            constexpr int N = (std::remove_cvref_t<V>::Size, ...);
            using R = decltype(f(v[0]...));
            return [&]<std::size_t... I>(std::index_sequence<I...>) {
                return Vec<R, N>{Component<I>(f, v...)...};
            }(std::make_index_sequence<N>{});
        }
        template<class T> struct IsVec : std::false_type {};
        template<class T, int N> struct IsVec<Vec<T, N>> : std::true_type {};
    }

    /// Any kor::Vec.
    template<class V>
    concept AnyVec = detail::IsVec<std::remove_cvref_t<V>>::value;

    /// Applies `f` to each component (of each argument, in step) and makes a vector of the results.
    template<class F, AnyVec... V>
    constexpr auto Map(F&& f, const V&... v) { return detail::MapComponents(std::forward<F>(f), v...); }

    // ---- arithmetic ---------------------------------------------------------------------------------

#define KOR_VEC_BINARY_OP(op)                                                                                     \
    template<Scalar T, int N> constexpr Vec<T, N> operator op(const Vec<T, N>& a, const Vec<T, N>& b) {          \
        return Map([](T l, T r) { return T(l op r); }, a, b);                                                    \
    }                                                                                                            \
    template<Scalar T, int N> constexpr Vec<T, N> operator op(const Vec<T, N>& a, std::type_identity_t<T> s) {   \
        return Map([s](T l) { return T(l op s); }, a);                                                           \
    }                                                                                                            \
    template<Scalar T, int N> constexpr Vec<T, N> operator op(std::type_identity_t<T> s, const Vec<T, N>& b) {   \
        return Map([s](T r) { return T(s op r); }, b);                                                           \
    }                                                                                                            \
    template<Scalar T, int N> constexpr Vec<T, N>& operator op##=(Vec<T, N>& a, const Vec<T, N>& b) {            \
        return a = a op b;                                                                                       \
    }                                                                                                            \
    template<Scalar T, int N> constexpr Vec<T, N>& operator op##=(Vec<T, N>& a, std::type_identity_t<T> s) {     \
        return a = a op s;                                                                                       \
    }

    KOR_VEC_BINARY_OP(+)
    KOR_VEC_BINARY_OP(-)
    KOR_VEC_BINARY_OP(*)
    KOR_VEC_BINARY_OP(/)
#undef KOR_VEC_BINARY_OP

#define KOR_VEC_INTEGER_OP(op)                                                                                     \
    template<std::integral T, int N> constexpr Vec<T, N> operator op(const Vec<T, N>& a, const Vec<T, N>& b) {     \
        return Map([](T l, T r) { return T(l op r); }, a, b);                                                     \
    }                                                                                                              \
    template<std::integral T, int N> constexpr Vec<T, N> operator op(const Vec<T, N>& a, std::type_identity_t<T> s) { \
        return Map([s](T l) { return T(l op s); }, a);                                                             \
    }                                                                                                              \
    template<std::integral T, int N> constexpr Vec<T, N> operator op(std::type_identity_t<T> s, const Vec<T, N>& b) { \
        return Map([s](T r) { return T(s op r); }, b);                                                             \
    }                                                                                                              \
    template<std::integral T, int N> constexpr Vec<T, N>& operator op##=(Vec<T, N>& a, const Vec<T, N>& b) {       \
        return a = a op b;                                                                                         \
    }                                                                                                              \
    template<std::integral T, int N> constexpr Vec<T, N>& operator op##=(Vec<T, N>& a, std::type_identity_t<T> s) { \
        return a = a op s;                                                                                         \
    }

    KOR_VEC_INTEGER_OP(%)
    KOR_VEC_INTEGER_OP(&)
    KOR_VEC_INTEGER_OP(|)
    KOR_VEC_INTEGER_OP(^)
    KOR_VEC_INTEGER_OP(<<)
    KOR_VEC_INTEGER_OP(>>)
#undef KOR_VEC_INTEGER_OP

    template<Scalar T, int N> requires std::is_signed_v<T>
    constexpr Vec<T, N> operator-(const Vec<T, N>& v) { return Map([](T c) { return T(-c); }, v); }
    template<Scalar T, int N>
    constexpr Vec<T, N> operator+(const Vec<T, N>& v) { return v; }
    template<std::integral T, int N>
    constexpr Vec<T, N> operator~(const Vec<T, N>& v) { return Map([](T c) { return T(~c); }, v); }
    /// Component-wise logical operators on bool vectors.
    template<int N> constexpr Vec<bool, N> operator&&(const Vec<bool, N>& a, const Vec<bool, N>& b) { return Map([](bool l, bool r) { return l && r; }, a, b); }
    template<int N> constexpr Vec<bool, N> operator||(const Vec<bool, N>& a, const Vec<bool, N>& b) { return Map([](bool l, bool r) { return l || r; }, a, b); }

    template<Scalar T, int N>
    constexpr bool operator==(const Vec<T, N>& a, const Vec<T, N>& b) {
        for (int i = 0; i < N; ++i)
            if (!(a[i] == b[i])) return false;
        return true;
    }

    template<Scalar T, int N>
    std::ostream& operator<<(std::ostream& os, const Vec<T, N>& v) {
        os << '(';
        for (int i = 0; i < N; ++i) os << (i ? ", " : "") << +v[i];
        return os << ')';
    }
}

/// `{}` formats a vector as (x, y, z); a format spec applies to every component: `{:.2f}`.
template<kor::Scalar T, int N, class Char>
struct std::formatter<kor::Vec<T, N>, Char> : std::formatter<std::conditional_t<sizeof(T) == 1 && std::is_integral_v<T> && !std::is_same_v<T, bool>, int, T>, Char> {
    using Element = std::conditional_t<sizeof(T) == 1 && std::is_integral_v<T> && !std::is_same_v<T, bool>, int, T>;
    template<class Context>
    auto format(const kor::Vec<T, N>& v, Context& ctx) const {
        auto out = ctx.out();
        *out++ = Char('(');
        for (int i = 0; i < N; ++i) {
            if (i) { *out++ = Char(','); *out++ = Char(' '); }
            ctx.advance_to(out);
            out = std::formatter<Element, Char>::format(Element(v[i]), ctx);
        }
        *out++ = Char(')');
        return out;
    }
};

template<kor::Scalar T, int N>
struct std::hash<kor::Vec<T, N>> {
    std::size_t operator()(const kor::Vec<T, N>& v) const noexcept {
        std::size_t h = 0;
        for (int i = 0; i < N; ++i) h ^= std::hash<T>{}(v[i]) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        return h;
    }
};
