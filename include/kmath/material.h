#pragma once

// Google's Material colours: the 2014 palette (Red 50 to 900, the A100–A700 accents), and Material 3's
// colour science — HCT, tonal palettes, and the dynamic colour schemes Android and Compose build from one
// seed colour (or from an image). The palette is data; the rest is Google's material-color-utilities
// (Apache 2.0), compiled into Koral.
//
// Every colour here is sRGB, channels in [0, 1], alpha 1: what ColorFromHex gives, for a UI or for
// SrgbToLinear before lighting.

#include "color.h"

#include "api.h"

#include <span>
#include <vector>

namespace kor {
#include "detail/material_palette.inl"

    /// A palette colour: `shade` 50, 100, 200, ..., 900 (another number gives the nearest of those).
    KORAL_API Vec4 MaterialColor(MaterialHue hue, int shade = 500);
    /**
     * An accent: `shade` 100, 200, 400 or 700 (A100 to A700; another number gives the nearest). Brown, grey
     * and blue grey have no accents: theirs are their plain shades of the same number.
     */
    KORAL_API Vec4 MaterialAccent(MaterialHue hue, int shade = 200);

    /**
     * A colour as Material 3 reasons about it: hue in degrees [0, 360), chroma (colourfulness, 0 for a grey,
     * rarely above 150), and tone (perceived lightness, 0 black to 100 white). Unlike HSV's, a tone means the
     * same lightness whatever the hue, which is what makes contrast predictable.
     */
    struct Hct {
        float hue = 0.f, chroma = 0.f, tone = 0.f;

        KORAL_API static Hct FromColor(const Vec4& srgb);
        /// The nearest displayable colour: an Hct asks for more chroma than sRGB has at some hues and tones.
        KORAL_API Vec4 ToColor() const;
    };

    /// One hue and chroma at every tone: Material 3's palettes, as 0 (black) to 100 (white).
    struct TonalPalette {
        float hue = 0.f, chroma = 0.f;

        /// The palette `color` belongs to.
        KORAL_API static TonalPalette FromColor(const Vec4& srgb);
        KORAL_API Vec4 Tone(float tone) const;
    };

    /// How a scheme spreads a seed colour over its palettes (Material 3's scheme variants).
    enum class SchemeVariant : u8 {
        eTonalSpot,   ///< Android's default: calm, the seed's hue at moderate chroma
        eNeutral,     ///< nearly greyscale
        eVibrant,     ///< the most colourful
        eExpressive,  ///< hues rotated away from the seed's, playful
        eFidelity,    ///< keeps the seed itself as the primary container, as near as it can
        eContent,     ///< like fidelity, for colours taken from content (an image)
        eMonochrome,  ///< greys only
        eRainbow,     ///< a colourful primary over grey surfaces
        eFruitSalad,  ///< hues rotated, colourful
    };

    /**
     * Every colour role of a Material 3 scheme — primary, onPrimary, primaryContainer, surface,
     * surfaceContainerHigh, outline, ... — as Compose's ColorScheme has them, for light or dark.
     */
    struct MaterialScheme {
#include "detail/material_scheme.inl"

        /// The scheme Material 3 makes from `seed`. `contrast` -1 (less) to 1 (most); 0 is the standard.
        KORAL_API static MaterialScheme FromSeed(const Vec4& seed, bool dark, SchemeVariant variant = SchemeVariant::eTonalSpot,
                                                 float contrast = 0.f);
    };

    /**
     * The colours an image suggests as seeds, best first (Android's wallpaper colours): `rgba8` is pixels of four
     * bytes, red first, as an RGBA8 image holds them. Always at least one: Google blue when nothing suits.
     */
    KORAL_API std::vector<Vec4> SeedColors(std::span<const u8> rgba8, int max = 4);

    /// `design` with its hue turned (a little) toward `key`'s: a brand colour that sits well in a scheme.
    KORAL_API Vec4 Harmonize(const Vec4& design, const Vec4& key);
    /// The WCAG contrast ratio of two colours, 1 (none) to 21 (black on white); text wants 4.5 at least.
    KORAL_API float ContrastRatio(const Vec4& a, const Vec4& b);
}
