#pragma once

// kor::Vec<T, N>: 2, 3 and 4 components of any arithmetic type, laid out exactly as a T[N] — no padding
// and no alignment beyond T's — so a Vec3 is the 12 bytes a vertex attribute or a std430 array element
// expects, and the same bytes glm used to give. (std140/std430 *struct members* still align a vec3 to 16:
// that is a property of the shader block, not of this type — pad such members yourself.)
//
// Arithmetic is component-wise, as in GLSL: a * b multiplies x by x, y by y. Every function that makes
// sense per component (Abs, Floor, Min, Clamp, Lerp, Sin, ...) is defined for vectors under the same name
// as its scalar form in kmath/scalar.h. Components default to zero.

#include "scalar.h"

#include <cstddef>
#include <format>
#include <functional>
#include <ostream>
#include <utility>

namespace kor {
    template<Scalar T, int N> struct Vec;

    template<Scalar T>
    struct Vec<T, 2> {
        using value_type = T;
        static constexpr int Size = 2;
        T x{}, y{};

        constexpr Vec() = default;
        constexpr Vec(T x, T y) : x(x), y(y) {}
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
    };

    template<Scalar T>
    struct Vec<T, 3> {
        using value_type = T;
        static constexpr int Size = 3;
        T x{}, y{}, z{};

        constexpr Vec() = default;
        constexpr Vec(T x, T y, T z) : x(x), y(y), z(z) {}
        constexpr explicit Vec(T s) : x(s), y(s), z(s) {}
        constexpr Vec(const Vec<T, 2>& xy, T z) : x(xy.x), y(xy.y), z(z) {}
        constexpr Vec(T x, const Vec<T, 2>& yz) : x(x), y(yz.x), z(yz.y) {}
        template<Scalar U, int M> requires (M >= 3)
        constexpr explicit Vec(const Vec<U, M>& v) : x(T(v.x)), y(T(v.y)), z(T(v.z)) {}

        constexpr T& operator[](int i) { return i == 0 ? x : i == 1 ? y : z; }
        constexpr const T& operator[](int i) const { return i == 0 ? x : i == 1 ? y : z; }
        constexpr T* data() { return &x; }
        constexpr const T* data() const { return &x; }

        constexpr Vec<T, 2> XY() const { return {x, y}; }

        static constexpr Vec Zero() { return Vec(T(0)); }
        static constexpr Vec One() { return Vec(T(1)); }
        static constexpr Vec UnitX() { return {T(1), T(0), T(0)}; }
        static constexpr Vec UnitY() { return {T(0), T(1), T(0)}; }
        static constexpr Vec UnitZ() { return {T(0), T(0), T(1)}; }
        /// Koral's world is right-handed with +Y up, and a camera looks down -Z (as glm::lookAt builds it).
        static constexpr Vec Up() requires std::is_signed_v<T> { return {T(0), T(1), T(0)}; }
        static constexpr Vec Down() requires std::is_signed_v<T> { return {T(0), T(-1), T(0)}; }
        static constexpr Vec Right() requires std::is_signed_v<T> { return {T(1), T(0), T(0)}; }
        static constexpr Vec Left() requires std::is_signed_v<T> { return {T(-1), T(0), T(0)}; }
        static constexpr Vec Forward() requires std::is_signed_v<T> { return {T(0), T(0), T(-1)}; }
        static constexpr Vec Back() requires std::is_signed_v<T> { return {T(0), T(0), T(1)}; }
    };

    template<Scalar T>
    struct Vec<T, 4> {
        using value_type = T;
        static constexpr int Size = 4;
        T x{}, y{}, z{}, w{};

        constexpr Vec() = default;
        constexpr Vec(T x, T y, T z, T w) : x(x), y(y), z(z), w(w) {}
        constexpr explicit Vec(T s) : x(s), y(s), z(s), w(s) {}
        constexpr Vec(const Vec<T, 3>& xyz, T w) : x(xyz.x), y(xyz.y), z(xyz.z), w(w) {}
        constexpr Vec(const Vec<T, 2>& xy, T z, T w) : x(xy.x), y(xy.y), z(z), w(w) {}
        constexpr Vec(const Vec<T, 2>& xy, const Vec<T, 2>& zw) : x(xy.x), y(xy.y), z(zw.x), w(zw.y) {}
        template<Scalar U>
        constexpr explicit Vec(const Vec<U, 4>& v) : x(T(v.x)), y(T(v.y)), z(T(v.z)), w(T(v.w)) {}

