package koral.compose

import androidx.compose.runtime.Composable
import androidx.compose.runtime.Composition
import androidx.compose.runtime.CompositionContext
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.rememberCompositionContext
import androidx.compose.runtime.staticCompositionLocalOf
import java.lang.foreign.Arena
import java.lang.foreign.MemorySegment
import koral.Log
import koral.ui.interop.KuiLayouts
import koral.ui.interop.KuiNative

/** The interface being composed: what a LazyColumn registers its items with. */
internal val LocalComposeUi = staticCompositionLocalOf<ComposeUi> { error("a LazyColumn outside setContent") }

/** In a LazyColumn's item. */
interface LazyItemScope
internal object LazyItemScopeInstance : LazyItemScope

/** What a LazyColumn's content says: its items, a run at a time. */
interface LazyListScope {
    /** One item. */
    fun item(key: Any? = null, content: @Composable LazyItemScope.() -> Unit)
    /** [count] items, the i-th made by [itemContent] when it comes into view. */
    fun items(count: Int, key: ((index: Int) -> Any)? = null, itemContent: @Composable LazyItemScope.(index: Int) -> Unit)
}

/** An item for each of [items]. */
inline fun <T> LazyListScope.items(items: List<T>, noinline key: ((item: T) -> Any)? = null,
                                   crossinline itemContent: @Composable LazyItemScope.(item: T) -> Unit) =
    items(items.size, key?.let { k -> { i: Int -> k(items[i]) } }) { itemContent(items[it]) }

/** An item for each of [items], with its index. */
inline fun <T> LazyListScope.itemsIndexed(items: List<T>, noinline key: ((index: Int, item: T) -> Any)? = null,
                                          crossinline itemContent: @Composable LazyItemScope.(index: Int, item: T) -> Unit) =
    items(items.size, key?.let { k -> { i: Int -> k(i, items[i]) } }) { itemContent(it, items[it]) }

/**
 * Where a lazy list is, and a way to send it elsewhere — Compose's:
 *
 * ```
 * val state = rememberLazyListState()
 * LazyColumn(state = state) { items(names) { Text(it) } }
 * Text("from ${state.firstVisibleItemIndex}")
 * Button(onClick = { scope.launch { state.scrollToItem(0) } }) { Text("Top") }
 * ```
 */
class LazyListState(firstVisibleItemIndex: Int = 0, firstVisibleItemScrollOffset: Int = 0) {
    /** The first item in view, and how far into it the view starts. */
    var firstVisibleItemIndex by androidx.compose.runtime.mutableIntStateOf(firstVisibleItemIndex)
        private set
    var firstVisibleItemScrollOffset by androidx.compose.runtime.mutableIntStateOf(firstVisibleItemScrollOffset)
        private set
    val canScrollBackward: Boolean get() = firstVisibleItemIndex > 0 || firstVisibleItemScrollOffset > 0

    internal var jumpIndex = firstVisibleItemIndex
    internal var jumpOffset = firstVisibleItemScrollOffset
    // Not what it last was: the list goes where it is told. Told from the start, where it starts elsewhere than at its top.
    internal var jump by androidx.compose.runtime.mutableIntStateOf(if (firstVisibleItemIndex != 0 || firstVisibleItemScrollOffset != 0) ++jumps else 0)

    internal fun at(index: Int, offset: Int) { firstVisibleItemIndex = index; firstVisibleItemScrollOffset = offset }

    /** Puts item [index] first in view, [scrollOffset] into it. */
    @Suppress("RedundantSuspendModifier")
    suspend fun scrollToItem(index: Int, scrollOffset: Int = 0) { jumpIndex = maxOf(index, 0); jumpOffset = scrollOffset; jump = ++jumps }
    /** The same: it goes there at once, not by scrolling through what is between. */
    suspend fun animateScrollToItem(index: Int, scrollOffset: Int = 0) = scrollToItem(index, scrollOffset)

    private companion object { var jumps = 0 }
}

@Composable
fun rememberLazyListState(initialFirstVisibleItemIndex: Int = 0, initialFirstVisibleItemScrollOffset: Int = 0): LazyListState =
    remember { LazyListState(initialFirstVisibleItemIndex, initialFirstVisibleItemScrollOffset) }

/**
 * A list down that composes only the items in view (and a screen either side), as Compose's does: a million items
 * cost what a screenful does, and each is as tall as it is.
 *
 * ```
 * LazyColumn(verticalArrangement = Arrangement.spacedBy(4.dp), contentPadding = PaddingValues(8.dp)) {
 *     item { Text("Header") }
 *     items(names, key = { it }) { name -> Text(name) }
 * }
 * ```
 *
 * An item keeps what it remembers while it stays in that range; one that leaves it is disposed, and composed
 * afresh when it comes back. A key keeps an item's state with the item when the list changes. An item not yet
 * composed is taken to be forty tall until it is; [itemHeight], koral-ui's own, says they are all one height, which
 * a list of very many lays out faster.
 */
