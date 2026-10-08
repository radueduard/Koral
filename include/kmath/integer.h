#pragma once

// GLSL 8.8, glm's integer.hpp: bit counting and bit fields, and extended arithmetic — plus glm's
// ext/scalar_integer and gtc/round: powers of two and multiples.

#include "common.h"

namespace kor {
    namespace detail {
        template<std::integral T> constexpr auto Unsigned(T v) { return std::make_unsigned_t<T>(v); }
    }

    /// How many bits are set.
    template<std::integral T> constexpr int BitCount(T v) { return std::popcount(detail::Unsigned(v)); }
    /// The lowest set bit's index; -1 for 0.
    template<std::integral T> constexpr int FindLSB(T v) { return v == 0 ? -1 : std::countr_zero(detail::Unsigned(v)); }
    /// The highest set bit's index; -1 for 0. For a negative value: the highest bit that is 0 (as GLSL).
    template<std::integral T> constexpr int FindMSB(T v) {
        if constexpr (std::is_signed_v<T>) if (v < 0) v = T(~v);
        return v == 0 ? -1 : int(sizeof(T) * 8) - 1 - std::countl_zero(detail::Unsigned(v));
    }
    /// `bits` bits of v from bit `offset`, at the bottom of the result — sign-extended for a signed type.
    template<std::integral T> constexpr T BitfieldExtract(T v, int offset, int bits) {
        if (bits <= 0) return T(0);
        constexpr int width = int(sizeof(T) * 8);
        const auto u = detail::Unsigned(v);
        const auto field = bits >= width ? decltype(u)(u >> offset) : decltype(u)((u >> offset) & ((decltype(u)(1) << bits) - 1));
        if constexpr (std::is_signed_v<T>) {
            if (bits < width && (field >> (bits - 1)) & 1u) return T(field | ~((decltype(u)(1) << bits) - 1));   // sign bit set: extend
        }
        return T(field);
    }
    /// base with `bits` bits from bit `offset` replaced by the low bits of `insert`.
    template<std::integral T> constexpr T BitfieldInsert(T base, T insert, int offset, int bits) {
        if (bits <= 0) return base;
        using U = std::make_unsigned_t<T>;
        constexpr int width = int(sizeof(T) * 8);
        const U low = bits >= width ? U(~U(0)) : U((U(1) << bits) - 1);
        const U mask = U(low << offset);
        return T((U(base) & U(~mask)) | ((U(insert) << offset) & mask));
    }
    /// The bits in reverse order.
    template<std::integral T> constexpr T BitfieldReverse(T v) {
        using U = std::make_unsigned_t<T>;
        U u = U(v), r = 0;
        for (std::size_t i = 0; i < sizeof(T) * 8; ++i, u >>= 1) r = U((r << 1) | (u & 1u));
        return T(r);
    }
    /// x + y, and in `carry` whether it overflowed (1 or 0).
    constexpr u32 UaddCarry(u32 x, u32 y, u32& carry) { const u32 s = x + y; carry = s < x ? 1u : 0u; return s; }
    /// x - y, and in `borrow` whether it went below zero (1 or 0).
    constexpr u32 UsubBorrow(u32 x, u32 y, u32& borrow) { borrow = x < y ? 1u : 0u; return x - y; }
    /// The 64-bit product of x and y, as its high and low 32 bits.
    constexpr void UmulExtended(u32 x, u32 y, u32& msb, u32& lsb) { const u64 p = u64(x) * u64(y); msb = u32(p >> 32); lsb = u32(p); }
    constexpr void ImulExtended(i32 x, i32 y, i32& msb, i32& lsb) { const i64 p = i64(x) * i64(y); msb = i32(p >> 32); lsb = i32(u32(u64(p))); }