        constexpr T& operator[](int i) { return i == 0 ? x : i == 1 ? y : i == 2 ? z : w; }
        constexpr const T& operator[](int i) const { return i == 0 ? x : i == 1 ? y : i == 2 ? z : w; }
        constexpr T* data() { return &x; }
        constexpr const T* data() const { return &x; }

        constexpr Vec<T, 2> XY() const { return {x, y}; }
        constexpr Vec<T, 3> XYZ() const { return {x, y, z}; }

        static constexpr Vec Zero() { return Vec(T(0)); }
        static constexpr Vec One() { return Vec(T(1)); }
        static constexpr Vec UnitX() { return {T(1), T(0), T(0), T(0)}; }
        static constexpr Vec UnitY() { return {T(0), T(1), T(0), T(0)}; }
        static constexpr Vec UnitZ() { return {T(0), T(0), T(1), T(0)}; }
        static constexpr Vec UnitW() { return {T(0), T(0), T(0), T(1)}; }
    };

    using Vec2 = Vec<float, 2>;
    using Vec3 = Vec<float, 3>;
    using Vec4 = Vec<float, 4>;
    using DVec2 = Vec<double, 2>;
    using DVec3 = Vec<double, 3>;
    using DVec4 = Vec<double, 4>;
    using IVec2 = Vec<i32, 2>;
    using IVec3 = Vec<i32, 3>;
    using IVec4 = Vec<i32, 4>;
    using UVec2 = Vec<u32, 2>;
    using UVec3 = Vec<u32, 3>;
    using UVec4 = Vec<u32, 4>;
    using U8Vec4 = Vec<u8, 4>;
    using BVec2 = Vec<bool, 2>;
    using BVec3 = Vec<bool, 3>;
    using BVec4 = Vec<bool, 4>;

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

    template<Scalar T, int N>
    constexpr bool operator==(const Vec<T, N>& a, const Vec<T, N>& b) {
        for (int i = 0; i < N; ++i)
            if (!(a[i] == b[i])) return false;
        return true;
    }

    // ---- component-wise comparisons, giving BVecs ----------------------------------------------------

    template<Scalar T, int N> constexpr Vec<bool, N> LessThan(const Vec<T, N>& a, const Vec<T, N>& b) { return Map([](T l, T r) { return l < r; }, a, b); }
    template<Scalar T, int N> constexpr Vec<bool, N> LessThanEqual(const Vec<T, N>& a, const Vec<T, N>& b) { return Map([](T l, T r) { return l <= r; }, a, b); }
    template<Scalar T, int N> constexpr Vec<bool, N> GreaterThan(const Vec<T, N>& a, const Vec<T, N>& b) { return Map([](T l, T r) { return l > r; }, a, b); }
    template<Scalar T, int N> constexpr Vec<bool, N> GreaterThanEqual(const Vec<T, N>& a, const Vec<T, N>& b) { return Map([](T l, T r) { return l >= r; }, a, b); }
    template<Scalar T, int N> constexpr Vec<bool, N> Equal(const Vec<T, N>& a, const Vec<T, N>& b) { return Map([](T l, T r) { return l == r; }, a, b); }
    template<int N> constexpr bool Any(const Vec<bool, N>& v) { for (int i = 0; i < N; ++i) if (v[i]) return true; return false; }
    template<int N> constexpr bool All(const Vec<bool, N>& v) { for (int i = 0; i < N; ++i) if (!v[i]) return false; return true; }
    template<int N> constexpr Vec<bool, N> Not(const Vec<bool, N>& v) { return Map([](bool b) { return !b; }, v); }
    /// Per component: `ifTrue` where the mask is set, `ifFalse` elsewhere.
    template<Scalar T, int N> constexpr Vec<T, N> Select(const Vec<bool, N>& mask, const Vec<T, N>& ifTrue, const Vec<T, N>& ifFalse) {
        return Map([](bool m, T t, T f) { return m ? t : f; }, mask, ifTrue, ifFalse);
    }

