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
            val all = value.elements()
            clickHandlers = all.filterIsInstance<ClickableElement>().map { it.onClick }
            dragSources = all.filterIsInstance<DragSourceElement>()
            dropTargets = all.filterIsInstance<DropTargetElement>()
            sizeHandlers = all.filterIsInstance<SizeChangedElement>().map { it.onChanged }
            pointerHandlers = all.filterIsInstance<PointerInputElement>().map { it.scope }
            if (field != value) {
                field = value
                elements = all
                align = all.lastOrNull { it is AlignElement } as AlignElement?
                weight = all.lastOrNull { it is WeightElement } as WeightElement?
                invalidate()
            }
        }
    /** The modifier's elements, outermost first, and the two only a parent can apply: kept, not found again at every rebuild. */
    internal var elements: List<Modifier.Element> = emptyList()
        private set
    private var align: AlignElement? = null
    private var weight: WeightElement? = null
    private val key = "n$id"
    internal var clickHandlers: List<() -> Unit> = emptyList()
    internal var dragSources: List<DragSourceElement> = emptyList()
    internal var dropTargets: List<DropTargetElement> = emptyList()
    internal var sizeHandlers: List<(IntSize) -> Unit> = emptyList()
    internal var pointerHandlers: List<PointerInputScope> = emptyList()

    // What the native callbacks forward to: changing them rebuilds nothing.
    internal var onClick: (() -> Unit)? = null
    internal var onBool: ((Boolean) -> Unit)? = null
    internal var onFloat: ((Float) -> Unit)? = null
    internal var onText: ((String) -> Unit)? = null
    internal var onSubmit: ((String) -> Unit)? = null
    internal var onColor: ((Color) -> Unit)? = null
    internal var onStops: ((List<ColorStop>) -> Unit)? = null
    internal var onDispose: (() -> Unit)? = null

    internal fun invalidate() {
        var node: UiNode? = this
        while (node != null) {
            node.release()
            node = node.parent
        }
    }

    private fun release() {
        if (cached !== MemorySegment.NULL) KuiNative.kui_widget_release(cached)
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
        if (cached !== MemorySegment.NULL) return cached   // the one NULL there is: told apart by which it is, not by comparing addresses
        val temporaries = mutableListOf<MemorySegment>()
        try {
            val kids = children.map { child ->
                // What only a parent can do with a child — share a Row's space, align it in a Box — wraps it here.
                var h = child.widget()
                val own = h
                child.align?.let { align ->
                    h = scratch { a -> KuiNative.kui_stack_align(Struct(a, KuiLayouts.KuiAlignment)
                        .float("x", align.alignment.horizontal).float("y", align.alignment.vertical).segment, h) }
                    temporaries += h
                }
                child.weight?.let { w ->
                    h = if (w.fill) KuiNative.kui_expanded(h, w.weight) else KuiNative.kui_flexible(h, w.weight)
                    temporaries += h
                }
                // The child's own widget has its key from when it was made; what wraps it here needs it too.
                if (h !== own) scratch { a -> KuiNative.kui_widget_set_key_at(h, a.allocateFrom(child.key)) }
                h
            }
            var made = make(this, kids)
            made = applyModifiers(this, made, elements)
            scratch { a -> KuiNative.kui_widget_set_key_at(made, a.allocateFrom(key)) }
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
private fun applyModifiers(node: UiNode, content: MemorySegment, elements: List<Modifier.Element>): MemorySegment {
    if (elements.isEmpty()) return content
    var h = content
    var click = elements.count { it is ClickableElement }
    var source = elements.count { it is DragSourceElement }
    var target = elements.count { it is DropTargetElement }
    var sized = elements.count { it is SizeChangedElement }
    var pointed = elements.count { it is PointerInputElement }
    scratch { a ->
    var index = elements.size - 1
    while (index >= 0) {
        val element = elements[index]
        val inner = h
        // A size round a background round padding — any two of them, in that order — is one box to koral-ui,
        // which lays it out and paints it as the three it stands for.
        var next = index
        val padding = elements[next] as? PaddingElement
        if (padding != null) next--
        val background = if (next >= 0) elements[next] as? BackgroundElement else null
        if (background != null) next--
        val size = if (next >= 0) elements[next] as? SizeElement else null
        if (size != null) next--
        if (index - next >= 2) {
            val options = Struct(a, KuiLayouts.KuiContainerOptions)
                .float("width", size?.let { given(it.width) } ?: -1f).float("height", size?.let { given(it.height) } ?: -1f)
            if (padding != null) options.floats("padding", padding.start.value, padding.top.value, padding.end.value, padding.bottom.value)
            if (background != null) options.struct("decoration", decoration(a, color = background.color, shape = background.shape))
            h = KuiNative.kui_container(options.segment, inner)
            KuiNative.kui_widget_release(inner)
            index = next
            continue
        }
        index--
        h = run {
            when (element) {
                is PaddingElement -> KuiNative.kui_padding(Struct(a, KuiLayouts.KuiEdgeInsets)
                    .floats("left", element.start.value, element.top.value, element.end.value, element.bottom.value).segment, inner)
                is BackgroundElement -> KuiNative.kui_decorated_box(decoration(a, color = element.color, shape = element.shape), inner)
                is BorderElement -> KuiNative.kui_decorated_box(
                    decoration(a, borderWidth = element.width.value, borderColor = element.color, shape = element.shape), inner)
                is SizeElement -> KuiNative.kui_sized_box(given(element.width), given(element.height), inner)
                // A share of the room, where it is not all of it.
                // All of the room there is that way, or a share of it — and, where there is no end to the room, as
                // big as it is of itself, as Compose has it.
                is FillElement -> KuiNative.kui_fractionally_sized_box(element.width.coerceIn(0f, 1f), element.height.coerceIn(0f, 1f), inner)
                is IntrinsicElement -> KuiNative.kui_intrinsic(element.width, element.height, inner)
                is ClickableElement -> KuiNative.kui_button_with_child(inner,
                    Callbacks.make(a, KuiLayouts.KuiAction, Callbacks.action,
                        if (element.enabled) (--click).let { i -> { node.clickHandlers.getOrNull(i)?.invoke() } } else null),
                    Struct(a, KuiLayouts.KuiButtonOptions).int("style", koral.ui.ButtonStyle.ePlain.value).float("width", Float.NaN)
                        .bool("has_padding", true).bool("enabled", element.enabled).segment).also { if (!element.enabled) click-- }
                is SizeChangedElement -> {
                    val i = --sized
                    KuiNative.kui_size_observer(Callbacks.make(a, KuiLayouts.KuiPanAction, Callbacks.sizeAction,
                        { _: Size, pixels: Size -> node.sizeHandlers.getOrNull(i)?.invoke(IntSize(pixels.width.toInt(), pixels.height.toInt())) }), inner)
                }
                is ConstraintsElement -> KuiNative.kui_constrained_box(Struct(a, KuiLayouts.KuiBoxConstraints)
                    .float("min_width", if (element.minWidth.isSpecified) element.minWidth.value else 0f)
                    .float("max_width", if (element.maxWidth.isSpecified) element.maxWidth.value else Float.POSITIVE_INFINITY)
                    .float("min_height", if (element.minHeight.isSpecified) element.minHeight.value else 0f)
                    .float("max_height", if (element.maxHeight.isSpecified) element.maxHeight.value else Float.POSITIVE_INFINITY).segment, inner)
                is WrapElement -> KuiNative.kui_align(Struct(a, KuiLayouts.KuiAlignment)
                    .float("x", element.alignment.horizontal).float("y", element.alignment.vertical).segment, inner)
                is ShadowElement -> KuiNative.kui_decorated_box(Struct(a, KuiLayouts.KuiDecoration)
                    .color("shadow_color", Color(0f, 0f, 0f, 0.35f)).float("shadow_blur", element.elevation.value * 2f)
                    .floats("shadow_offset", 0f, element.elevation.value * 0.5f).struct("radius", radii(a, element.shape)).segment, inner)
                is PointerInputElement -> {
                    val i = --pointed
                    KuiNative.kui_gesture_detector(pointerOptions(a) { node.pointerHandlers.getOrNull(i) }, inner)
                }
                is AlphaElement -> KuiNative.kui_opacity(element.alpha, inner)
                is ClipElement -> KuiNative.kui_clip_rrect(radii(a, element.shape), inner)
                is OffsetElement -> KuiNative.kui_translate(vec2(a, element.x.value, element.y.value), inner)
                is ScrollElement -> {
                    // It says where it is, and goes where its state was told to: made again when the state is told.
                    val state = element.state
                    state.attached = { node.invalidate() }
                    KuiNative.kui_scroll_view_observed(inner, if (element.vertical) koral.ui.Axis.eVertical.value else koral.ui.Axis.eHorizontal.value,
                        Callbacks.make(a, KuiLayouts.KuiPointAction, Callbacks.pointAction,
                            { at: Offset -> state.value = Math.round(at.x); state.maxValue = Math.round(at.y) }), state.jumpTo, state.jump)
                }
                is TransformElement -> {
                    // Grown, then turned, then moved: about the point of the box the origin names.
                    val r = Math.toRadians(element.rotation.toDouble())
                    val cos = Math.cos(r).toFloat(); val sin = Math.sin(r).toFloat()
                    KuiNative.kui_transform_box(Struct(a, KuiLayouts.KuiTransform)
                        .float("a", cos * element.scaleX).float("b", sin * element.scaleX).float("c", -sin * element.scaleY).float("d", cos * element.scaleY)
                        .float("tx", element.translationX).float("ty", element.translationY).segment,
                        Struct(a, KuiLayouts.KuiAlignment).float("x", element.originX * 2f - 1f).float("y", element.originY * 2f - 1f).segment, inner)
                }
                is AspectRatioElement -> KuiNative.kui_aspect_ratio(element.ratio, inner)
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
                is WeightElement, is AlignElement, is FocusRequesterElement -> KuiNative.kui_widget_retain(inner)   // the parent's to apply
            }
        }
        KuiNative.kui_widget_release(inner)
    }
    }
    return h
}

private fun given(d: Dp) = if (d.value.isNaN()) -1f else d.value

internal fun decoration(a: SegmentAllocator, color: Color = Color.Transparent, borderWidth: Float = 0f, borderColor: Color = Color.Transparent,
                        shape: Shape? = null): MemorySegment {
    return Struct(a, KuiLayouts.KuiDecoration).color("color", color).float("border_width", borderWidth).color("border_color", borderColor)
        .struct("radius", radii(a, shape)).segment
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
class ComposeUi(theme: Theme = KoralDarkTheme, scale: Float = 1f, content: @Composable () -> Unit) : AutoCloseable {
    internal val ui: MemorySegment = scratch { a -> KuiNative.kui_ui_new(MemorySegment.NULL, theme.native(a), scale) }
    private val themeState = androidx.compose.runtime.mutableStateOf(theme)

    /**
     * The theme everything is drawn in. Setting another — `ui.theme = Themes.material(dark = false)` — changes
     * the whole interface where it stands: koral-ui's own controls, and what the composables read of the theme.
     */
    var theme: Theme
        get() = themeState.value
        set(value) {
            if (closed || value == themeState.value) return
            themeState.value = value
            scratch { a -> KuiNative.kui_ui_set_theme(ui, value.native(a)) }
        }

    /** Takes the keyboard from whatever has it: a text field being typed into. */
    fun clearFocus() { if (!closed) KuiNative.kui_ui_clear_focus(ui) }
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
            androidx.compose.runtime.CompositionLocalProvider(LocalTheme provides themeState.value, LocalDrawReads provides drawReads,
                                                            LocalComposeUi provides this) { DialogLayer(content) }
        }
        Ownership.adopt(this)
        ComposeReload.track(this)
    }

    /** Recomposes what changed, hands koral-ui what that rebuilt, and lets it take the scene's input. */
    fun update() {
        if (closed) return
        val start = System.nanoTime()
        Snapshot.sendApplyNotifications()
        dispatcher.pump()
        LongPress.poll(start)
        clock.sendFrame(start)
        dispatcher.pump()
        reportErrors()
        val composed = System.nanoTime()
        // Everything that changed is made again in one go, out of one arena.
        if (scratch { _ -> rootChanged() }) KuiNative.kui_ui_set_root(ui, root.widget())
        val made = System.nanoTime()
        KuiNative.kui_ui_update(ui)
        koral.checkLastError()
        timings = Timings((composed - start) / 1e6, (made - composed) / 1e6, (System.nanoTime() - made) / 1e6)
    }

    /**
     * Where the last update's time went, in milliseconds: Compose recomposing what changed; the widgets of what
     * that touched being made again and handed to koral-ui; and koral-ui building, laying out and painting them.
     */
    data class Timings(val recomposeMs: Double = 0.0, val widgetsMs: Double = 0.0, val nativeMs: Double = 0.0)
    var timings = Timings()
        private set

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
fun Scene.setContent(theme: Theme = KoralDarkTheme, scale: Float = 1f, content: @Composable () -> Unit): ComposeUi {
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

internal fun stack(kids: List<MemorySegment>, alignment: Alignment): MemorySegment = scratch { a ->
    KuiNative.kui_stack(handles(a, kids), kids.size.toLong(), Struct(a, KuiLayouts.KuiAlignment)
        .float("x", alignment.horizontal).float("y", alignment.vertical).segment)
}

@Suppress("unused") private val keepKoralNative = KoralNative