    KOR_VEC_UNARY(BitCount)
    KOR_VEC_UNARY(FindLSB)
    KOR_VEC_UNARY(FindMSB)
    KOR_VEC_UNARY(BitfieldReverse)
    template<std::integral T, int N> constexpr Vec<T, N> BitfieldExtract(const Vec<T, N>& v, int offset, int bits) {
        return Map([=](T c) { return BitfieldExtract(c, offset, bits); }, v);
    }
    template<std::integral T, int N> constexpr Vec<T, N> BitfieldInsert(const Vec<T, N>& base, const Vec<T, N>& insert, int offset, int bits) {
        return Map([=](T b, T i) { return BitfieldInsert(b, i, offset, bits); }, base, insert);
    }
    template<int N> constexpr Vec<u32, N> UaddCarry(const Vec<u32, N>& x, const Vec<u32, N>& y, Vec<u32, N>& carry) {
        Vec<u32, N> s;
        for (int i = 0; i < N; ++i) s[i] = UaddCarry(x[i], y[i], carry[i]);
        return s;
    }
    template<int N> constexpr Vec<u32, N> UsubBorrow(const Vec<u32, N>& x, const Vec<u32, N>& y, Vec<u32, N>& borrow) {
        Vec<u32, N> d;
        for (int i = 0; i < N; ++i) d[i] = UsubBorrow(x[i], y[i], borrow[i]);
        return d;
    }
    template<int N> constexpr void UmulExtended(const Vec<u32, N>& x, const Vec<u32, N>& y, Vec<u32, N>& msb, Vec<u32, N>& lsb) {
        for (int i = 0; i < N; ++i) UmulExtended(x[i], y[i], msb[i], lsb[i]);
    }
    template<int N> constexpr void ImulExtended(const Vec<i32, N>& x, const Vec<i32, N>& y, Vec<i32, N>& msb, Vec<i32, N>& lsb) {
        for (int i = 0; i < N; ++i) ImulExtended(x[i], y[i], msb[i], lsb[i]);
    }

    // ---- powers of two and multiples (glm's ext/scalar_integer, gtc/round) ------------------------------

    template<std::integral T> constexpr bool IsPowerOfTwo(T v) { return v > 0 && (v & (v - 1)) == 0; }
    /// The smallest power of two >= v (1 for 0 and below).
    template<std::integral T> constexpr T CeilPowerOfTwo(T v) { return v <= 1 ? T(1) : T(std::bit_ceil(detail::Unsigned(v))); }
    /// The largest power of two <= v (0 for 0 and below).
    template<std::integral T> constexpr T FloorPowerOfTwo(T v) { return v <= 0 ? T(0) : T(std::bit_floor(detail::Unsigned(v))); }
    /// The nearest power of two (the larger, halfway between two).
    template<std::integral T> constexpr T RoundPowerOfTwo(T v) {
        const T up = CeilPowerOfTwo(v), down = FloorPowerOfTwo(v);
        return up - v <= v - down ? up : down;
    }
    template<std::integral T> constexpr bool IsMultiple(T v, T multiple) { return multiple != 0 && v % multiple == 0; }
    /// v rounded up (toward +∞) to a multiple of `multiple`.
    template<std::integral T> constexpr T CeilMultiple(T v, T multiple) { const T r = Mod(v, multiple); return r == 0 ? v : v + multiple - r; }
    template<std::integral T> constexpr T FloorMultiple(T v, T multiple) { return v - Mod(v, multiple); }
    template<std::integral T> constexpr T RoundMultiple(T v, T multiple) {
        const T down = FloorMultiple(v, multiple), up = down + multiple;
        return v - down < up - v ? down : up;
    }
    template<std::floating_point T> T CeilMultiple(T v, T multiple) { return std::ceil(v / multiple) * multiple; }
    template<std::floating_point T> T FloorMultiple(T v, T multiple) { return std::floor(v / multiple) * multiple; }
    template<std::floating_point T> T RoundMultiple(T v, T multiple) { return std::round(v / multiple) * multiple; }
    /// v rounded up to a multiple of `alignment`, which must be a power of two.
    template<std::unsigned_integral T> constexpr T AlignUp(T v, T alignment) { return (v + alignment - 1) & ~(alignment - 1); }
    /// a / b rounded up: how many groups of b cover a (dispatch sizes).
    template<std::integral T> constexpr T DivideRoundUp(T a, T b) { return (a + b - 1) / b; }

    KOR_VEC_UNARY(IsPowerOfTwo)
    KOR_VEC_UNARY(CeilPowerOfTwo)
    KOR_VEC_UNARY(FloorPowerOfTwo)
    KOR_VEC_UNARY(RoundPowerOfTwo)
    KOR_VEC_BINARY(IsMultiple)
    KOR_VEC_BINARY(CeilMultiple)
    KOR_VEC_BINARY(FloorMultiple)
    KOR_VEC_BINARY(RoundMultiple)
}
