using System.Runtime.InteropServices;
using Koral.Native;

namespace Koral;

// kmath/material.h: Material 3's colour science (Google's material-color-utilities, in Koral's native code).
// The 2014 palette — MaterialColors.Red500, KMath.MaterialColor(MaterialHue.Teal, 300) — is Generated/Material.g.cs.

/// <summary>
/// kor::Hct: hue in degrees [0, 360), chroma (0 for a grey), tone (perceived lightness, 0 black to 100 white).
/// A tone means the same lightness whatever the hue, which is what makes contrast predictable.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public record struct Hct(float Hue, float Chroma, float Tone)
{
    public static Hct FromColor(Vec4 srgb) => KoralMathNative.koral_hct_from_color(srgb);
    /// <summary>The nearest displayable colour, sRGB.</summary>
    public readonly Vec4 ToColor() => KoralMathNative.koral_hct_to_color(this);
}

/// <summary>kor::TonalPalette: one hue and chroma at every tone, 0 (black) to 100 (white).</summary>
[StructLayout(LayoutKind.Sequential)]
public record struct TonalPalette(float Hue, float Chroma)
{
    public static TonalPalette FromColor(Vec4 srgb) => KoralMathNative.koral_tonal_palette_from_color(srgb);
    public readonly Vec4 Tone(float tone) => KoralMathNative.koral_tonal_palette_tone(this, tone);
}

public partial struct MaterialScheme
{
    /// <summary>The scheme Material 3 makes from <paramref name="seed"/>; <paramref name="contrast"/> -1 to 1, 0 the standard.</summary>
    public static MaterialScheme FromSeed(Vec4 seed, bool dark, SchemeVariant variant = SchemeVariant.TonalSpot, float contrast = 0f) =>
        KoralMathNative.koral_material_scheme_from_seed(seed, (byte)(dark ? 1 : 0), (int)variant, contrast);
}

public static partial class KMath
{
    /// <summary>
    /// The colours an image suggests as seeds, best first: <paramref name="rgba8"/> is pixels of four bytes, red
    /// first. At least one: Google blue when nothing suits.
    /// </summary>
    public static unsafe Vec4[] SeedColors(ReadOnlySpan<byte> rgba8, int max = 4)
    {
        var seeds = new Vec4[Math.Max(max, 1)];
        nuint n;
        fixed (byte* p = rgba8)
        fixed (Vec4* s = seeds)
            n = KoralMathNative.koral_seed_colors(p, (nuint)(rgba8.Length / 4), s, (nuint)seeds.Length);
        return seeds[..(int)n];
    }
    /// <summary><paramref name="design"/> with its hue turned a little toward <paramref name="key"/>'s.</summary>
    public static Vec4 Harmonize(Vec4 design, Vec4 key) => KoralMathNative.koral_harmonize(design, key);
    /// <summary>The WCAG contrast ratio, 1 to 21; text wants 4.5 at least.</summary>
    public static float ContrastRatio(Vec4 a, Vec4 b) => KoralMathNative.koral_contrast_ratio(a, b);
}
