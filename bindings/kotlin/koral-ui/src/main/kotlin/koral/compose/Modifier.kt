package koral.compose

/**
 * Decorations and behaviour for a composable, chained as in Compose: `Modifier.padding(16.dp).background(Color.Red)`.
 * Order matters as it does there — the first is outermost, so padding before a background pads outside it.
 */
sealed interface Modifier {
    infix fun then(other: Modifier): Modifier =
        if (other === Modifier) this else if (this === Modifier) other else Combined(this, other)

    /** Every element, outermost first. */
    fun elements(): List<Element> = when (this) {
        is Combined -> outer.elements() + inner.elements()
        is Element -> listOf(this)
        else -> emptyList()
    }

    /** One modifier. */
    sealed interface Element : Modifier

    /** The empty modifier, which every chain starts from. */
    companion object : Modifier

    private data class Combined(val outer: Modifier, val inner: Modifier) : Modifier
}

internal data class PaddingElement(val start: Dp, val top: Dp, val end: Dp, val bottom: Dp) : Modifier.Element
internal data class BackgroundElement(val color: Color, val shape: Shape) : Modifier.Element
internal data class BorderElement(val width: Dp, val color: Color, val shape: Shape) : Modifier.Element
internal data class SizeElement(val width: Dp, val height: Dp) : Modifier.Element
internal data class FillElement(val width: Float, val height: Float) : Modifier.Element   // fractions; 0: not filled that way
internal class ClickableElement(val enabled: Boolean, val onClick: () -> Unit) : Modifier.Element {
    // Equal whatever the lambda: what it calls is looked up when clicked, so a new one rebuilds nothing.
    override fun equals(other: Any?) = other is ClickableElement && other.enabled == enabled
    override fun hashCode() = enabled.hashCode()
}
/** What a drag carries: its kind, which targets go by, and the thing itself. */
data class DragData(val type: String, val payload: Any? = null)

// Equal whatever their lambdas, as a clickable is: what they call is looked up when it happens.
internal class DragSourceElement(val data: DragData, val enabled: Boolean, val onDragStart: (() -> Unit)?,
                                 val onDragEnd: ((Boolean) -> Unit)?) : Modifier.Element {
    override fun equals(other: Any?) = other is DragSourceElement && other.data == data && other.enabled == enabled
    override fun hashCode() = data.hashCode() * 31 + enabled.hashCode()
}
internal class DropTargetElement(val type: String?, val onEnter: ((DragData) -> Unit)?, val onLeave: (() -> Unit)?,
                                 val onMove: ((DragData, Offset) -> Unit)?, val onDrop: (DragData) -> Unit) : Modifier.Element {
    override fun equals(other: Any?) = other is DropTargetElement && other.type == type
    override fun hashCode() = type.hashCode()
}
internal data class AlphaElement(val alpha: Float) : Modifier.Element
internal data class ClipElement(val shape: Shape) : Modifier.Element
internal data class OffsetElement(val x: Dp, val y: Dp) : Modifier.Element
internal data class ScrollElement(val vertical: Boolean) : Modifier.Element
internal data class WeightElement(val weight: Float, val fill: Boolean) : Modifier.Element
internal data class AlignElement(val alignment: Alignment) : Modifier.Element

fun Modifier.padding(all: Dp): Modifier = then(PaddingElement(all, all, all, all))
fun Modifier.padding(horizontal: Dp = 0.dp, vertical: Dp = 0.dp): Modifier = then(PaddingElement(horizontal, vertical, horizontal, vertical))
fun Modifier.padding(start: Dp = 0.dp, top: Dp = 0.dp, end: Dp = 0.dp, bottom: Dp = 0.dp): Modifier = then(PaddingElement(start, top, end, bottom))

fun Modifier.background(color: Color, shape: Shape = RectangleShape): Modifier = then(BackgroundElement(color, shape))
fun Modifier.border(width: Dp, color: Color, shape: Shape = RectangleShape): Modifier = then(BorderElement(width, color, shape))

