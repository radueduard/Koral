#pragma once

// Colour as shaders see it: an RGB(A) Vec3/Vec4 of floats, with conversions between the spaces a game
// deals in, and the packed forms the GPU reads.
//
// Which space a colour is in is the caller's to know. Rule of thumb: what a person picked (a hex code, a
// colour picker, a texture painted by an artist) is sRGB; what lighting adds and multiplies must be linear.
// Convert with SrgbToLinear before shading, and let an *_SRGB render target (or LinearToSrgb) convert back.

#include "vector.h"

#include <bit>

namespace kor {
    /// One sRGB-encoded channel to linear (the exact piecewise curve, not a 2.2 gamma).
    inline float SrgbToLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
    inline float LinearToSrgb(float c) { return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.f / 2.4f) - 0.055f; }
    inline Vec3 SrgbToLinear(const Vec3& c) { return {SrgbToLinear(c.x), SrgbToLinear(c.y), SrgbToLinear(c.z)}; }
    inline Vec3 LinearToSrgb(const Vec3& c) { return {LinearToSrgb(c.x), LinearToSrgb(c.y), LinearToSrgb(c.z)}; }
    /// Alpha is never encoded, so it passes through.
    inline Vec4 SrgbToLinear(const Vec4& c) { return {SrgbToLinear(Vec3(c)), c.w}; }
    inline Vec4 LinearToSrgb(const Vec4& c) { return {LinearToSrgb(Vec3(c)), c.w}; }

    /// 0xRRGGBB as an opaque colour, channels in [0, 1], in whatever space the code was written in (usually sRGB).
    constexpr Vec4 ColorFromHex(u32 rgb) {
        return {float((rgb >> 16) & 0xffu) / 255.f, float((rgb >> 8) & 0xffu) / 255.f, float(rgb & 0xffu) / 255.f, 1.f};
    }
    /// 0xRRGGBBAA.
    constexpr Vec4 ColorFromHexA(u32 rgba) {
        Vec4 c = ColorFromHex(rgba >> 8);
        c.w = float(rgba & 0xffu) / 255.f;
        return c;
    }

    /// Perceived brightness of a *linear* colour (Rec. 709 / sRGB primaries).
    constexpr float Luminance(const Vec3& linear) { return Dot(linear, Vec3(0.2126f, 0.7152f, 0.0722f)); }

    /// RGB to hue, saturation, value; hue in [0, 1) (a full turn), all channels in [0, 1].
    constexpr Vec3 RgbToHsv(const Vec3& c) {
        const float max = MaxComponent(c), min = MinComponent(c), d = max - min;
        float h = 0.f;
        if (d > 0.f) {
            if (max == c.x) h = (c.y - c.z) / d + (c.y < c.z ? 6.f : 0.f);
            else if (max == c.y) h = (c.z - c.x) / d + 2.f;
            else h = (c.x - c.y) / d + 4.f;
            h /= 6.f;
        }
        return {h, max > 0.f ? d / max : 0.f, max};
    }
    inline Vec3 HsvToRgb(const Vec3& hsv) {
        const float h = Fract(hsv.x) * 6.f, s = hsv.y, v = hsv.z;
        const int i = int(h);
        const float f = h - float(i), p = v * (1.f - s), q = v * (1.f - s * f), t = v * (1.f - s * (1.f - f));
        switch (i % 6) {
            case 0: return {v, t, p};
            case 1: return {q, v, p};
            case 2: return {p, v, t};
            case 3: return {p, q, v};
            case 4: return {t, p, v};
            default: return {v, p, q};
        }
    }
    /// RGB to hue, saturation, lightness; hue in [0, 1).
    constexpr Vec3 RgbToHsl(const Vec3& c) {
        const float max = MaxComponent(c), min = MinComponent(c), l = (max + min) * 0.5f, d = max - min;
        if (d == 0.f) return {0.f, 0.f, l};
        const float s = l > 0.5f ? d / (2.f - max - min) : d / (max + min);
        float h;
        if (max == c.x) h = (c.y - c.z) / d + (c.y < c.z ? 6.f : 0.f);
        else if (max == c.y) h = (c.z - c.x) / d + 2.f;
        else h = (c.x - c.y) / d + 4.f;
        return {h / 6.f, s, l};
    }
    inline Vec3 HslToRgb(const Vec3& hsl) {
        const float h = Fract(hsl.x), s = hsl.y, l = hsl.z;
        if (s == 0.f) return Vec3(l);
        const float q = l < 0.5f ? l * (1.f + s) : l + s - l * s, p = 2.f * l - q;
        auto channel = [p, q](float t) {
            t = Fract(t);
            if (t < 1.f / 6.f) return p + (q - p) * 6.f * t;
            if (t < 0.5f) return q;
            if (t < 2.f / 3.f) return p + (q - p) * (2.f / 3.f - t) * 6.f;
            return p;
        };
        return {channel(h + 1.f / 3.f), channel(h), channel(h - 1.f / 3.f)};
    }

    /// Linear sRGB to Oklab (Ottosson 2020): L lightness, a green–red, b blue–yellow. Perceptually even,
    /// so a gradient or blend made there has no muddy middle.
    inline Vec3 LinearToOklab(const Vec3& c) {
        const float l = std::cbrt(0.4122214708f * c.x + 0.5363325363f * c.y + 0.0514459929f * c.z);
        const float m = std::cbrt(0.2119034982f * c.x + 0.6806995451f * c.y + 0.1073969566f * c.z);
        const float s = std::cbrt(0.0883024619f * c.x + 0.2817188376f * c.y + 0.6299787005f * c.z);
        return {0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s,
                1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s,
                0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s};
    }
    inline Vec3 OklabToLinear(const Vec3& lab) {
        const float l = lab.x + 0.3963377774f * lab.y + 0.2158037573f * lab.z;
        const float m = lab.x - 0.1055613458f * lab.y - 0.0638541728f * lab.z;
        const float s = lab.x - 0.0894841775f * lab.y - 1.2914855480f * lab.z;
        const float l3 = l * l * l, m3 = m * m * m, s3 = s * s * s;
        return {4.0767416621f * l3 - 3.3077115913f * m3 + 0.2309699292f * s3,
                -1.2684380046f * l3 + 2.6097574011f * m3 - 0.3413193965f * s3,
                -0.0041960863f * l3 - 0.7034186147f * m3 + 1.7076147010f * s3};
    }
    /// Blends two linear colours through Oklab.
    inline Vec3 MixOklab(const Vec3& a, const Vec3& b, float t) { return OklabToLinear(Lerp(LinearToOklab(a), LinearToOklab(b), t)); }

    /// The linear colour of a black body at `kelvin` (1000–40000 K), normalised so its brightest channel is 1.
    inline Vec3 ColorTemperature(float kelvin) {
        // Tanner Helland's fit, done in sRGB and converted.
        const float t = Clamp(kelvin, 1000.f, 40000.f) / 100.f;
        float r, g, b;
        if (t <= 66.f) {
            r = 255.f;
            g = 99.4708025861f * std::log(t) - 161.1195681661f;
            b = t <= 19.f ? 0.f : 138.5177312231f * std::log(t - 10.f) - 305.0447927307f;
        } else {
            r = 329.698727446f * std::pow(t - 60.f, -0.1332047592f);
            g = 288.1221695283f * std::pow(t - 60.f, -0.0755148492f);
            b = 255.f;
        }
        const Vec3 linear = SrgbToLinear(Clamp(Vec3(r, g, b) / 255.f, 0.f, 1.f));
        return linear / Max(MaxComponent(linear), 1e-6f);
    }

    // ---- packing, as the GPU reads it ------------------------------------------------------------------

    /// Four [0, 1] floats into RGBA8 (R in the lowest byte), rounded to nearest — GLSL's packUnorm4x8.
    constexpr u32 PackUnorm4x8(const Vec4& c) {
        u32 packed = 0;
        for (int i = 0; i < 4; ++i) packed |= u32(Saturate(c[i]) * 255.f + 0.5f) << (8 * i);
        return packed;
    }
    constexpr Vec4 UnpackUnorm4x8(u32 packed) {
        return {float(packed & 0xffu) / 255.f, float((packed >> 8) & 0xffu) / 255.f,
                float((packed >> 16) & 0xffu) / 255.f, float((packed >> 24) & 0xffu) / 255.f};
    }
    constexpr U8Vec4 ToU8Vec4(const Vec4& c) {
        const u32 p = PackUnorm4x8(c);
        return {u8(p), u8(p >> 8), u8(p >> 16), u8(p >> 24)};
    }
    constexpr Vec4 FromU8Vec4(const U8Vec4& c) { return Vec4(c) / 255.f; }

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
    /// Two floats as halves, x in the low 16 bits — GLSL's packHalf2x16.
    constexpr u32 PackHalf2x16(const Vec2& v) { return u32(FloatToHalf(v.x)) | (u32(FloatToHalf(v.y)) << 16); }
    constexpr Vec2 UnpackHalf2x16(u32 packed) { return {HalfToFloat(u16(packed)), HalfToFloat(u16(packed >> 16))}; }
    /// A unit normal in two snorm16s (octahedral mapping): 4 bytes per normal, error under 0.01°.
    constexpr u32 PackOctahedral(const Vec3& n) {
        Vec2 p = Vec2(n) * (1.f / (Abs(n.x) + Abs(n.y) + Abs(n.z)));
        if (n.z < 0.f) p = {(1.f - Abs(p.y)) * (p.x >= 0.f ? 1.f : -1.f), (1.f - Abs(p.x)) * (p.y >= 0.f ? 1.f : -1.f)};
        auto snorm = [](float v) { return u32(u16(i16(Clamp(v, -1.f, 1.f) * 32767.f + (v >= 0.f ? 0.5f : -0.5f)))); };
        return snorm(p.x) | (snorm(p.y) << 16);
    }
    inline Vec3 UnpackOctahedral(u32 packed) {
        const Vec2 p{Max(float(i16(u16(packed))) / 32767.f, -1.f), Max(float(i16(u16(packed >> 16))) / 32767.f, -1.f)};
        Vec3 n{p.x, p.y, 1.f - Abs(p.x) - Abs(p.y)};
        const float t = Max(-n.z, 0.f);
        n.x += n.x >= 0.f ? -t : t;
        n.y += n.y >= 0.f ? -t : t;
        return Normalize(n);
    }
}
