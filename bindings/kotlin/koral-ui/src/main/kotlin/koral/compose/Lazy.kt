package koral.compose

import androidx.compose.runtime.Composable
import androidx.compose.runtime.Composition
import androidx.compose.runtime.CompositionContext
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.remember
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
 * A vertical list that composes only the items in view (and a screen either side), as Compose's does: a
 * million items cost what a screenful does. koral-ui lays every item out [itemHeight] tall.
 *
 * ```
 * LazyColumn(itemHeight = 32.dp) {
 *     item { Text("Header") }
 *     items(names, key = { it }) { name -> Text(name) }
 * }
 * ```
 *
 * An item keeps what it remembers while it stays in that range; one that leaves it is disposed, and
 * composed afresh when it comes back. A key keeps an item's state with the item when the list changes.
 */
@Composable
fun LazyColumn(modifier: Modifier = Modifier, itemHeight: Dp = 40.dp, content: LazyListScope.() -> Unit) {
    val context = rememberCompositionContext()
    val ui = LocalComposeUi.current
    val items = remember { LazyItems(ui, context) }
    DisposableEffect(items) {
        ui.lazyLists += items
        onDispose { items.dispose(); ui.lazyLists -= items }
    }
    val intervals = Intervals().apply(content)
    items.intervals = intervals
    Node(modifier, intervals to itemHeight, { node, _ ->
        items.node = node
        Arena.ofConfined().use { a ->
            KuiNative.kui_list_view_builder_with_range(intervals.count.toLong(), itemHeight.value,
                Struct(a, KuiLayouts.KuiItemBuilder).address("build", Callbacks.itemBuilder)
                    .address("user", koral.Handles.put({ i: Long -> items.build(i.toInt()) }))
                    .address("destroy", Callbacks.free).segment,
                Callbacks.make(a, KuiLayouts.KuiRangeAction, Callbacks.range, { f: Long, l: Long -> items.keep(f.toInt(), l.toInt()) }))
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
    private val items = HashMap<Any, Item>()

    /** Item [index]'s widget, composing it first if it is new or its content changed: a new handle. */
    fun build(index: Int): MemorySegment {
        if (index >= intervals.count) return MemorySegment.NULL
        val interval = intervals.at(index)
        val key = intervals.keyOf(index)
        val item = items.getOrPut(key) {
            val root = UiNode().apply { make = { _, kids -> stack(kids, Alignment.TopStart) } }
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
