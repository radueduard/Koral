package koral.compose

import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.compositionLocalOf
import koral.ui.interop.KuiLayouts
import koral.ui.interop.KuiNative

// Text, as Jetpack Compose writes it: the same parameters under the same names, and the types they take.
// A weight or a slant its family has no font for is made from the nearest it has (see Fonts.kt).

/** How the lines of a text are placed across its width. */
enum class TextAlign(internal val native: Int) { Left(0), Right(2), Center(1), Justify(0), Start(0), End(2) }

/** How heavy the strokes of a font are: 400 is normal, 700 bold. */
class FontWeight(val weight: Int) : Comparable<FontWeight> {
    override fun compareTo(other: FontWeight) = weight.compareTo(other.weight)
    override fun equals(other: Any?) = other is FontWeight && other.weight == weight
    override fun hashCode() = weight
    companion object {
        val W100 = FontWeight(100); val W200 = FontWeight(200); val W300 = FontWeight(300); val W400 = FontWeight(400); val W500 = FontWeight(500)
        val W600 = FontWeight(600); val W700 = FontWeight(700); val W800 = FontWeight(800); val W900 = FontWeight(900)
        val Thin = W100; val ExtraLight = W200; val Light = W300; val Normal = W400; val Medium = W500
        val SemiBold = W600; val Bold = W700; val ExtraBold = W800; val Black = W900
    }
}

enum class FontStyle { Normal, Italic }

/** A line drawn under or through text. */
class TextDecoration private constructor(val mask: Int) {
    operator fun plus(other: TextDecoration) = TextDecoration(mask or other.mask)
    operator fun contains(other: TextDecoration) = (mask and other.mask) == other.mask
    override fun equals(other: Any?) = other is TextDecoration && other.mask == mask
    override fun hashCode() = mask
    companion object {
        val None = TextDecoration(0); val Underline = TextDecoration(1); val LineThrough = TextDecoration(2)
        fun combine(decorations: List<TextDecoration>) = TextDecoration(decorations.fold(0) { mask, d -> mask or d.mask })
    }
}

/** What becomes of text that does not fit in its lines: cut off, or cut off with an ellipsis to say so. */
enum class TextOverflow { Clip, Ellipsis, Visible }

/** How text looks: what a Text's own parameters say one at a time, as one value — a theme's typography is made of them. */
data class TextStyle(
    val color: Color = Color.Unspecified,
    val fontSize: TextUnit = TextUnit.Unspecified,
    val fontWeight: FontWeight? = null,
    val fontStyle: FontStyle? = null,
    val fontFamily: FontFamily? = null,
    val letterSpacing: TextUnit = TextUnit.Unspecified,
    val textDecoration: TextDecoration? = null,
    val textAlign: TextAlign? = null,
    val lineHeight: TextUnit = TextUnit.Unspecified,
) {
    /** This, with whatever [other] says in place of what this says. */
    fun merge(other: TextStyle?): TextStyle = if (other == null) this else TextStyle(
        if (other.color.isSpecified) other.color else color,
        if (other.fontSize.isSpecified) other.fontSize else fontSize,
        other.fontWeight ?: fontWeight, other.fontStyle ?: fontStyle, other.fontFamily ?: fontFamily,
        if (other.letterSpacing.isSpecified) other.letterSpacing else letterSpacing,
        other.textDecoration ?: textDecoration, other.textAlign ?: textAlign,
        if (other.lineHeight.isSpecified) other.lineHeight else lineHeight)

    operator fun plus(other: TextStyle): TextStyle = merge(other)

    companion object { val Default = TextStyle() }
}

/** The style text has where it is not given one: what [ProvideTextStyle] set round it. */
val LocalTextStyle = compositionLocalOf { TextStyle.Default }

/** [content], with [value] merged into the style its text has. */
@Composable
fun ProvideTextStyle(value: TextStyle, content: @Composable () -> Unit) =
    CompositionLocalProvider(LocalTextStyle provides LocalTextStyle.current.merge(value), content = content)

