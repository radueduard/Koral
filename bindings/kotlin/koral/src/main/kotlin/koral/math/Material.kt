package koral

import koral.interop.KoralMathNative
import java.lang.foreign.Arena
import java.lang.foreign.MemorySegment
import java.lang.foreign.ValueLayout.JAVA_BYTE
import java.lang.foreign.ValueLayout.JAVA_FLOAT

// kmath/material.h: Material 3's colour science (Google's material-color-utilities, in Koral's native code).
// The 2014 palette — MaterialColors.Red500, materialColor(MaterialHue.Teal, 300) — is generated/Material.kt.

private fun Arena.vec4(v: Vec4): MemorySegment = allocateFrom(JAVA_FLOAT, v.x, v.y, v.z, v.w)
private fun MemorySegment.vec4(offset: Long = 0L) =
    Vec4(get(JAVA_FLOAT, offset), get(JAVA_FLOAT, offset + 4), get(JAVA_FLOAT, offset + 8), get(JAVA_FLOAT, offset + 12))

/**
 * kor::Hct: hue in degrees [0, 360), chroma (0 for a grey), tone (perceived lightness, 0 black to 100 white). A tone
 * means the same lightness whatever the hue, which is what makes contrast predictable.
 */
data class Hct(val hue: Float, val chroma: Float, val tone: Float) {
    /** The nearest displayable colour, sRGB. */
    fun toColor(): Vec4 = Arena.ofConfined().use { a ->
        KoralMathNative.koral_hct_to_color(a, a.allocateFrom(JAVA_FLOAT, hue, chroma, tone)).vec4()
    }

    companion object {
        fun fromColor(srgb: Vec4): Hct = Arena.ofConfined().use { a ->
            val s = KoralMathNative.koral_hct_from_color(a, a.vec4(srgb))
            Hct(s.get(JAVA_FLOAT, 0), s.get(JAVA_FLOAT, 4), s.get(JAVA_FLOAT, 8))
        }
    }
}

/** kor::TonalPalette: one hue and chroma at every tone, 0 (black) to 100 (white). */
data class TonalPalette(val hue: Float, val chroma: Float) {
    fun tone(tone: Float): Vec4 = Arena.ofConfined().use { a ->
        KoralMathNative.koral_tonal_palette_tone(a, a.allocateFrom(JAVA_FLOAT, hue, chroma), tone).vec4()
    }

    companion object {
        fun fromColor(srgb: Vec4): TonalPalette = Arena.ofConfined().use { a ->
            val s = KoralMathNative.koral_tonal_palette_from_color(a, a.vec4(srgb))
            TonalPalette(s.get(JAVA_FLOAT, 0), s.get(JAVA_FLOAT, 4))
        }
    }
}

/** The scheme Material 3 makes from [seed]; [contrast] -1 to 1, 0 the standard. */
fun MaterialScheme.Companion.fromSeed(seed: Vec4, dark: Boolean, variant: SchemeVariant = SchemeVariant.TonalSpot,
                                      contrast: Float = 0f): MaterialScheme = Arena.ofConfined().use { a ->
    MaterialScheme.read(KoralMathNative.koral_material_scheme_from_seed(a, a.vec4(seed), dark, variant.ordinal, contrast))
}

/** The colours an image suggests as seeds, best first: [rgba8] is pixels of four bytes, red first. At least one. */
fun seedColors(rgba8: ByteArray, max: Int = 4): List<Vec4> = Arena.ofConfined().use { a ->
    val seeds = a.allocate(16L * maxOf(max, 1))
    val n = KoralMathNative.koral_seed_colors(a.allocateFrom(JAVA_BYTE, *rgba8), (rgba8.size / 4).toLong(), seeds, maxOf(max, 1).toLong())
    List(n.toInt()) { seeds.vec4(it * 16L) }
}

/** [design] with its hue turned a little toward [key]'s. */
fun harmonize(design: Vec4, key: Vec4): Vec4 = Arena.ofConfined().use { a ->
    KoralMathNative.koral_harmonize(a, a.vec4(design), a.vec4(key)).vec4()
}

/** The WCAG contrast ratio, 1 to 21; text wants 4.5 at least. */
fun contrastRatio(a: Vec4, b: Vec4): Float = Arena.ofConfined().use { arena ->
    KoralMathNative.koral_contrast_ratio(arena.vec4(a), arena.vec4(b))
}