@Composable
fun LazyColumn(modifier: Modifier = Modifier, state: LazyListState = rememberLazyListState(), contentPadding: PaddingValues = PaddingValues(0.dp),
               verticalArrangement: Arrangement.Vertical = Arrangement.Top, horizontalAlignment: Alignment.Horizontal = Alignment.Start,
               itemHeight: Dp = Dp.Unspecified, content: LazyListScope.() -> Unit) =
    LazyList(modifier.padding(start = contentPadding.start, end = contentPadding.end), state, vertical = true, contentPadding.top, contentPadding.bottom,
             verticalArrangement.spacing, Alignment(horizontalAlignment.bias, -1f), itemHeight, content)

/** A list across, as [LazyColumn] is one down: only the items in view are composed, each as wide as it is ([itemWidth]: all one width). */
@Composable
fun LazyRow(modifier: Modifier = Modifier, state: LazyListState = rememberLazyListState(), contentPadding: PaddingValues = PaddingValues(0.dp),
            horizontalArrangement: Arrangement.Horizontal = Arrangement.Start, verticalAlignment: Alignment.Vertical = Alignment.Top,
            itemWidth: Dp = Dp.Unspecified, content: LazyListScope.() -> Unit) =
    LazyList(modifier.padding(top = contentPadding.top, bottom = contentPadding.bottom), state, vertical = false, contentPadding.start, contentPadding.end,
             horizontalArrangement.spacing, Alignment(-1f, verticalAlignment.bias), itemWidth, content)

@Composable
private fun LazyList(modifier: Modifier, state: LazyListState, vertical: Boolean, before: Dp, after: Dp, gap: Dp, alignment: Alignment,
                     extent: Dp, content: LazyListScope.() -> Unit) {
    val context = rememberCompositionContext()
    val ui = LocalComposeUi.current
    val items = remember { LazyItems(ui, context) }
    DisposableEffect(items) {
        ui.lazyLists += items
        onDispose { items.dispose(); ui.lazyLists -= items }
    }
    val intervals = Intervals().apply(content)
    items.intervals = intervals
    items.alignment = alignment
    val jump = state.jump
    Node(modifier, listOf(intervals, vertical, before, after, gap, alignment, extent, state, jump), { node, _ ->
        items.node = node
        scratch { a ->
            val options = Struct(a, KuiLayouts.KuiLazyListOptions)
            options.segment.set(java.lang.foreign.ValueLayout.JAVA_LONG, 0L, intervals.count.toLong())
            options.int("axis", if (vertical) 1 else 0)
                .float("item_extent", if (extent.isSpecified) extent.value else 0f).float("estimated_extent", 40f)
                .float("gap", gap.value).float("padding_start", before.value).float("padding_end", after.value)
                .float("jump_offset", state.jumpOffset.toFloat()).int("jump", jump)
            options.segment.set(java.lang.foreign.ValueLayout.JAVA_LONG,
                KuiLayouts.KuiLazyListOptions.byteOffset(java.lang.foreign.MemoryLayout.PathElement.groupElement("jump_index")), state.jumpIndex.toLong())
            options.address("builder.build", Callbacks.itemBuilder)
                .address("builder.user", koral.Handles.put({ i: Long -> items.build(i.toInt()) }))
                .address("builder.destroy", Callbacks.free)
            options.struct("on_range", Callbacks.make(a, KuiLayouts.KuiRangeAction, Callbacks.range, { f: Long, l: Long -> items.keep(f.toInt(), l.toInt()) }))
            options.struct("on_scrolled", Callbacks.make(a, KuiLayouts.KuiIndexAction, Callbacks.index, { i: Long, into: Float -> state.at(i.toInt(), Math.round(into)) }))
            KuiNative.kui_lazy_list(options.segment)
        }
    })
}

/** The runs of items a LazyColumn's content declared. */
internal class Intervals : LazyListScope {
    class Interval(val start: Int, val count: Int, val key: ((Int) -> Any)?, val content: @Composable LazyItemScope.(Int) -> Unit)
    val list = ArrayList<Interval>()
    var count = 0
        private set

    override fun item(key: Any?, content: @Composable LazyItemScope.() -> Unit) =
        items(1, key?.let { k -> { _: Int -> k } }) { content() }

    override fun items(count: Int, key: ((Int) -> Any)?, itemContent: @Composable LazyItemScope.(Int) -> Unit) {
        if (count <= 0) return
        list += Interval(this.count, count, key, itemContent)
        this.count += count
    }

    fun at(index: Int): Interval {
        var lo = 0
        var hi = list.size - 1
        while (lo < hi) {
            val mid = (lo + hi + 1) / 2
            if (list[mid].start <= index) lo = mid else hi = mid - 1
        }
        return list[lo]
    }

    fun keyOf(index: Int): Any {
        val interval = at(index)
        return interval.key?.invoke(index - interval.start) ?: Positional(index)
    }

    data class Positional(val index: Int)
}

/** A LazyColumn's items: each a composition of its own, under the list's, made when koral-ui asks for it. */
internal class LazyItems(private val ui: ComposeUi, private val context: CompositionContext) {
    private class Item(val root: UiNode, val composition: Composition) {
        var index = -1
        var content: (@Composable LazyItemScope.(Int) -> Unit)? = null
        var local = -1
    }

