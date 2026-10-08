// kmath/material.h over Google's material-color-utilities (src/third_party/material-color-utilities).

#include "kmath/material.h"

#include "cpp/blend/blend.h"
#include "cpp/cam/hct.h"
#include "cpp/contrast/contrast.h"
#include "cpp/dynamiccolor/dynamic_scheme.h"
#include "cpp/palettes/tones.h"
#include "cpp/quantize/celebi.h"
#include "cpp/scheme/scheme_content.h"
#include "cpp/scheme/scheme_expressive.h"
#include "cpp/scheme/scheme_fidelity.h"
#include "cpp/scheme/scheme_fruit_salad.h"
#include "cpp/scheme/scheme_monochrome.h"
#include "cpp/scheme/scheme_neutral.h"
#include "cpp/scheme/scheme_rainbow.h"
#include "cpp/scheme/scheme_tonal_spot.h"
#include "cpp/scheme/scheme_vibrant.h"
#include "cpp/score/score.h"
#include "cpp/utils/utils.h"

#include <cstdlib>

namespace mcu = material_color_utilities;

namespace kor {
    namespace {
        /// sRGB [0, 1] to 0xAARRGGBB, opaque, rounded to the nearest 8-bit value.
        mcu::Argb ToArgb(const Vec4& c) {
            const auto byte = [](float v) { return u32(Clamp(v, 0.f, 1.f) * 255.f + 0.5f); };
            return 0xff000000u | (byte(c.x) << 16) | (byte(c.y) << 8) | byte(c.z);
        }
        Vec4 FromArgb(mcu::Argb argb) { return ColorFromHex(argb & 0xffffffu); }

        template<std::size_t N> int Nearest(const int (&shades)[N], int shade) {
            int best = 0;
            for (int i = 1; i < int(N); ++i)
                if (std::abs(shades[i] - shade) < std::abs(shades[best] - shade)) best = i;
            return best;
        }
        constexpr int Shades[] = {50, 100, 200, 300, 400, 500, 600, 700, 800, 900};
        constexpr int AccentShades[] = {100, 200, 400, 700};
    }

    Vec4 MaterialColor(MaterialHue hue, int shade) {
        return ColorFromHex(detail::MaterialShades[int(hue)][Nearest(Shades, shade)]);
    }

    Vec4 MaterialAccent(MaterialHue hue, int shade) {
        const int i = Nearest(AccentShades, shade);
        const u32 accent = detail::MaterialAccents[int(hue)][i];
        return accent ? ColorFromHex(accent) : MaterialColor(hue, AccentShades[i]);
    }

    Hct Hct::FromColor(const Vec4& srgb) {
        const mcu::Hct h(ToArgb(srgb));
        return {float(h.get_hue()), float(h.get_chroma()), float(h.get_tone())};
    }

    Vec4 Hct::ToColor() const { return FromArgb(mcu::Hct(hue, chroma, tone).ToInt()); }

    TonalPalette TonalPalette::FromColor(const Vec4& srgb) {
        const mcu::TonalPalette p(ToArgb(srgb));
        return {float(p.get_hue()), float(p.get_chroma())};
    }

    Vec4 TonalPalette::Tone(float tone) const { return FromArgb(mcu::TonalPalette(hue, chroma).get(tone)); }

    MaterialScheme MaterialScheme::FromSeed(const Vec4& seed, bool dark, SchemeVariant variant, float contrast) {
        const mcu::Hct source(ToArgb(seed));
        const auto fill = [](const mcu::DynamicScheme& d) {
            MaterialScheme s;
#include "material_scheme_fill.inl"
            return s;
        };
        switch (variant) {
            case SchemeVariant::eNeutral: return fill(mcu::SchemeNeutral(source, dark, contrast));
            case SchemeVariant::eVibrant: return fill(mcu::SchemeVibrant(source, dark, contrast));
            case SchemeVariant::eExpressive: return fill(mcu::SchemeExpressive(source, dark, contrast));
            case SchemeVariant::eFidelity: return fill(mcu::SchemeFidelity(source, dark, contrast));
            case SchemeVariant::eContent: return fill(mcu::SchemeContent(source, dark, contrast));
            case SchemeVariant::eMonochrome: return fill(mcu::SchemeMonochrome(source, dark, contrast));
            case SchemeVariant::eRainbow: return fill(mcu::SchemeRainbow(source, dark, contrast));
            case SchemeVariant::eFruitSalad: return fill(mcu::SchemeFruitSalad(source, dark, contrast));
            case SchemeVariant::eTonalSpot: break;
        }
        return fill(mcu::SchemeTonalSpot(source, dark, contrast));
    }

    std::vector<Vec4> SeedColors(std::span<const u8> rgba8, int max) {
        std::vector<mcu::Argb> pixels;
        pixels.reserve(rgba8.size() / 4);
        for (std::size_t i = 0; i + 3 < rgba8.size(); i += 4)
            if (rgba8[i + 3] == 255)   // as Android does: what is see-through is not the image's colour
                pixels.push_back(0xff000000u | (u32(rgba8[i]) << 16) | (u32(rgba8[i + 1]) << 8) | u32(rgba8[i + 2]));
        const auto quantized = mcu::QuantizeCelebi(pixels, 128);
        mcu::ScoreOptions options;
        options.desired = std::size_t(Max(max, 1));
        std::vector<Vec4> seeds;
        for (const mcu::Argb argb : mcu::RankedSuggestions(quantized.color_to_count, options)) seeds.push_back(FromArgb(argb));
        return seeds;
    }

    Vec4 Harmonize(const Vec4& design, const Vec4& key) { return FromArgb(mcu::BlendHarmonize(ToArgb(design), ToArgb(key))); }

    float ContrastRatio(const Vec4& a, const Vec4& b) {
        return float(mcu::RatioOfTones(mcu::LstarFromArgb(ToArgb(a)), mcu::LstarFromArgb(ToArgb(b))));
    }
}
