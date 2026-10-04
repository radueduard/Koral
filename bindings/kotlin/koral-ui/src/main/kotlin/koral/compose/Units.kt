package koral.compose

import kotlin.math.max
import kotlin.math.min

// The values a Compose interface is written in, named as Jetpack Compose names them. A Dp is one of the
// UI's logical units (the Ui's scale turns them into pixels); so is an Sp.

@JvmInline
value class Dp(val value: Float) : Comparable<Dp> {
    operator fun plus(o: Dp) = Dp(value + o.value)
    operator fun minus(o: Dp) = Dp(value - o.value)
    operator fun times(s: Float) = Dp(value * s)
    operator fun div(s: Float) = Dp(value / s)
    override fun compareTo(other: Dp) = value.compareTo(other.value)
    companion object {
        val Unspecified = Dp(Float.NaN)
        val Hairline = Dp(0f)
    }
}

val Dp.isSpecified: Boolean get() = !value.isNaN()
val Dp.isUnspecified: Boolean get() = value.isNaN()
val TextUnit.isSpecified: Boolean get() = !value.isNaN()
val TextUnit.isUnspecified: Boolean get() = value.isNaN()

/** A size in whole pixels: what onSizeChanged tells. */
data class IntSize(val width: Int, val height: Int) {
    companion object { val Zero = IntSize(0, 0) }
}

/** A place in whole pixels. */
data class IntOffset(val x: Int, val y: Int) {
    companion object { val Zero = IntOffset(0, 0) }
}

/** A size in the interface's units. */
data class DpSize(val width: Dp, val height: Dp) {
    companion object { val Zero = DpSize(Dp(0f), Dp(0f)); val Unspecified = DpSize(Dp.Unspecified, Dp.Unspecified) }
}

/** A place in the interface's units. */
data class DpOffset(val x: Dp, val y: Dp) {
    companion object { val Zero = DpOffset(Dp(0f), Dp(0f)) }
}

val Int.dp: Dp get() = Dp(toFloat())
val Float.dp: Dp get() = Dp(this)
val Double.dp: Dp get() = Dp(toFloat())

@JvmInline
value class TextUnit(val value: Float) : Comparable<TextUnit> {
    operator fun plus(o: TextUnit) = TextUnit(value + o.value)
    operator fun minus(o: TextUnit) = TextUnit(value - o.value)
    operator fun times(s: Float) = TextUnit(value * s)
    operator fun div(s: Float) = TextUnit(value / s)
    override fun compareTo(other: TextUnit) = value.compareTo(other.value)
    companion object { val Unspecified = TextUnit(Float.NaN) }
}

val Int.sp: TextUnit get() = TextUnit(toFloat())
val Float.sp: TextUnit get() = TextUnit(this)
val Double.sp: TextUnit get() = TextUnit(toFloat())

/** A colour, as Compose writes one: `Color(0xFF3F51B5)` is ARGB. Straight alpha, sRGB. */
@JvmInline
value class Color(val argb: Long) {
    val alpha: Float get() = ((argb shr 24) and 0xff) / 255f
    val red: Float get() = ((argb shr 16) and 0xff) / 255f
    val green: Float get() = ((argb shr 8) and 0xff) / 255f
    val blue: Float get() = (argb and 0xff) / 255f
    val isSpecified: Boolean get() = argb != Unspecified.argb

    fun copy(alpha: Float = this.alpha, red: Float = this.red, green: Float = this.green, blue: Float = this.blue) = Color(red, green, blue, alpha)

    companion object {
        val Black = Color(0xFF000000)
        val DarkGray = Color(0xFF444444)
        val Gray = Color(0xFF888888)
        val LightGray = Color(0xFFCCCCCC)
        val White = Color(0xFFFFFFFF)
        val Red = Color(0xFFFF0000)
        val Green = Color(0xFF00FF00)
        val Blue = Color(0xFF0000FF)
        val Yellow = Color(0xFFFFFF00)
        val Cyan = Color(0xFF00FFFF)
        val Magenta = Color(0xFFFF00FF)
        val Transparent = Color(0x00000000)
        /** Not given: whatever the context supplies (a Text's is LocalContentColor's). */
        val Unspecified = Color(0x10000000000L)
    }
}

