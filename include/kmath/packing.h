#pragma once

// GLSL 8.4, glm's packing.hpp: floats packed into integers the way the GPU reads them, and halves.

#include "common.h"

namespace kor {
    /// IEEE half precision, round to nearest even; overflow becomes infinity, NaN stays NaN.
    constexpr u16 FloatToHalf(float value) {
        const u32 bits = std::bit_cast<u32>(value);
        const u32 sign = (bits >> 16) & 0x8000u;
        const u32 exponent = (bits >> 23) & 0xffu;
        u32 mantissa = bits & 0x7fffffu;
        if (exponent == 0xffu) return u16(sign | 0x7c00u | (mantissa ? 0x200u : 0u));
        const int e = int(exponent) - 127 + 15;
        if (e >= 31) return u16(sign | 0x7c00u);
        if (e <= 0) {
            if (e < -10) return u16(sign);
            mantissa |= 0x800000u;
            const u32 shift = u32(14 - e);
            u32 half = mantissa >> shift;
            const u32 rem = mantissa & ((1u << shift) - 1u), halfway = 1u << (shift - 1u);
            if (rem > halfway || (rem == halfway && (half & 1u))) ++half;
            return u16(sign | half);
        }
        u32 half = (u32(e) << 10) | (mantissa >> 13);
        const u32 rem = mantissa & 0x1fffu;
        if (rem > 0x1000u || (rem == 0x1000u && (half & 1u))) ++half;   // may carry into the exponent: correct
        return u16(sign | half);
    }
    constexpr float HalfToFloat(u16 half) {
        const u32 sign = u32(half & 0x8000u) << 16;
        const u32 exponent = (half >> 10) & 0x1fu;
        u32 mantissa = half & 0x3ffu;
        if (exponent == 0) {
            if (mantissa == 0) return std::bit_cast<float>(sign);
            int e = -1;
            do { ++e; mantissa <<= 1; } while ((mantissa & 0x400u) == 0);
            return std::bit_cast<float>(sign | (u32(127 - 15 - e) << 23) | ((mantissa & 0x3ffu) << 13));
        }
        if (exponent == 31) return std::bit_cast<float>(sign | 0x7f800000u | (mantissa << 13));
        return std::bit_cast<float>(sign | ((exponent + 127 - 15) << 23) | (mantissa << 13));
    }

    namespace detail {
        // round(clamp(v, lo, 1) * scale): halves away from zero, as std::round (and glm).
        constexpr i32 RoundAway(float v) { return v < 0.f ? -i32(-v + 0.5f) : i32(v + 0.5f); }
        constexpr u32 Unorm(float v, float scale) { return u32(RoundAway(Clamp(v, 0.f, 1.f) * scale)); }
        constexpr i32 Snorm(float v, float scale) { return RoundAway(Clamp(v, -1.f, 1.f) * scale); }
    }

    /// Four [0, 1] floats into 8 bits each, x in the lowest byte.
    constexpr u32 PackUnorm4x8(const Vec4& v) {
        return detail::Unorm(v.x, 255.f) | (detail::Unorm(v.y, 255.f) << 8) | (detail::Unorm(v.z, 255.f) << 16) | (detail::Unorm(v.w, 255.f) << 24);
    }
    constexpr Vec4 UnpackUnorm4x8(u32 p) {
        return {float(p & 0xffu) / 255.f, float((p >> 8) & 0xffu) / 255.f, float((p >> 16) & 0xffu) / 255.f, float((p >> 24) & 0xffu) / 255.f};
    }
    /// Four [-1, 1] floats into 8 signed bits each.
    constexpr u32 PackSnorm4x8(const Vec4& v) {
        u32 p = 0;
        for (int i = 0; i < 4; ++i) p |= u32(u8(i8(detail::Snorm(v[i], 127.f)))) << (8 * i);
        return p;
    }
    constexpr Vec4 UnpackSnorm4x8(u32 p) {
        Vec4 v;
        for (int i = 0; i < 4; ++i) v[i] = Clamp(float(i8(u8(p >> (8 * i)))) / 127.f, -1.f, 1.f);
        return v;
    }
    /// Two [0, 1] floats into 16 bits each.
    constexpr u32 PackUnorm2x16(const Vec2& v) { return detail::Unorm(v.x, 65535.f) | (detail::Unorm(v.y, 65535.f) << 16); }
    constexpr Vec2 UnpackUnorm2x16(u32 p) { return {float(p & 0xffffu) / 65535.f, float(p >> 16) / 65535.f}; }
    /// Two [-1, 1] floats into 16 signed bits each.
    constexpr u32 PackSnorm2x16(const Vec2& v) {
        return u32(u16(i16(detail::Snorm(v.x, 32767.f)))) | (u32(u16(i16(detail::Snorm(v.y, 32767.f)))) << 16);
    }
    constexpr Vec2 UnpackSnorm2x16(u32 p) {
        return {Clamp(float(i16(u16(p))) / 32767.f, -1.f, 1.f), Clamp(float(i16(u16(p >> 16))) / 32767.f, -1.f, 1.f)};
    }
    /// Two floats as halves, x in the low 16 bits.
    constexpr u32 PackHalf2x16(const Vec2& v) { return u32(FloatToHalf(v.x)) | (u32(FloatToHalf(v.y)) << 16); }
    constexpr Vec2 UnpackHalf2x16(u32 p) { return {HalfToFloat(u16(p)), HalfToFloat(u16(p >> 16))}; }
    /// A double's bits as two uints (low first), and back.
    constexpr double PackDouble2x32(const UVec2& v) { return std::bit_cast<double>(u64(v.x) | (u64(v.y) << 32)); }
    constexpr UVec2 UnpackDouble2x32(double d) { const u64 b = std::bit_cast<u64>(d); return {u32(b), u32(b >> 32)}; }
}
