package koral.compose

import androidx.compose.runtime.Composable
import java.lang.foreign.MemorySegment
import java.lang.foreign.ValueLayout
import koral.ui.interop.KuiLayouts
import koral.ui.interop.KuiNative

// A layout of the caller's own, as Compose's Layout is written:
//
//     Layout(content = { Text("a"); Text("b") }) { measurables, constraints ->
//         val placeables = measurables.map { it.measure(constraints.copy(minWidth = 0, minHeight = 0)) }
//         layout(constraints.maxWidth, placeables.sumOf { it.height }) {
//             var y = 0
//             placeables.forEach { it.place(0, y); y += it.height }
//         }
//     }
//
// Sizes are whole numbers of the interface's units — which are pixels where its scale is one, as they are in
// Compose — and Constraints.Infinity is no limit. A child's intrinsic size is the size it is with all the room
// there is that way: the least and the most it could be are, here, the same.

/** How big something may be: from the least to the most, each way. */
data class Constraints(val minWidth: Int = 0, val maxWidth: Int = Infinity, val minHeight: Int = 0, val maxHeight: Int = Infinity) {
    val hasBoundedWidth: Boolean get() = maxWidth != Infinity
    val hasBoundedHeight: Boolean get() = maxHeight != Infinity
    val hasFixedWidth: Boolean get() = minWidth == maxWidth
    val hasFixedHeight: Boolean get() = minHeight == maxHeight
    fun constrainWidth(width: Int): Int = width.coerceIn(minWidth, maxWidth)
    fun constrainHeight(height: Int): Int = height.coerceIn(minHeight, maxHeight)

    companion object {
        const val Infinity = Int.MAX_VALUE
        fun fixed(width: Int, height: Int) = Constraints(width, width, height, height)
        fun fixedWidth(width: Int) = Constraints(minWidth = width, maxWidth = width)
        fun fixedHeight(height: Int) = Constraints(minHeight = height, maxHeight = height)
    }
}

/** A child of a layout, before it has a size: measured once, with the constraints the layout likes. */
interface Measurable {
    fun measure(constraints: Constraints): Placeable
    /** How wide it is of itself, [height] tall at the most. It is laid out to find out: measure it after, to say how it is to be. */
    fun maxIntrinsicWidth(height: Int): Int = measure(Constraints(maxHeight = height)).width
    fun minIntrinsicWidth(height: Int): Int = maxIntrinsicWidth(height)
    /** How tall it is of itself, [width] wide at the most. */
    fun maxIntrinsicHeight(width: Int): Int = measure(Constraints(maxWidth = width)).height
    fun minIntrinsicHeight(width: Int): Int = maxIntrinsicHeight(width)
}

/** A child of a layout, measured: how big it came out, to be placed. */
abstract class Placeable {
    abstract val width: Int
    abstract val height: Int

    /** Where a layout's placement block is written: the children are put where it says. */
    abstract class PlacementScope {
        abstract fun Placeable.place(x: Int, y: Int, zIndex: Float = 0f)
        fun Placeable.place(position: IntOffset, zIndex: Float = 0f) = place(position.x, position.y, zIndex)
        fun Placeable.placeRelative(x: Int, y: Int, zIndex: Float = 0f) = place(x, y, zIndex)
        fun Placeable.placeRelative(position: IntOffset, zIndex: Float = 0f) = place(position.x, position.y, zIndex)
    }
}

/** What a measure block gives back: the layout's own size, and where its children go. */
class MeasureResult internal constructor(val width: Int, val height: Int, internal val placement: Placeable.PlacementScope.() -> Unit)

/** What a measure block is written in. */
interface MeasureScope {
    fun layout(width: Int, height: Int, placementBlock: Placeable.PlacementScope.() -> Unit): MeasureResult = MeasureResult(width, height, placementBlock)
}

/** The rule of a layout: how it measures and places what it holds. */
fun interface MeasurePolicy {
    fun MeasureScope.measure(measurables: List<Measurable>, constraints: Constraints): MeasureResult
}

private fun whole(v: Float) = if (v.isInfinite()) Constraints.Infinity else Math.round(v)
private fun unit(v: Int) = if (v == Constraints.Infinity) Float.POSITIVE_INFINITY else v.toFloat()

