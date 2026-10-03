package koral.compose

import java.lang.foreign.Arena
import java.lang.foreign.MemorySegment
import java.lang.foreign.SegmentAllocator
import kotlin.math.PI
import koral.ui.interop.KuiLayouts
import koral.ui.interop.KuiNative

// Drawing, as Compose's Canvas draws: a DrawScope with drawRect, drawCircle, drawPath and the rest, over
// koral-ui's canvas — every shape an exact anti-aliased signed distance, paths tessellated once.

/** How a shape is painted: filled, or outlined. */
sealed interface DrawStyle
object Fill : DrawStyle
data class Stroke(
    val width: Float = 1f,
    val miter: Float = 4f,
    val cap: StrokeCap = StrokeCap.Butt,
    val join: StrokeJoin = StrokeJoin.Miter,
) : DrawStyle

enum class StrokeCap(internal val kui: Int) { Butt(0), Round(1), Square(2) }
enum class StrokeJoin(internal val kui: Int) { Miter(0), Round(1), Bevel(2) }
enum class PathFillType(internal val kui: Int) { NonZero(0), EvenOdd(1) }

/** What fills a shape: a colour, or a gradient. */
sealed class Brush {
    internal abstract fun native(a: SegmentAllocator): MemorySegment?   // a KuiGradient, or null for a colour

    companion object {
        fun linearGradient(colors: List<Color>, start: Offset = Offset.Zero, end: Offset = Offset(Float.POSITIVE_INFINITY, Float.POSITIVE_INFINITY)) =
            Gradient(0, colors, start, end, 0f)
        fun horizontalGradient(colors: List<Color>, startX: Float = 0f, endX: Float = Float.POSITIVE_INFINITY) =
            Gradient(0, colors, Offset(startX, 0f), Offset(endX, 0f), 0f)
        fun verticalGradient(colors: List<Color>, startY: Float = 0f, endY: Float = Float.POSITIVE_INFINITY) =
            Gradient(0, colors, Offset(0f, startY), Offset(0f, endY), 0f)
        fun radialGradient(colors: List<Color>, center: Offset, radius: Float) = Gradient(1, colors, center, center, radius)
        fun sweepGradient(colors: List<Color>, center: Offset) = Gradient(2, colors, center, center, 0f)
    }
}

class SolidColor(val value: Color) : Brush() {
    override fun native(a: SegmentAllocator): MemorySegment? = null
}

class Gradient internal constructor(
    private val kind: Int, private val colors: List<Color>, private val start: Offset, private val end: Offset, private val radius: Float,
) : Brush() {
    /** Infinite ends (Compose's "to the far edge") resolved against the size drawn in. */
    internal var size: Size = Size.Zero

    override fun native(a: SegmentAllocator): MemorySegment? {
        val offsets = a.allocate(java.lang.foreign.ValueLayout.JAVA_FLOAT, colors.size.toLong())
        val stops = a.allocate(KuiLayouts.KuiColor, colors.size.toLong())
        colors.forEachIndexed { i, c ->
            offsets.setAtIndex(java.lang.foreign.ValueLayout.JAVA_FLOAT, i.toLong(), if (colors.size > 1) i / (colors.size - 1f) else 0f)
            Struct(stops.asSlice(i * KuiLayouts.KuiColor.byteSize(), KuiLayouts.KuiColor), KuiLayouts.KuiColor).color("r", c)
        }
        fun clamp(v: Float, max: Float) = if (v.isInfinite()) max else v
        val e = Offset(clamp(end.x, size.width), clamp(end.y, size.height))
        val n = colors.size.toLong()
        return when (kind) {
            0 -> KuiNative.kui_gradient_linear(vec2(a, start.x, start.y), vec2(a, e.x, e.y), offsets, stops, n)
            1 -> KuiNative.kui_gradient_radial(vec2(a, start.x, start.y), radius, offsets, stops, n)
            else -> KuiNative.kui_gradient_sweep(vec2(a, start.x, start.y), 0f, offsets, stops, n)
        }
    }
}

/** A path, as Compose builds one: moveTo, lineTo, curves, arcs, close. */
class Path {
    internal val ops = mutableListOf<(MemorySegment, Arena) -> Unit>()
    var fillType: PathFillType = PathFillType.NonZero