    // ---- geometric -----------------------------------------------------------------------------------

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
    template<std::floating_point T, int N> constexpr T LengthSquared(const Vec<T, N>& v) { return Dot(v, v); }
    template<std::floating_point T, int N> T Length(const Vec<T, N>& v) { return std::sqrt(Dot(v, v)); }
    template<std::floating_point T, int N> T Distance(const Vec<T, N>& a, const Vec<T, N>& b) { return Length(b - a); }
    template<std::floating_point T, int N> constexpr T DistanceSquared(const Vec<T, N>& a, const Vec<T, N>& b) { return LengthSquared(b - a); }
    /// v / Length(v). A zero vector stays zero rather than becoming NaN.
    template<std::floating_point T, int N> Vec<T, N> Normalize(const Vec<T, N>& v) {
        const T len = Length(v);
        return len > T(0) ? v * (T(1) / len) : v;
    }
    /// Normalize, or `fallback` when v is (nearly) zero.
    template<std::floating_point T, int N> Vec<T, N> NormalizeOr(const Vec<T, N>& v, const Vec<T, N>& fallback) {
        const T len = Length(v);
        return len > std::numeric_limits<T>::epsilon() ? v * (T(1) / len) : fallback;
    }
    /// v with its length capped at `maxLength`.
    template<std::floating_point T, int N> Vec<T, N> ClampLength(const Vec<T, N>& v, T maxLength) {
        const T sq = LengthSquared(v);
        return sq > maxLength * maxLength ? v * (maxLength / std::sqrt(sq)) : v;
    }
    /// The mirror of incident direction `i` about the surface with normal `n` (normalised).
    template<std::floating_point T, int N> constexpr Vec<T, N> Reflect(const Vec<T, N>& i, const Vec<T, N>& n) { return i - n * (T(2) * Dot(n, i)); }
    /// The refraction of `i` through normal `n` with ratio of indices `eta`; zero on total internal reflection.
    template<std::floating_point T, int N> Vec<T, N> Refract(const Vec<T, N>& i, const Vec<T, N>& n, T eta) {
        const T d = Dot(n, i);
        const T k = T(1) - eta * eta * (T(1) - d * d);
        return k < T(0) ? Vec<T, N>() : i * eta - n * (eta * d + std::sqrt(k));
    }
    /// The part of `v` along `onto`.
    template<std::floating_point T, int N> constexpr Vec<T, N> Project(const Vec<T, N>& v, const Vec<T, N>& onto) {
        const T sq = Dot(onto, onto);
        return sq > T(0) ? onto * (Dot(v, onto) / sq) : Vec<T, N>();
    }
    /// v without its part along the (normalised) plane normal `n`.
    template<std::floating_point T, int N> constexpr Vec<T, N> ProjectOnPlane(const Vec<T, N>& v, const Vec<T, N>& n) { return v - n * Dot(v, n); }
    /// The unsigned angle between two vectors, in radians.
    template<std::floating_point T, int N> T Angle(const Vec<T, N>& a, const Vec<T, N>& b) {
        const T denom = std::sqrt(LengthSquared(a) * LengthSquared(b));
        return denom > T(0) ? std::acos(Clamp(Dot(a, b) / denom, T(-1), T(1))) : T(0);
    }
    /// The angle from a to b around `axis`, in radians: positive when counter-clockwise looking down the axis.
    template<std::floating_point T> T SignedAngle(const Vec<T, 3>& a, const Vec<T, 3>& b, const Vec<T, 3>& axis) {
        return std::atan2(Dot(Cross(a, b), axis), Dot(a, b));
    }
    /// A unit vector perpendicular to `v` (any one), for building a basis around a direction.
    template<std::floating_point T> Vec<T, 3> AnyPerpendicular(const Vec<T, 3>& v) {
        const Vec<T, 3> other = Abs(v.x) < T(0.9) ? Vec<T, 3>::UnitX() : Vec<T, 3>::UnitY();
        return Normalize(Cross(v, other));
    }
    /// Gram-Schmidt: makes `normal` unit length and `tangent` unit length and perpendicular to it.
    template<std::floating_point T> void OrthoNormalize(Vec<T, 3>& normal, Vec<T, 3>& tangent) {
        normal = Normalize(normal);
        tangent = NormalizeOr(tangent - normal * Dot(tangent, normal), AnyPerpendicular(normal));
    }
    template<std::floating_point T, int N> constexpr bool ApproxEqual(const Vec<T, N>& a, const Vec<T, N>& b, T epsilon = Epsilon<T>) {
        for (int i = 0; i < N; ++i)
            if (!ApproxEqual(a[i], b[i], epsilon)) return false;
        return true;
    }
    template<Scalar T, int N> constexpr T MinComponent(const Vec<T, N>& v) { T m = v[0]; for (int i = 1; i < N; ++i) m = Min(m, v[i]); return m; }
    template<Scalar T, int N> constexpr T MaxComponent(const Vec<T, N>& v) { T m = v[0]; for (int i = 1; i < N; ++i) m = Max(m, v[i]); return m; }
    template<Scalar T, int N> constexpr T Sum(const Vec<T, N>& v) { T s = v[0]; for (int i = 1; i < N; ++i) s += v[i]; return s; }
    template<Scalar T, int N> constexpr T Product(const Vec<T, N>& v) { T s = v[0]; for (int i = 1; i < N; ++i) s *= v[i]; return s; }