fun Color(red: Float, green: Float, blue: Float, alpha: Float = 1f): Color {
    fun q(v: Float) = (v.coerceIn(0f, 1f) * 255f + 0.5f).toLong()
    return Color((q(alpha) shl 24) or (q(red) shl 16) or (q(green) shl 8) or q(blue))
}

fun Color(argb: Int): Color = Color(argb.toLong() and 0xFFFFFFFFL)

data class Offset(val x: Float, val y: Float) {
    operator fun plus(o: Offset) = Offset(x + o.x, y + o.y)
    operator fun minus(o: Offset) = Offset(x - o.x, y - o.y)
    operator fun times(s: Float) = Offset(x * s, y * s)
    companion object { val Zero = Offset(0f, 0f) }
}

data class Size(val width: Float, val height: Float) {
    val minDimension: Float get() = min(width, height)
    val maxDimension: Float get() = max(width, height)
    val center: Offset get() = Offset(width / 2f, height / 2f)
    companion object { val Zero = Size(0f, 0f) }
}

data class CornerRadius(val x: Float, val y: Float = x) {
    companion object { val Zero = CornerRadius(0f) }
}

/** Where something goes in a box: (-1, -1) is the top-start, (1, 1) the bottom-end. */
data class Alignment(val horizontal: Float, val vertical: Float) {
    /** Along a Column's width. */
    data class Horizontal(val bias: Float)
    /** Along a Row's height. */
    data class Vertical(val bias: Float)

    companion object {
        val TopStart = Alignment(-1f, -1f)
        val TopCenter = Alignment(0f, -1f)
        val TopEnd = Alignment(1f, -1f)
        val CenterStart = Alignment(-1f, 0f)
        val Center = Alignment(0f, 0f)
        val CenterEnd = Alignment(1f, 0f)
        val BottomStart = Alignment(-1f, 1f)
        val BottomCenter = Alignment(0f, 1f)
        val BottomEnd = Alignment(1f, 1f)

        val Start = Horizontal(-1f)
        val CenterHorizontally = Horizontal(0f)
        val End = Horizontal(1f)

        val Top = Vertical(-1f)
        val CenterVertically = Vertical(0f)
        val Bottom = Vertical(1f)
    }
}

/** How a Row or Column spreads its children along itself. */
object Arrangement {
    interface Horizontal { val kind: Int; val spacing: Dp }
    interface Vertical { val kind: Int; val spacing: Dp }
    /** Both: Center, SpaceBetween, spacedBy... */
    interface HorizontalOrVertical : Horizontal, Vertical

    // The kinds are kui::MainAxisAlignment's values.
    private data class Fixed(override val kind: Int, override val spacing: Dp = 0.dp) : HorizontalOrVertical

    val Start: Horizontal = Fixed(0)
    val End: Horizontal = Fixed(1)
    val Top: Vertical = Fixed(0)
    val Bottom: Vertical = Fixed(1)
    val Center: HorizontalOrVertical = Fixed(2)
    val SpaceBetween: HorizontalOrVertical = Fixed(3)
    val SpaceAround: HorizontalOrVertical = Fixed(4)
    val SpaceEvenly: HorizontalOrVertical = Fixed(5)

    /** [space] between neighbouring children, from the start. */
    fun spacedBy(space: Dp): HorizontalOrVertical = Fixed(0, space)
    fun spacedBy(space: Dp, alignment: Alignment.Horizontal): Horizontal = Fixed(if (alignment.bias < 0) 0 else if (alignment.bias > 0) 1 else 2, space)
    fun spacedBy(space: Dp, alignment: Alignment.Vertical): Vertical = Fixed(if (alignment.bias < 0) 0 else if (alignment.bias > 0) 1 else 2, space)
}

/** The outline a background, border or clip follows. */
sealed interface Shape

data class RoundedCornerShape(val topStart: Dp, val topEnd: Dp = topStart, val bottomEnd: Dp = topStart, val bottomStart: Dp = topStart) : Shape

/** A circle: corners as round as the box allows. */
val CircleShape: Shape = RoundedCornerShape(Dp(1e6f))
val RectangleShape: Shape = RoundedCornerShape(0.dp)