/** [content], measured and placed by [measurePolicy]: each thing [content] emits is one measurable, in order. */
@Composable
fun Layout(content: @Composable () -> Unit, modifier: Modifier = Modifier, measurePolicy: MeasurePolicy) {
    val policy = androidx.compose.runtime.rememberUpdatedState(measurePolicy)
    Node(modifier, measurePolicy, { _, kids ->
        scratch { a ->
            // Called by koral-ui while it lays the interface out, with what it may measure and place through.
            val rule = { context: MemorySegment, c: FloatArray ->
                val out = scratch { it.allocate(ValueLayout.JAVA_FLOAT, 2) }
                val count = KuiNative.kui_layout_count(context).toInt()
                val measurables = List(count) { index ->
                    object : Measurable {
                        override fun measure(constraints: Constraints): Placeable = scratch { m ->
                            val size = m.allocate(ValueLayout.JAVA_FLOAT, 2)
                            KuiNative.kui_layout_measure(context, index.toLong(), unit(constraints.minWidth), unit(constraints.maxWidth),
                                unit(constraints.minHeight), unit(constraints.maxHeight), size, size.asSlice(4))
                            val w = Math.round(size.getAtIndex(ValueLayout.JAVA_FLOAT, 0)); val h = Math.round(size.getAtIndex(ValueLayout.JAVA_FLOAT, 1))
                            Child(index, w, h)
                        }
                    }
                }
                val result = with(policy.value) { MeasureScopeInstance.measure(measurables, Constraints(whole(c[0]), whole(c[1]), whole(c[2]), whole(c[3]))) }
                result.placement(object : Placeable.PlacementScope() {
                    override fun Placeable.place(x: Int, y: Int, zIndex: Float) {
                        (this as? Child)?.let { KuiNative.kui_layout_place(context, it.index.toLong(), x.toFloat(), y.toFloat()) }
                    }
                })
                out.let { Size(result.width.toFloat(), result.height.toFloat()) }
            }
            KuiNative.kui_custom_layout(Callbacks.make(a, KuiLayouts.KuiLayoutRule, Callbacks.layoutRule, rule), handles(a, kids), kids.size.toLong())
        }
    }) { content() }
}

/** The same, for a layout that holds nothing: it only says how big it is. */
@Composable
fun Layout(modifier: Modifier = Modifier, measurePolicy: MeasurePolicy) = Layout({}, modifier, measurePolicy)

private class Child(val index: Int, override val width: Int, override val height: Int) : Placeable()
private object MeasureScopeInstance : MeasureScope

// ---- a layout that composes what it lays out ---------------------------------------------------------------------

/** What a [SubcomposeLayout]'s rule is written in: it composes a part of its content, and measures what that made. */
interface SubcomposeMeasureScope : MeasureScope {
    /**
     * What [content] is, to be measured: one measurable, whatever [content] emits (several things are laid over each
     * other, as in a Box). The first time a [slotId] is asked for, it is not composed yet and there is nothing to
     * measure: the list is empty, and the layout is run again the frame after, when it is.
     */
    fun subcompose(slotId: Any?, content: @Composable () -> Unit): List<Measurable>
}

private class SubcomposeSlots {
    val contents = androidx.compose.runtime.mutableStateMapOf<Any?, androidx.compose.runtime.MutableState<@Composable () -> Unit>>()
    var composed: List<Any?> = emptyList()
}

/**
 * A layout whose rule composes its content as it goes — `subcompose(slot) { … }` — so that what one part is can
 * depend on how big another came out:
 *
 * ```
 * SubcomposeLayout { constraints ->
 *     val title = subcompose("title") { Title() }.map { it.measure(constraints) }
 *     val height = title.maxOfOrNull { it.height } ?: 0
 *     val body = subcompose("body") { Body(under = height) }.map { it.measure(constraints) }
 *     layout(constraints.maxWidth, constraints.maxHeight) { title.forEach { it.place(0, 0) }; body.forEach { it.place(0, height) } }
 * }
 * ```
 *
 * Compose composes a slot inside the measure pass; here it is composed in the frame after it is first asked for,
 * so a layout settles over as many frames as it has parts that depend on one another.
 */
@Composable
fun SubcomposeLayout(modifier: Modifier = Modifier, measurePolicy: SubcomposeMeasureScope.(Constraints) -> MeasureResult) {
    val slots = androidx.compose.runtime.remember { SubcomposeSlots() }
    val composed = slots.contents.keys.toList()
    slots.composed = composed
    Layout({
        for (id in composed) androidx.compose.runtime.key(id) { Box { slots.contents[id]?.value?.invoke() } }
    }, modifier) { measurables, constraints ->
        val used = HashSet<Any?>()
        val scope = object : SubcomposeMeasureScope {
            override fun subcompose(slotId: Any?, content: @Composable () -> Unit): List<Measurable> {
                used += slotId
                val held = slots.contents[slotId]
                if (held == null) {
                    slots.contents[slotId] = androidx.compose.runtime.mutableStateOf(content, androidx.compose.runtime.referentialEqualityPolicy())
                    return emptyList()
                }
                if (held.value !== content) held.value = content
                val at = slots.composed.indexOf(slotId)
                return if (at in measurables.indices) listOf(measurables[at]) else emptyList()
            }
        }
        val result = scope.measurePolicy(constraints)
        // What the rule no longer asks for goes.
        slots.contents.keys.filter { it !in used }.forEach { slots.contents.remove(it) }
        result
    }
}