    // ---- component-wise versions of the scalar functions ---------------------------------------------

#define KOR_VEC_UNARY(Fn)                                                                                          \
    template<Scalar T, int N> auto Fn(const Vec<T, N>& v) -> Vec<decltype(Fn(T{})), N> {                          \
        return Map([](T c) { return Fn(c); }, v);                                                                  \
    }
    KOR_VEC_UNARY(Abs)
    KOR_VEC_UNARY(Sign)
    KOR_VEC_UNARY(Floor)
    KOR_VEC_UNARY(Ceil)
    KOR_VEC_UNARY(Round)
    KOR_VEC_UNARY(Trunc)
    KOR_VEC_UNARY(Fract)
    KOR_VEC_UNARY(Saturate)
    KOR_VEC_UNARY(Radians)
    KOR_VEC_UNARY(Degrees)
    KOR_VEC_UNARY(Sqrt)
    KOR_VEC_UNARY(InverseSqrt)
    KOR_VEC_UNARY(Exp)
    KOR_VEC_UNARY(Log)
    KOR_VEC_UNARY(Sin)
    KOR_VEC_UNARY(Cos)
    KOR_VEC_UNARY(Tan)
    KOR_VEC_UNARY(Asin)
    KOR_VEC_UNARY(Acos)
    KOR_VEC_UNARY(Atan)
#undef KOR_VEC_UNARY