    var intervals = Intervals()
    var node: UiNode? = null
    /** Where an item is put in the room across the list that it does not take. */
    var alignment = Alignment.TopStart
    private val items = HashMap<Any, Item>()

    /** Item [index]'s widget, composing it first if it is new or its content changed: a new handle. */
    fun build(index: Int): MemorySegment {
        if (index >= intervals.count) return MemorySegment.NULL
        val interval = intervals.at(index)
        val key = intervals.keyOf(index)
        val item = items.getOrPut(key) {
            val root = UiNode().apply { make = { _, kids -> stack(kids, alignment) } }
            Item(root, Composition(UiApplier(root), context))
        }
        // An item's change shows as a change of the list: koral-ui builds its items again, this one anew.
        item.root.parent = node
        item.index = index
        val content = interval.content
        val local = index - interval.start
        if (item.content !== content || item.local != local) {
            item.content = content
            item.local = local
            item.composition.setContent { content(LazyItemScopeInstance, local) }
        }
        return KuiNative.kui_widget_retain(item.root.widget())
    }

    /** koral-ui keeps items [first, last): the others' compositions go. */
    fun keep(first: Int, last: Int) {
        val gone = items.filterValues { it.index < first || it.index >= last }
        for ((key, item) in gone) {
            items.remove(key)
            dispose(item)
        }
    }

    fun reassemble() {
        for (item in items.values) {
            try { item.composition.javaClass.getMethod("invalidateAll").invoke(item.composition) }
            catch (e: ReflectiveOperationException) { Log.warn("[koral.compose] could not recompose a list item after a hot reload: $e") }
            item.root.dispose()
        }
    }

    fun dispose() {
        items.values.forEach(::dispose)
        items.clear()
    }

    private fun dispose(item: Item) {
        item.root.parent = null
        item.composition.dispose()
        item.root.dispose()
    }
}

// ---- a grid ----------------------------------------------------------------------------------------------------

/** How many columns a [LazyVerticalGrid] has. */
sealed interface GridCells {
    class Fixed(val count: Int) : GridCells
    /** As many as fit across at [minSize] each, at the least; they share what room is left over. */
    class Adaptive(val minSize: Dp) : GridCells
}

/** What a [LazyVerticalGrid]'s items are said in. */
interface LazyGridScope {
    fun item(key: Any? = null, content: @Composable () -> Unit)
    fun items(count: Int, key: ((index: Int) -> Any)? = null, itemContent: @Composable (index: Int) -> Unit)
}

inline fun <T> LazyGridScope.items(items: List<T>, noinline key: ((item: T) -> Any)? = null, crossinline itemContent: @Composable (item: T) -> Unit) =
    items(items.size, key?.let { k -> { i: Int -> k(items[i]) } }) { itemContent(items[it]) }

/**
 * A grid that scrolls down, composing only the rows in view: so many [columns] across — or as many as fit — each
 * cell a share of the width, each row as tall as its tallest cell ([rowHeight]: all one height).
 */
@Composable
fun LazyVerticalGrid(columns: GridCells, modifier: Modifier = Modifier, state: LazyListState = rememberLazyListState(),
                     contentPadding: PaddingValues = PaddingValues(0.dp), verticalArrangement: Arrangement.Vertical = Arrangement.Top,
                     horizontalArrangement: Arrangement.Horizontal = Arrangement.Start, rowHeight: Dp = Dp.Unspecified, content: LazyGridScope.() -> Unit) {
    val cells = mutableListOf<@Composable () -> Unit>()
    object : LazyGridScope {
        override fun item(key: Any?, content: @Composable () -> Unit) { cells += content }
        override fun items(count: Int, key: ((index: Int) -> Any)?, itemContent: @Composable (index: Int) -> Unit) {
            repeat(count) { i -> cells += { itemContent(i) } }
        }
    }.content()
    @Composable
    fun Rows(across: Int, all: Modifier) = LazyColumn(all, state, contentPadding, verticalArrangement, itemHeight = rowHeight) {
        items((cells.size + across - 1) / across) { row ->
            Row(Modifier.fillMaxWidth(), horizontalArrangement) {
                for (column in 0 until across) {
                    val cell = cells.getOrNull(row * across + column)
                    Box(if (rowHeight.isSpecified) Modifier.weight(1f).fillMaxHeight() else Modifier.weight(1f)) { cell?.invoke() }
                }
            }
        }
    }
    when (columns) {
        is GridCells.Fixed -> Rows(maxOf(columns.count, 1), modifier)
        // As many as fit in the width there turns out to be.
        is GridCells.Adaptive -> BoxWithConstraints(modifier) {
            val gap = horizontalArrangement.spacing.value
            val room = maxWidth.value - contentPadding.start.value - contentPadding.end.value
            val fit = if (room.isFinite()) ((room + gap) / (maxOf(columns.minSize.value, 1f) + gap)).toInt() else 1
            Rows(maxOf(fit, 1), Modifier.fillMaxSize())
        }
    }
}