/**
 * [text], as Compose's Text takes it. Its colour is [color], or [style]'s, or the content colour around it
 * ([LocalContentColor]: a Button's), or the theme's; its size [fontSize], or [style]'s, or the theme's.
 * Its font is [fontFamily]'s, or [style]'s, or the theme's, at [fontWeight] and [fontStyle]. No more than [maxLines]
 * lines of it are shown — with [TextOverflow.Ellipsis], the last ending in one — and it is as tall as [minLines].
 */
@Composable
fun Text(text: String, modifier: Modifier = Modifier, color: Color = Color.Unspecified, fontSize: TextUnit = TextUnit.Unspecified,
         fontStyle: FontStyle? = null, fontWeight: FontWeight? = null, fontFamily: FontFamily? = null,
         letterSpacing: TextUnit = TextUnit.Unspecified, textDecoration: TextDecoration? = null, textAlign: TextAlign? = null,
         lineHeight: TextUnit = TextUnit.Unspecified, overflow: TextOverflow = TextOverflow.Clip, softWrap: Boolean = true,
         maxLines: Int = Int.MAX_VALUE, minLines: Int = 1, style: TextStyle = LocalTextStyle.current) {
    val theme = LocalTheme.current
    val resolved = when {
        color.isSpecified -> color
        style.color.isSpecified -> style.color
        LocalContentColor.current.isSpecified -> LocalContentColor.current
        else -> theme.text
    }
    val size = when {
        fontSize.isSpecified -> fontSize.value
        style.fontSize.isSpecified -> style.fontSize.value
        else -> theme.fontSize.value
    }
    val spacing = when {
        letterSpacing.isSpecified -> letterSpacing.value
        style.letterSpacing.isSpecified -> style.letterSpacing.value
        else -> 0f
    }
    // A line's height is given as a size, as Compose gives it; koral-ui takes it as so many times the text's.
    val height = (if (lineHeight.isSpecified) lineHeight else style.lineHeight).let { if (it.isSpecified && size > 0f) it.value / size else 1.25f }
    val align = (textAlign ?: style.textAlign ?: TextAlign.Start).native
    val weight = fontWeight ?: style.fontWeight ?: FontWeight.Normal
    val slant = fontStyle ?: style.fontStyle ?: FontStyle.Normal
    val decoration = textDecoration ?: style.textDecoration ?: TextDecoration.None
    // The family's nearest font; what it is short of the weight asked for, koral-ui makes up by thickening it.
    val font = (fontFamily ?: style.fontFamily ?: theme.fontFamily)?.resolve(weight, slant)
    val drawnWeight = 400f + (weight.weight - (font?.weight?.weight ?: 400))
    val italic = slant == FontStyle.Italic && font?.style != FontStyle.Italic
    val ellipsis = overflow == TextOverflow.Ellipsis
    val lines = if (maxLines == Int.MAX_VALUE) 0 else maxOf(maxLines, 1)
    // One line is one that is not wrapped: clipped where its box ends, or cut there with an ellipsis.
    val single = lines == 1 || !softWrap
    val wrap = !single
    val limit = if (single) (if (ellipsis) 1 else 0) else lines
    val sized = if (minLines > 1) modifier.heightIn(min = (minLines * size * height).dp) else modifier
    Node(sized, listOf(text, resolved, size, align, wrap, spacing, height, font, drawnWeight, italic, decoration, limit, ellipsis), { _, _ ->
        scratch { a ->
            val native = Struct(a, KuiLayouts.KuiTextStyle).float("size", size).color("color", resolved)
                .float("line_height", height).float("letter_spacing", spacing)
                .address("font", font?.native ?: java.lang.foreign.MemorySegment.NULL).float("weight", drawnWeight).bool("italic", italic)
                .bool("underline", TextDecoration.Underline in decoration).bool("line_through", TextDecoration.LineThrough in decoration).segment
            KuiNative.kui_text_lines(text, native, align, wrap, limit, ellipsis)
        }
    })
}
