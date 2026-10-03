package koral.compose

import androidx.compose.runtime.AbstractApplier
import androidx.compose.runtime.BroadcastFrameClock
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Composition
import androidx.compose.runtime.Recomposer
import androidx.compose.runtime.compositionLocalOf
import androidx.compose.runtime.snapshots.Snapshot
import java.lang.foreign.Arena
import java.lang.foreign.MemorySegment
import java.lang.foreign.SegmentAllocator
import java.util.ArrayDeque
import kotlin.coroutines.CoroutineContext
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch
import koral.GraphPass
import koral.Ownership
import koral.Scene
import koral.interop.KoralNative
import koral.ui.interop.KuiLayouts
import koral.ui.interop.KuiNative

/** The colour text and icons take when they are not given one: a Button's content takes its own. */
val LocalContentColor = compositionLocalOf { Color.Unspecified }

/** The colours, shapes and type the built-in controls draw with: koral-ui's kui::Theme. */
data class Theme(
    val background: Color, val surface: Color, val surfaceHover: Color, val surfacePressed: Color,
    val primary: Color, val primaryHover: Color, val primaryPressed: Color, val onPrimary: Color,
    val text: Color, val textMuted: Color, val border: Color, val focus: Color,
    val radius: Dp, val controlHeight: Dp, val fontSize: TextUnit,
) {
    internal fun native(a: SegmentAllocator): MemorySegment = Struct(a.allocate(KuiLayouts.KuiTheme).also { KuiNative.kui_theme_dark(it) }, KuiLayouts.KuiTheme)
        .color("background", background).color("surface", surface).color("surface_hover", surfaceHover)
        .color("surface_pressed", surfacePressed).color("primary", primary).color("primary_hover", primaryHover)
        .color("primary_pressed", primaryPressed).color("on_primary", onPrimary).color("text", text)
        .color("text_muted", textMuted).color("border", border).color("focus", focus)
        .float("radius", radius.value).float("control_height", controlHeight.value)
        .float("text_style.size", fontSize.value).color("text_style.color", text)
        .segment

    companion object {
        val Dark: Theme by lazy { of { KuiNative.kui_theme_dark(it) } }
        val Light: Theme by lazy { of { KuiNative.kui_theme_light(it) } }

        private fun of(fill: (MemorySegment) -> Unit): Theme = Arena.ofConfined().use { a ->
            val t = a.allocate(KuiLayouts.KuiTheme)
            fill(t)
            fun c(name: String): Color {
                val o = KuiLayouts.KuiTheme.byteOffset(java.lang.foreign.MemoryLayout.PathElement.groupElement(name))
                fun f(i: Int) = t.get(java.lang.foreign.ValueLayout.JAVA_FLOAT, o + i * 4L)
                return Color(f(0), f(1), f(2), f(3))
            }
            fun f(name: String) = t.get(java.lang.foreign.ValueLayout.JAVA_FLOAT, KuiLayouts.KuiTheme.byteOffset(java.lang.foreign.MemoryLayout.PathElement.groupElement(name)))
            Theme(c("background"), c("surface"), c("surface_hover"), c("surface_pressed"), c("primary"), c("primary_hover"),
                  c("primary_pressed"), c("on_primary"), c("text"), c("text_muted"), c("border"), c("focus"),
                  f("radius").dp, f("control_height").dp,
                  t.get(java.lang.foreign.ValueLayout.JAVA_FLOAT, KuiLayouts.KuiTheme.byteOffset(
                      java.lang.foreign.MemoryLayout.PathElement.groupElement("text_style"),
                      java.lang.foreign.MemoryLayout.PathElement.groupElement("size"))).sp)
        }
    }
}

/**
 * A node of the composition: what one composable emits. It turns itself into a koral-ui widget, and
 * keeps that widget until something about it changes — so an unchanged subtree is handed to koral-ui as
 * the very widget it already has, which it does not even look at.
 */
class UiNode internal constructor() {
    internal val id = nextId++
    internal val children = mutableListOf<UiNode>()
    internal var parent: UiNode? = null
    private var cached: MemorySegment = MemorySegment.NULL

