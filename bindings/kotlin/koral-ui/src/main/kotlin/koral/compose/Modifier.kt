package koral.compose

import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.launch

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
internal data class ScrollElement(val vertical: Boolean, val state: ScrollState) : Modifier.Element
internal data class TransformElement(val scaleX: Float, val scaleY: Float, val rotation: Float, val translationX: Float, val translationY: Float,
                                     val originX: Float, val originY: Float) : Modifier.Element
internal data class AspectRatioElement(val ratio: Float) : Modifier.Element
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

/** Which of the sizes something has of itself: koral-ui knows one, the size it is with all the room there is. */
enum class IntrinsicSize { Min, Max }
internal data class IntrinsicElement(val width: Boolean, val height: Boolean) : Modifier.Element

/**
 * As wide as what is in it is of itself: `Column(Modifier.width(IntrinsicSize.Max))` is as wide as its widest
 * child, and its other children can fill that.
 */
fun Modifier.width(@Suppress("UNUSED_PARAMETER") intrinsicSize: IntrinsicSize): Modifier = then(IntrinsicElement(width = true, height = false))
/** As tall as what is in it is of itself: in a `Row(Modifier.height(IntrinsicSize.Min))` a divider can fill the height of the tallest. */
fun Modifier.height(@Suppress("UNUSED_PARAMETER") intrinsicSize: IntrinsicSize): Modifier = then(IntrinsicElement(width = false, height = true))

/** As wide as allowed — or that [fraction] of it. */
fun Modifier.fillMaxWidth(fraction: Float = 1f): Modifier = then(FillElement(fraction, 0f))
fun Modifier.fillMaxHeight(fraction: Float = 1f): Modifier = then(FillElement(0f, fraction))
fun Modifier.fillMaxSize(fraction: Float = 1f): Modifier = then(FillElement(fraction, fraction))

/** Calls [onClick] when clicked, and shows when hovered and pressed. */
fun Modifier.clickable(enabled: Boolean = true, onClick: () -> Unit): Modifier = then(ClickableElement(enabled, onClick))

// Equal whatever its lambda, as a clickable is: what it calls is looked up when the size changes.
internal class SizeChangedElement(val onChanged: (IntSize) -> Unit) : Modifier.Element {
    override fun equals(other: Any?) = other is SizeChangedElement
    override fun hashCode() = 7919
}

/**
 * Tells [onSizeChanged] how big this has been laid out, in pixels — the first time, and whenever that changes — as
 * Compose's does. What shows a texture drawn elsewhere (a viewport) makes the texture that size there:
 *
 * ```
 * Picture(scene.viewport, "Viewport", Modifier.fillMaxSize().onSizeChanged { scene.resizeViewport(it) })
 * ```
 *
 * It is called while the interface is laid out; state it sets shows from the next frame.
 */
fun Modifier.onSizeChanged(onSizeChanged: (size: IntSize) -> Unit): Modifier = then(SizeChangedElement(onSizeChanged))

internal data class ConstraintsElement(val minWidth: Dp, val maxWidth: Dp, val minHeight: Dp, val maxHeight: Dp) : Modifier.Element
internal data class WrapElement(val alignment: Alignment) : Modifier.Element
internal data class ShadowElement(val elevation: Dp, val shape: Shape) : Modifier.Element

/** No narrower than [min], no wider than [max], whatever it would be otherwise. */
fun Modifier.widthIn(min: Dp = Dp.Unspecified, max: Dp = Dp.Unspecified): Modifier = then(ConstraintsElement(min, max, Dp.Unspecified, Dp.Unspecified))
fun Modifier.heightIn(min: Dp = Dp.Unspecified, max: Dp = Dp.Unspecified): Modifier = then(ConstraintsElement(Dp.Unspecified, Dp.Unspecified, min, max))
fun Modifier.sizeIn(minWidth: Dp = Dp.Unspecified, minHeight: Dp = Dp.Unspecified, maxWidth: Dp = Dp.Unspecified, maxHeight: Dp = Dp.Unspecified): Modifier =
    then(ConstraintsElement(minWidth, maxWidth, minHeight, maxHeight))
/** No smaller than this each way: what Material asks of anything pressed. */
fun Modifier.defaultMinSize(minWidth: Dp = Dp.Unspecified, minHeight: Dp = Dp.Unspecified): Modifier =
    then(ConstraintsElement(minWidth, Dp.Unspecified, minHeight, Dp.Unspecified))

/** The size, as [size] sets it: koral-ui's sizes are kept within what the parent allows either way. */
fun Modifier.requiredSize(size: Dp): Modifier = size(size)
fun Modifier.requiredSize(width: Dp, height: Dp): Modifier = size(width, height)
fun Modifier.requiredWidth(width: Dp): Modifier = width(width)
fun Modifier.requiredHeight(height: Dp): Modifier = height(height)
fun Modifier.size(size: DpSize): Modifier = size(size.width, size.height)

/** As big as it wants to be, placed by [align] in the room it is given — whatever size that room says to be. */
fun Modifier.wrapContentSize(align: Alignment = Alignment.Center): Modifier = then(WrapElement(align))
fun Modifier.wrapContentWidth(align: Alignment.Horizontal = Alignment.CenterHorizontally): Modifier = then(WrapElement(Alignment(align.bias, 0f)))
fun Modifier.wrapContentHeight(align: Alignment.Vertical = Alignment.CenterVertically): Modifier = then(WrapElement(Alignment(0f, align.bias)))