    fun moveTo(x: Float, y: Float) { ops += { p, a -> KuiNative.kui_path_move_to(p, vec2(a, x, y)) } }
    fun lineTo(x: Float, y: Float) { ops += { p, a -> KuiNative.kui_path_line_to(p, vec2(a, x, y)) } }
    fun quadraticTo(x1: Float, y1: Float, x2: Float, y2: Float) { ops += { p, a -> KuiNative.kui_path_quad_to(p, vec2(a, x1, y1), vec2(a, x2, y2)) } }
    fun cubicTo(x1: Float, y1: Float, x2: Float, y2: Float, x3: Float, y3: Float) {
        ops += { p, a -> KuiNative.kui_path_cubic_to(p, vec2(a, x1, y1), vec2(a, x2, y2), vec2(a, x3, y3)) }
    }
    /** An arc of the circle inscribed in [rect], from [startAngleDegrees] clockwise by [sweepAngleDegrees]. */
    fun arcTo(rect: Rect, startAngleDegrees: Float, sweepAngleDegrees: Float, @Suppress("UNUSED_PARAMETER") forceMoveTo: Boolean = false) {
        ops += { p, a ->
            KuiNative.kui_path_arc_to(p, vec2(a, rect.center.x, rect.center.y), rect.minDimension / 2f,
                                      radians(startAngleDegrees), radians(sweepAngleDegrees))
        }
    }
    fun close() { ops += { p, _ -> KuiNative.kui_path_close(p) } }
    fun addRect(rect: Rect) { ops += { p, a -> KuiNative.kui_path_add_rect(p, rect.native(a)) } }
    fun addOval(rect: Rect) { ops += { p, a -> KuiNative.kui_path_add_oval(p, rect.native(a)) } }
    fun addRoundRect(rect: Rect, radius: CornerRadius) {
        ops += { p, a -> KuiNative.kui_path_add_rrect(p, rect.native(a), radii(a, RoundedCornerShape(radius.x.dp))) }
    }
    fun reset() = ops.clear()

    internal fun <T> native(a: Arena, use: (MemorySegment) -> T): T {
        val p = KuiNative.kui_path_new()
        try {
            ops.forEach { it(p, a) }
            KuiNative.kui_path_set_fill_rule(p, fillType.kui)
            return use(p)
        } finally {
            KuiNative.kui_path_destroy(p)
        }
    }
}

/** A rectangle, by its edges. */
data class Rect(val left: Float, val top: Float, val right: Float, val bottom: Float) {
    constructor(offset: Offset, size: Size) : this(offset.x, offset.y, offset.x + size.width, offset.y + size.height)
    val width get() = right - left
    val height get() = bottom - top
    val center get() = Offset((left + right) / 2f, (top + bottom) / 2f)
    val minDimension get() = minOf(width, height)
    internal fun native(a: SegmentAllocator): MemorySegment =
        Struct(a, KuiLayouts.KuiRect).float("left", left).float("top", top).float("right", right).float("bottom", bottom).segment
}

private fun radians(degrees: Float) = (degrees * PI / 180.0).toFloat()

/**
 * What a Canvas draws in: Compose's DrawScope, over koral-ui's canvas. [size] is the Canvas's; [center]
 * its middle. Transforms and clips nest as blocks.
 */
class DrawScope internal constructor(private val canvas: MemorySegment, val size: Size) {
    val center: Offset get() = size.center

    private fun <T> paint(brush: Brush, alpha: Float, style: DrawStyle, body: (MemorySegment) -> T): T = Arena.ofConfined().use { a ->
        if (brush is Gradient) brush.size = size
        val gradient = brush.native(a)
        try {
            val s = Struct(a, KuiLayouts.KuiPaint).float("opacity", alpha)
            val color = (brush as? SolidColor)?.value ?: Color.White
            when (style) {
                is Fill -> {
                    s.color("fill", color)
                    if (gradient != null) s.address("gradient", gradient)
                }
                is Stroke -> s.float("stroke.width", style.width).color("stroke.color", color)
                    .int("stroke.cap", style.cap.kui).int("stroke.join", style.join.kui).float("stroke.miter_limit", style.miter)
            }
            body(s.segment)
        } finally {
            if (gradient != null) KuiNative.kui_gradient_release(gradient)
        }
    }

    private fun rect(a: SegmentAllocator, topLeft: Offset, size: Size) = Rect(topLeft, size).native(a)

    fun drawRect(color: Color, topLeft: Offset = Offset.Zero, size: Size = this.size, alpha: Float = 1f, style: DrawStyle = Fill) =
        drawRect(SolidColor(color), topLeft, size, alpha, style)
    fun drawRect(brush: Brush, topLeft: Offset = Offset.Zero, size: Size = this.size, alpha: Float = 1f, style: DrawStyle = Fill) =
        paint(brush, alpha, style) { p -> Arena.ofConfined().use { a -> KuiNative.kui_canvas_draw_rect(canvas, rect(a, topLeft, size), p) } }

    fun drawRoundRect(color: Color, topLeft: Offset = Offset.Zero, size: Size = this.size, cornerRadius: CornerRadius = CornerRadius.Zero,
                      alpha: Float = 1f, style: DrawStyle = Fill) =
        drawRoundRect(SolidColor(color), topLeft, size, cornerRadius, alpha, style)
    fun drawRoundRect(brush: Brush, topLeft: Offset = Offset.Zero, size: Size = this.size, cornerRadius: CornerRadius = CornerRadius.Zero,
                      alpha: Float = 1f, style: DrawStyle = Fill) =
        paint(brush, alpha, style) { p ->
            Arena.ofConfined().use { a ->
                KuiNative.kui_canvas_draw_rrect(canvas, rect(a, topLeft, size), radii(a, RoundedCornerShape(cornerRadius.x.dp)), p)
            }
        }