    /** Makes the widget from the children's (borrowed handles); returns a new handle. */
    internal var make: (UiNode, List<MemorySegment>) -> MemorySegment = { _, _ -> KuiNative.kui_sized_box(0f, 0f, MemorySegment.NULL) }
        set(value) { field = value; invalidate() }

    var modifier: Modifier = Modifier
        internal set(value) {
            // A new click lambda each recomposition is the same modifier: the callback forwards to the latest.
            clickHandlers = value.elements().filterIsInstance<ClickableElement>().map { it.onClick }
            dragSources = value.elements().filterIsInstance<DragSourceElement>()
            dropTargets = value.elements().filterIsInstance<DropTargetElement>()
            if (field != value) { field = value; invalidate() }
        }
    internal var clickHandlers: List<() -> Unit> = emptyList()
    internal var dragSources: List<DragSourceElement> = emptyList()
    internal var dropTargets: List<DropTargetElement> = emptyList()

    // What the native callbacks forward to: changing them rebuilds nothing.
    internal var onClick: (() -> Unit)? = null
    internal var onBool: ((Boolean) -> Unit)? = null
    internal var onFloat: ((Float) -> Unit)? = null
    internal var onText: ((String) -> Unit)? = null
    internal var onSubmit: ((String) -> Unit)? = null
    internal var onDispose: (() -> Unit)? = null

    internal fun invalidate() {
        var node: UiNode? = this
        while (node != null) {
            node.release()
            node = node.parent
        }
    }

    private fun release() {
        if (cached != MemorySegment.NULL) KuiNative.kui_widget_release(cached)
        cached = MemorySegment.NULL
    }

    internal fun dispose() {
        onDispose?.invoke()
        release()
        children.forEach { it.dispose() }
    }

    /** How many widgets it has made: a new one means its parent's, or the Ui's root, is new too. */
    internal var builds = 0L
        private set

    /** Its widget: the one made before, if nothing changed since. Borrowed: the node keeps it. */
    internal fun widget(): MemorySegment {
        if (cached != MemorySegment.NULL) return cached
        val temporaries = mutableListOf<MemorySegment>()
        try {
            val kids = children.map { child ->
                // What only a parent can do with a child — share a Row's space, align it in a Box — wraps it here.
                var h = child.widget()
                child.modifier.elements().filterIsInstance<AlignElement>().lastOrNull()?.let { align ->
                    h = Arena.ofConfined().use { a -> KuiNative.kui_stack_align(Struct(a, KuiLayouts.KuiAlignment)
                        .float("x", align.alignment.horizontal).float("y", align.alignment.vertical).segment, h) }
                    temporaries += h
                }
                child.modifier.elements().filterIsInstance<WeightElement>().lastOrNull()?.let { w ->
                    h = if (w.fill) KuiNative.kui_expanded(h, w.weight) else KuiNative.kui_flexible(h, w.weight)
                    temporaries += h
                }
                KuiNative.kui_widget_set_key(h, "n${child.id}")
                h
            }
            var made = make(this, kids)
            made = applyModifiers(this, made, modifier)
            KuiNative.kui_widget_set_key(made, "n$id")
            cached = made
            builds++
            return made
        } finally {
            temporaries.forEach { KuiNative.kui_widget_release(it) }
        }
    }

    private companion object { var nextId = 1L }
}