/** A shadow under it, as of something [elevation] above what is behind it, in the outline of [shape]. */
fun Modifier.shadow(elevation: Dp, shape: Shape = RectangleShape): Modifier = if (elevation.value > 0f) then(ShadowElement(elevation, shape)) else this

/** Cut to its own box. */
fun Modifier.clipToBounds(): Modifier = clip(RectangleShape)
fun Modifier.padding(paddingValues: PaddingValues): Modifier =
    padding(paddingValues.start, paddingValues.top, paddingValues.end, paddingValues.bottom)
fun Modifier.offset(offset: DpOffset): Modifier = offset(offset.x, offset.y)
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

/**
 * Where something scrolled is: [value], from 0 to [maxValue], in the interface's units. Read where it is shown —
 * it is state — and set with [scrollTo]; [animateScrollTo] gets there over a few frames.
 */
class ScrollState(initial: Int = 0) {
    var value: Int by androidx.compose.runtime.mutableStateOf(initial)
        internal set
    var maxValue: Int by androidx.compose.runtime.mutableStateOf(Int.MAX_VALUE)
        internal set
    val canScrollForward: Boolean get() = value < maxValue
    val canScrollBackward: Boolean get() = value > 0
    // Where it has been told to go, and how many times: what scrolls it goes there when the count changes.
    internal var jumpTo = initial.toFloat()
    internal var jump = if (initial != 0) 1 else 0
    internal var attached: (() -> Unit)? = null

    @Suppress("RedundantSuspendModifier")
    suspend fun scrollTo(value: Int): Float {
        val before = this.value
        jumpTo = value.coerceIn(0, maxValue).toFloat()
        jump++
        attached?.invoke()
        return jumpTo - before
    }

    suspend fun animateScrollTo(value: Int, animationSpec: AnimationSpec<Float> = spring()) {
        val position = Animatable(this.value.toFloat())
        val target = value.coerceIn(0, maxValue).toFloat()
        var last = -1f
        coroutineScope {
            val run = launch { position.animateTo(target, animationSpec) }
            while (run.isActive) {
                androidx.compose.runtime.withFrameNanos { }
                if (position.value != last) { last = position.value; scrollTo(Math.round(last)) }
            }
        }
        scrollTo(Math.round(target))
    }

    @Suppress("RedundantSuspendModifier")
    suspend fun scrollBy(value: Float): Float = scrollTo(Math.round(this.value + value))
}

@androidx.compose.runtime.Composable
fun rememberScrollState(initial: Int = 0): ScrollState = androidx.compose.runtime.remember { ScrollState(initial) }
fun Modifier.verticalScroll(state: ScrollState, @Suppress("UNUSED_PARAMETER") enabled: Boolean = true): Modifier = then(ScrollElement(vertical = true, state))
fun Modifier.horizontalScroll(state: ScrollState, @Suppress("UNUSED_PARAMETER") enabled: Boolean = true): Modifier = then(ScrollElement(vertical = false, state))

/** Where a transform turns and grows about, as fractions of the box: (0.5, 0.5) is its middle. */
data class TransformOrigin(val pivotFractionX: Float, val pivotFractionY: Float) {
    companion object { val Center = TransformOrigin(0.5f, 0.5f) }
}

/**
 * Drawn — and pressed — grown by [scaleX] and [scaleY], turned [rotationZ] degrees and moved by the translation,
 * about [transformOrigin]; and through [alpha]. Its layout is as it was: what is round it does not move.
 */
fun Modifier.graphicsLayer(scaleX: Float = 1f, scaleY: Float = 1f, alpha: Float = 1f, translationX: Float = 0f, translationY: Float = 0f,
                           rotationZ: Float = 0f, transformOrigin: TransformOrigin = TransformOrigin.Center, clip: Boolean = false): Modifier {
    var all: Modifier = this
    if (scaleX != 1f || scaleY != 1f || rotationZ != 0f || translationX != 0f || translationY != 0f)
        all = all.then(TransformElement(scaleX, scaleY, rotationZ, translationX, translationY, transformOrigin.pivotFractionX, transformOrigin.pivotFractionY))
    if (alpha != 1f) all = all.alpha(alpha)
    if (clip) all = all.clipToBounds()
    return all
}

/** What a [graphicsLayer] block sets. */
class GraphicsLayerScope {
    var scaleX = 1f; var scaleY = 1f; var alpha = 1f; var translationX = 0f; var translationY = 0f; var rotationZ = 0f
    var transformOrigin = TransformOrigin.Center; var clip = false
}
fun Modifier.graphicsLayer(block: GraphicsLayerScope.() -> Unit): Modifier = GraphicsLayerScope().apply(block).let {
    graphicsLayer(it.scaleX, it.scaleY, it.alpha, it.translationX, it.translationY, it.rotationZ, it.transformOrigin, it.clip)
}
fun Modifier.rotate(degrees: Float): Modifier = if (degrees != 0f) graphicsLayer(rotationZ = degrees) else this
fun Modifier.scale(scale: Float): Modifier = if (scale != 1f) graphicsLayer(scaleX = scale, scaleY = scale) else this
fun Modifier.scale(scaleX: Float, scaleY: Float): Modifier = if (scaleX != 1f || scaleY != 1f) graphicsLayer(scaleX = scaleX, scaleY = scaleY) else this

/** As wide as it may be, and as tall as that makes it at [ratio] — width over height. */
fun Modifier.aspectRatio(ratio: Float, @Suppress("UNUSED_PARAMETER") matchHeightConstraintsFirst: Boolean = false): Modifier = then(AspectRatioElement(ratio))

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