    fun drawCircle(color: Color, radius: Float = size.minDimension / 2f, center: Offset = this.center, alpha: Float = 1f, style: DrawStyle = Fill) =
        drawCircle(SolidColor(color), radius, center, alpha, style)
    fun drawCircle(brush: Brush, radius: Float = size.minDimension / 2f, center: Offset = this.center, alpha: Float = 1f, style: DrawStyle = Fill) =
        paint(brush, alpha, style) { p -> Arena.ofConfined().use { a -> KuiNative.kui_canvas_draw_circle(canvas, vec2(a, center.x, center.y), radius, p) } }

    fun drawOval(color: Color, topLeft: Offset = Offset.Zero, size: Size = this.size, alpha: Float = 1f, style: DrawStyle = Fill) =
        paint(SolidColor(color), alpha, style) { p -> Arena.ofConfined().use { a -> KuiNative.kui_canvas_draw_oval(canvas, rect(a, topLeft, size), p) } }

    /** An arc of the circle in [topLeft]/[size], from [startAngle] clockwise by [sweepAngle], in degrees; a pie with [useCenter]. */
    fun drawArc(color: Color, startAngle: Float, sweepAngle: Float, useCenter: Boolean, topLeft: Offset = Offset.Zero,
                size: Size = this.size, alpha: Float = 1f, style: DrawStyle = Fill) {
        val r = Rect(topLeft, size)
        // Without the centre an arc is a line: drawn with the stroke it is given, or a hairline.
        val arcStyle = if (!useCenter && style is Fill) Stroke(1f) else style
        paint(SolidColor(color), alpha, arcStyle) { p ->
            Arena.ofConfined().use { a ->
                KuiNative.kui_canvas_draw_arc(canvas, vec2(a, r.center.x, r.center.y), r.minDimension / 2f, radians(startAngle), radians(sweepAngle), useCenter, p)
            }
        }
    }

    fun drawLine(color: Color, start: Offset, end: Offset, strokeWidth: Float = 1f, cap: StrokeCap = StrokeCap.Butt, alpha: Float = 1f) =
        paint(SolidColor(color), alpha, Stroke(strokeWidth, cap = cap)) { p ->
            Arena.ofConfined().use { a -> KuiNative.kui_canvas_draw_line(canvas, vec2(a, start.x, start.y), vec2(a, end.x, end.y), p) }
        }

    fun drawPath(path: Path, color: Color, alpha: Float = 1f, style: DrawStyle = Fill) = drawPath(path, SolidColor(color), alpha, style)
    fun drawPath(path: Path, brush: Brush, alpha: Float = 1f, style: DrawStyle = Fill) =
        paint(brush, alpha, style) { p -> Arena.ofConfined().use { a -> path.native(a) { kp -> KuiNative.kui_canvas_draw_path(canvas, kp, p) } } }

    /** One line of text, its top-left at [topLeft]. */
    fun drawText(text: String, topLeft: Offset = Offset.Zero, color: Color = Color.Black, fontSize: TextUnit = 14.sp) =
        Arena.ofConfined().use { a ->
            val style = Struct(a, KuiLayouts.KuiTextStyle).float("size", fontSize.value).color("color", color).float("line_height", 1.25f)
            KuiNative.kui_canvas_draw_text(canvas, text, vec2(a, topLeft.x, topLeft.y), style.segment)
        }

    // -- transforms and clips, as blocks
    inline fun translate(left: Float = 0f, top: Float = 0f, block: DrawScope.() -> Unit) = within({ move(left, top) }, block)
    inline fun rotate(degrees: Float, pivot: Offset = center, block: DrawScope.() -> Unit) = within({ turn(degrees, pivot) }, block)
    inline fun scale(scale: Float, pivot: Offset = center, block: DrawScope.() -> Unit) = within({ grow(scale, pivot) }, block)
    inline fun clipRect(left: Float = 0f, top: Float = 0f, right: Float = size.width, bottom: Float = size.height, block: DrawScope.() -> Unit) =
        within({ clip(left, top, right, bottom) }, block)

    @PublishedApi internal inline fun within(set: () -> Unit, block: DrawScope.() -> Unit) {
        save()
        try { set(); block() } finally { restore() }
    }
    @PublishedApi internal fun save() = KuiNative.kui_canvas_save(canvas)
    @PublishedApi internal fun restore() = KuiNative.kui_canvas_restore(canvas)
    @PublishedApi internal fun move(x: Float, y: Float) = Arena.ofConfined().use { a -> KuiNative.kui_canvas_translate(canvas, vec2(a, x, y)) }
    @PublishedApi internal fun turn(degrees: Float, pivot: Offset) {
        move(pivot.x, pivot.y); KuiNative.kui_canvas_rotate(canvas, radians(degrees)); move(-pivot.x, -pivot.y)
    }
    @PublishedApi internal fun grow(s: Float, pivot: Offset) {
        move(pivot.x, pivot.y); Arena.ofConfined().use { a -> KuiNative.kui_canvas_scale(canvas, vec2(a, s, s)) }; move(-pivot.x, -pivot.y)
    }
    @PublishedApi internal fun clip(l: Float, t: Float, r: Float, b: Float) =
        Arena.ofConfined().use { a -> KuiNative.kui_canvas_clip_rect(canvas, Rect(l, t, r, b).native(a)) }
}