    template<Scalar T, int N> constexpr Vec<T, N> Min(const Vec<T, N>& a, const Vec<T, N>& b) { return Map([](T l, T r) { return Min(l, r); }, a, b); }
    template<Scalar T, int N> constexpr Vec<T, N> Max(const Vec<T, N>& a, const Vec<T, N>& b) { return Map([](T l, T r) { return Max(l, r); }, a, b); }
    template<Scalar T, int N> constexpr Vec<T, N> Min(const Vec<T, N>& a, std::type_identity_t<T> b) { return Min(a, Vec<T, N>(b)); }
    template<Scalar T, int N> constexpr Vec<T, N> Max(const Vec<T, N>& a, std::type_identity_t<T> b) { return Max(a, Vec<T, N>(b)); }
    template<Scalar T, int N> constexpr Vec<T, N> Clamp(const Vec<T, N>& v, const Vec<T, N>& lo, const Vec<T, N>& hi) { return Min(Max(v, lo), hi); }
    template<Scalar T, int N> constexpr Vec<T, N> Clamp(const Vec<T, N>& v, std::type_identity_t<T> lo, std::type_identity_t<T> hi) { return Clamp(v, Vec<T, N>(lo), Vec<T, N>(hi)); }
    template<std::floating_point T, int N> Vec<T, N> Mod(const Vec<T, N>& v, const Vec<T, N>& m) { return Map([](T a, T b) { return Mod(a, b); }, v, m); }
    template<std::floating_point T, int N> Vec<T, N> Mod(const Vec<T, N>& v, std::type_identity_t<T> m) { return Mod(v, Vec<T, N>(m)); }
    template<std::floating_point T, int N> Vec<T, N> Pow(const Vec<T, N>& v, const Vec<T, N>& e) { return Map([](T a, T b) { return std::pow(a, b); }, v, e); }
    template<std::floating_point T, int N> Vec<T, N> Pow(const Vec<T, N>& v, std::type_identity_t<T> e) { return Pow(v, Vec<T, N>(e)); }
    template<std::floating_point T, int N> Vec<T, N> Atan2(const Vec<T, N>& y, const Vec<T, N>& x) { return Map([](T a, T b) { return std::atan2(a, b); }, y, x); }
    template<std::floating_point T, int N> constexpr Vec<T, N> Lerp(const Vec<T, N>& a, const Vec<T, N>& b, std::type_identity_t<T> t) { return a + (b - a) * t; }
    template<std::floating_point T, int N> constexpr Vec<T, N> Lerp(const Vec<T, N>& a, const Vec<T, N>& b, const Vec<T, N>& t) { return a + (b - a) * t; }
    template<std::floating_point T, int N> constexpr Vec<T, N> Step(const Vec<T, N>& edge, const Vec<T, N>& v) { return Map([](T e, T c) { return Step(e, c); }, edge, v); }
    template<std::floating_point T, int N> constexpr Vec<T, N> SmoothStep(std::type_identity_t<T> e0, std::type_identity_t<T> e1, const Vec<T, N>& v) {
        return Map([e0, e1](T c) { return SmoothStep(e0, e1, c); }, v);
    }
    /// Moves `current` toward `target` by at most `maxDistance` (a straight line, not per component).
    template<std::floating_point T, int N> Vec<T, N> MoveTowards(const Vec<T, N>& current, const Vec<T, N>& target, T maxDistance) {
        const Vec<T, N> d = target - current;
        const T len = Length(d);
        return len <= maxDistance || len == T(0) ? target : current + d * (maxDistance / len);
    }
    /// The vector form of SmoothDamp (kmath/scalar.h), springing all components together.
    template<std::floating_point T, int N>
    Vec<T, N> SmoothDamp(const Vec<T, N>& current, const Vec<T, N>& target, Vec<T, N>& velocity, T smoothTime, T deltaTime,
                         T maxSpeed = std::numeric_limits<T>::infinity()) {
        smoothTime = Max(T(0.0001), smoothTime);
        const T omega = T(2) / smoothTime;
        const T x = omega * deltaTime;
        const T exp = T(1) / (T(1) + x + T(0.48) * x * x + T(0.235) * x * x * x);
        const Vec<T, N> change = ClampLength(current - target, maxSpeed * smoothTime);
        const Vec<T, N> clampedTarget = current - change;
        const Vec<T, N> temp = (velocity + change * omega) * deltaTime;
        velocity = (velocity - temp * omega) * exp;
        Vec<T, N> output = clampedTarget + (change + temp) * exp;
        if (Dot(target - current, output - target) > T(0)) {
            output = target;
            velocity = (output - target) / deltaTime;
        }
        return output;
    }

    template<std::floating_point T, int N> bool IsFinite(const Vec<T, N>& v) { for (int i = 0; i < N; ++i) if (!std::isfinite(v[i])) return false; return true; }

    template<Scalar T, int N>
    std::ostream& operator<<(std::ostream& os, const Vec<T, N>& v) {
        os << '(';
        for (int i = 0; i < N; ++i) os << (i ? ", " : "") << +v[i];
        return os << ')';
    }
}

/// `{}` formats a vector as (x, y, z); a format spec applies to every component: `{:.2f}`.
template<kor::Scalar T, int N, class Char>
struct std::formatter<kor::Vec<T, N>, Char> : std::formatter<std::conditional_t<sizeof(T) == 1 && std::is_integral_v<T>, int, T>, Char> {
    using Element = std::conditional_t<sizeof(T) == 1 && std::is_integral_v<T>, int, T>;
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