/** Wraps [content] in what [modifier] adds, outermost first; returns the new handle, having let [content] go. */
private fun applyModifiers(node: UiNode, content: MemorySegment, modifier: Modifier): MemorySegment {
    var h = content
    val elements = modifier.elements()
    var click = elements.count { it is ClickableElement }
    var source = elements.count { it is DragSourceElement }
    var target = elements.count { it is DropTargetElement }
    for (element in elements.asReversed()) {
        val inner = h
        h = Arena.ofConfined().use { a ->
            when (element) {
                is PaddingElement -> KuiNative.kui_padding(Struct(a, KuiLayouts.KuiEdgeInsets)
                    .floats("left", element.start.value, element.top.value, element.end.value, element.bottom.value).segment, inner)
                is BackgroundElement -> KuiNative.kui_decorated_box(decoration(a, color = element.color, shape = element.shape), inner)
                is BorderElement -> KuiNative.kui_decorated_box(
                    decoration(a, borderWidth = element.width.value, borderColor = element.color, shape = element.shape), inner)
                is SizeElement -> KuiNative.kui_sized_box(given(element.width), given(element.height), inner)
                is FillElement -> KuiNative.kui_constrained_box(Struct(a, KuiLayouts.KuiBoxConstraints)
                    .float("min_width", if (element.width > 0f) Float.POSITIVE_INFINITY else 0f).float("max_width", Float.POSITIVE_INFINITY)
                    .float("min_height", if (element.height > 0f) Float.POSITIVE_INFINITY else 0f).float("max_height", Float.POSITIVE_INFINITY)
                    .segment, inner)
                is ClickableElement -> KuiNative.kui_button_with_child(inner,
                    Callbacks.make(a, KuiLayouts.KuiAction, Callbacks.action,
                        if (element.enabled) (--click).let { i -> { node.clickHandlers.getOrNull(i)?.invoke() } } else null),
                    Struct(a, KuiLayouts.KuiButtonOptions).int("style", koral.ui.ButtonStyle.ePlain.value).float("width", Float.NaN)
                        .bool("has_padding", true).bool("enabled", element.enabled).segment).also { if (!element.enabled) click-- }
                is AlphaElement -> KuiNative.kui_opacity(element.alpha, inner)
                is ClipElement -> KuiNative.kui_clip_rrect(radii(a, element.shape), inner)
                is OffsetElement -> KuiNative.kui_translate(vec2(a, element.x.value, element.y.value), inner)
                is ScrollElement -> KuiNative.kui_scroll_view(inner, if (element.vertical) koral.ui.Axis.eVertical.value else koral.ui.Axis.eHorizontal.value)
                is DragSourceElement -> {
                    val i = --source
                    val data = Struct(a, KuiLayouts.KuiDragData).address("type", a.allocateFrom(element.data.type))
                        .address("text", a.allocateFrom(element.data.payload as? String ?: ""))
                        .address("payload", koral.Handles.put(element.data)).address("destroy", Callbacks.free)
                    val options = Struct(a, KuiLayouts.KuiDraggableOptions).bool("disabled", !element.enabled)
                        .struct("on_drag_start", Callbacks.make(a, KuiLayouts.KuiAction, Callbacks.action, { node.dragSources.getOrNull(i)?.onDragStart?.invoke() }))
                        .struct("on_drag_end", Callbacks.make(a, KuiLayouts.KuiBoolAction, Callbacks.boolAction,
                            { ok: Boolean -> node.dragSources.getOrNull(i)?.onDragEnd?.invoke(ok) }))
                    KuiNative.kui_draggable(data.segment, inner, options.segment)
                }
                is DropTargetElement -> {
                    val i = --target
                    fun drop(call: (DropTargetElement, DragData, Offset) -> Unit) = Callbacks.make(a, KuiLayouts.KuiDropAction, Callbacks.dropAction,
                        { data: DragData, at: Offset -> node.dropTargets.getOrNull(i)?.let { call(it, data, at) } })
                    val options = Struct(a, KuiLayouts.KuiDropTargetOptions)
                        .address("accepts_type", element.type?.let { a.allocateFrom(it) } ?: MemorySegment.NULL)
                        .struct("on_drop", drop { t, d, _ -> t.onDrop(d) })
                        .struct("on_enter", drop { t, d, _ -> t.onEnter?.invoke(d) })
                        .struct("on_move", drop { t, d, at -> t.onMove?.invoke(d, at) })
                        .struct("on_leave", Callbacks.make(a, KuiLayouts.KuiAction, Callbacks.action, { node.dropTargets.getOrNull(i)?.onLeave?.invoke() }))
                    KuiNative.kui_drop_target(options.segment, inner)
                }
                is WeightElement, is AlignElement -> KuiNative.kui_widget_retain(inner)   // the parent's to apply
            }
        }
        KuiNative.kui_widget_release(inner)
    }
    return h
}