fun Modifier.size(size: Dp): Modifier = then(SizeElement(size, size))
fun Modifier.size(width: Dp, height: Dp): Modifier = then(SizeElement(width, height))
fun Modifier.width(width: Dp): Modifier = then(SizeElement(width, Dp.Unspecified))
fun Modifier.height(height: Dp): Modifier = then(SizeElement(Dp.Unspecified, height))

/** As wide as allowed. Only the whole width (a fraction of 1) is supported. */
fun Modifier.fillMaxWidth(fraction: Float = 1f): Modifier = then(FillElement(fraction, 0f))
fun Modifier.fillMaxHeight(fraction: Float = 1f): Modifier = then(FillElement(0f, fraction))
fun Modifier.fillMaxSize(fraction: Float = 1f): Modifier = then(FillElement(fraction, fraction))

/** Calls [onClick] when clicked, and shows when hovered and pressed. */
fun Modifier.clickable(enabled: Boolean = true, onClick: () -> Unit): Modifier = then(ClickableElement(enabled, onClick))
fun Modifier.alpha(alpha: Float): Modifier = then(AlphaElement(alpha))

/**
 * Can be picked up and dragged, carrying a [DragData] of [type] and [payload] until it is dropped on a
 * [dropTarget] that takes it, or let go anywhere else. [onDragEnd] hears whether a target took it.
 */
fun Modifier.dragSource(type: String, payload: Any? = null, enabled: Boolean = true, onDragStart: (() -> Unit)? = null,
                        onDragEnd: ((accepted: Boolean) -> Unit)? = null): Modifier =
    then(DragSourceElement(DragData(type, payload), enabled, onDragStart, onDragEnd))

/**
 * Takes drags of [type] (any, when null) dropped on it. [onEnter] and [onLeave] say when one is over it —
 * to show it — and [onDrop] gets it.
 */
fun Modifier.dropTarget(type: String? = null, onEnter: ((DragData) -> Unit)? = null, onLeave: (() -> Unit)? = null,
                        onMove: ((DragData, Offset) -> Unit)? = null, onDrop: (DragData) -> Unit): Modifier =
    then(DropTargetElement(type, onEnter, onLeave, onMove, onDrop))

/** Cuts what follows to [shape]: `Modifier.clip(RoundedCornerShape(8.dp)).background(...)`. */
fun Modifier.clip(shape: Shape): Modifier = then(ClipElement(shape))

/**
 * Moves what follows by ([x], [y]) where it is drawn and clicked; the layout around it does not move. As
 * everywhere in koral-ui, a click reaches it only inside its parent's box.
 */
fun Modifier.offset(x: Dp = 0.dp, y: Dp = 0.dp): Modifier = then(OffsetElement(x, y))

/** What rememberScrollState() gives: the scroll position is the UI's, kept with what it scrolls. */
class ScrollState internal constructor()
fun rememberScrollState(): ScrollState = ScrollState()
fun Modifier.verticalScroll(@Suppress("UNUSED_PARAMETER") state: ScrollState): Modifier = then(ScrollElement(vertical = true))
fun Modifier.horizontalScroll(@Suppress("UNUSED_PARAMETER") state: ScrollState): Modifier = then(ScrollElement(vertical = false))

/** In a Row. */
interface RowScope {
    /** A share of the Row's width, by weight against its other weighted children. */
    fun Modifier.weight(weight: Float, fill: Boolean = true): Modifier = then(WeightElement(weight, fill))
    fun Modifier.align(alignment: Alignment.Vertical): Modifier = then(AlignElement(Alignment(0f, alignment.bias)))
}

/** In a Column. */
interface ColumnScope {
    /** A share of the Column's height, by weight against its other weighted children. */
    fun Modifier.weight(weight: Float, fill: Boolean = true): Modifier = then(WeightElement(weight, fill))
    fun Modifier.align(alignment: Alignment.Horizontal): Modifier = then(AlignElement(Alignment(alignment.bias, 0f)))
}

/** In a Box. */
interface BoxScope {
    /** Placed by its own alignment in the Box. */
    fun Modifier.align(alignment: Alignment): Modifier = then(AlignElement(alignment))
}

internal object RowScopeInstance : RowScope
internal object ColumnScopeInstance : ColumnScope
internal object BoxScopeInstance : BoxScope