private fun given(d: Dp) = if (d.value.isNaN()) -1f else d.value

internal fun decoration(a: SegmentAllocator, color: Color = Color.Transparent, borderWidth: Float = 0f, borderColor: Color = Color.Transparent,
                        shape: Shape? = null): MemorySegment {
    val s = Struct(a, KuiLayouts.KuiDecoration).color("color", color).float("border_width", borderWidth).color("border_color", borderColor)
    s.segment.asSlice(KuiLayouts.KuiDecoration.byteOffset(java.lang.foreign.MemoryLayout.PathElement.groupElement("radius")), KuiLayouts.KuiRadii)
        .copyFrom(radii(a, shape))
    return s.segment
}

internal class UiApplier(root: UiNode) : AbstractApplier<UiNode>(root) {
    override fun insertTopDown(index: Int, instance: UiNode) {}
    override fun insertBottomUp(index: Int, instance: UiNode) {
        current.children.add(index, instance)
        instance.parent = current
        current.invalidate()
    }
    override fun remove(index: Int, count: Int) {
        repeat(count) { current.children.removeAt(index).apply { parent = null; dispose() } }
        current.invalidate()
    }
    override fun move(from: Int, to: Int, count: Int) {
        val moved = (0 until count).map { current.children.removeAt(from) }
        current.children.addAll(if (to > from) to - count else to, moved)
        current.invalidate()
    }
    override fun onClear() {
        root.children.forEach { it.dispose() }
        root.children.clear()
        root.invalidate()
    }
}

/** Runs coroutines when told to, on the thread that tells it: the frame's. */
private class FrameDispatcher : CoroutineDispatcher() {
    private val queue = ArrayDeque<Runnable>()
    override fun dispatch(context: CoroutineContext, block: Runnable) { synchronized(queue) { queue.add(block) } }
    fun pump() {
        while (true) {
            val next = synchronized(queue) { queue.poll() } ?: return
            next.run()
        }
    }
}

/**
 * A Compose interface on koral-ui: the composition, the Recomposer driving it, and the kui::Ui it builds.
 * Usually made by [setContent]; owned by the scene that made it.
 */
class ComposeUi(theme: Theme = Theme.Dark, scale: Float = 1f, content: @Composable () -> Unit) : AutoCloseable {
    internal val ui: MemorySegment = Arena.ofConfined().use { a -> KuiNative.kui_ui_new(MemorySegment.NULL, theme.native(a), scale) }
    private val root = UiNode().apply {
        // Compose's root lays its children over each other, from the top-start, as a Box does.
        make = { _, kids -> stack(kids, Alignment.TopStart) }
    }
    private val dispatcher = FrameDispatcher()
    private val clock = BroadcastFrameClock()
    private val scope = CoroutineScope(dispatcher + clock)
    private val recomposer = Recomposer(scope.coroutineContext)
    private val composition = Composition(UiApplier(root), recomposer)
    private var closed = false
    /** The LazyColumns' item compositions, which a hot reload reaches too. */
    internal val lazyLists: MutableSet<LazyItems> = java.util.Collections.newSetFromMap(java.util.IdentityHashMap())
    // What Canvases read while drawing; told of changes when the frame applies them, on the frame's thread.
    private val drawReads = androidx.compose.runtime.snapshots.SnapshotStateObserver { it() }

    init {
        scope.launch(start = CoroutineStart.UNDISPATCHED) { recomposer.runRecomposeAndApplyChanges() }
        drawReads.start()
        composition.setContent {
            androidx.compose.runtime.CompositionLocalProvider(LocalTheme provides theme, LocalDrawReads provides drawReads,
                                                            LocalComposeUi provides this, content = content)
        }
        Ownership.adopt(this)
        ComposeReload.track(this)
    }

    /** Recomposes what changed, hands koral-ui what that rebuilt, and lets it take the scene's input. */
    fun update() {
        if (closed) return
        Snapshot.sendApplyNotifications()
        dispatcher.pump()
        clock.sendFrame(System.nanoTime())
        dispatcher.pump()
        reportErrors()
        if (rootChanged()) KuiNative.kui_ui_set_root(ui, root.widget())
        KuiNative.kui_ui_update(ui)
        koral.checkLastError()
    }

    private var reported: Throwable? = null

    /** A composition that failed after a hot reload is kept, not thrown: say why, once, and wait for the fix. */
    private fun reportErrors() {
        val error = androidx.compose.runtime.getCurrentCompositionErrors().firstOrNull()?.first
        if (error != null && error !== reported)
            koral.Log.error("[koral.compose] composing failed — fix the code and save again: $error\n${error.stackTraceToString()}")
        reported = error
    }

    private var rootBuilds = -1L
    private fun rootChanged(): Boolean {
        root.widget()
        return (root.builds != rootBuilds).also { rootBuilds = root.builds }
    }

    /**
     * After a hot reload: everything composes again with the new code, keeping what it remembers (see
     * [ComposeReload]), and every widget is made again — a Canvas's lambda is the same object, but draws
     * something new.
     */
    fun reassemble() {
        if (closed) return
        try {
            composition.javaClass.getMethod("invalidateAll").invoke(composition)
        } catch (e: ReflectiveOperationException) {
            koral.Log.warn("[koral.compose] could not recompose after a hot reload: $e")
        }
        root.dispose()
        lazyLists.toList().forEach { it.reassemble() }
        KuiNative.kui_ui_reassemble(ui)
    }


    /** Whether the pointer is over the interface, or text is being typed into it. */
    val wantsPointer: Boolean get() = KuiNative.kui_ui_wants_pointer(ui)
    val wantsKeyboard: Boolean get() = KuiNative.kui_ui_wants_keyboard(ui)

    override fun close() {
        if (closed) return
        closed = true
        ComposeReload.untrack(this)
        lazyLists.toList().forEach { it.dispose() }
        composition.dispose()
        drawReads.stop()
        drawReads.clear()
        recomposer.cancel()
        scope.cancel()
        dispatcher.pump()
        root.dispose()
        KuiNative.kui_ui_destroy(ui)
        Ownership.release(this)
    }
}

/** A frame-graph pass drawing a [ComposeUi] over [target] — the screen by default. */
class UiPass(private val compose: ComposeUi, private val target: String? = null) : GraphPass() {
    override fun addTo(graph: MemorySegment): MemorySegment {
        val pass = KuiNative.kui_graph_add_ui_pass(graph, compose.ui, target)
        koral.checkLastError()
        return pass
    }
}

/**
 * Shows [content] in this scene, as Android's `setContent` does: drawn over what the scene's frame graph
 * already draws, updated every frame before the scene's own update.
 *
 * ```
 * class Menu : Scene() {
 *     override fun initialize() = setContent { Counter() }
 * }
 * ```
 */
fun Scene.setContent(theme: Theme = Theme.Dark, scale: Float = 1f, content: @Composable () -> Unit): ComposeUi {
    val compose = ComposeUi(theme, scale, content)
    graph.add(UiPass(compose))
    beforeUpdate { compose.update() }
    return compose
}

// ---- kui's building blocks, from borrowed child handles ---------------------------------------------------

internal fun handles(a: SegmentAllocator, kids: List<MemorySegment>): MemorySegment {
    val array = a.allocate(java.lang.foreign.ValueLayout.ADDRESS, maxOf(kids.size, 1).toLong())
    kids.forEachIndexed { i, h -> array.setAtIndex(java.lang.foreign.ValueLayout.ADDRESS, i.toLong(), h) }
    return array
}

internal fun stack(kids: List<MemorySegment>, alignment: Alignment): MemorySegment = Arena.ofConfined().use { a ->
    KuiNative.kui_stack(handles(a, kids), kids.size.toLong(), Struct(a, KuiLayouts.KuiAlignment)
        .float("x", alignment.horizontal).float("y", alignment.vertical).segment)
}

@Suppress("unused") private val keepKoralNative = KoralNative
