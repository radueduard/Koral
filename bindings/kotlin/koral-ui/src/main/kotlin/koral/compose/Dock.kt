package koral.compose

import androidx.compose.runtime.Composable
import androidx.compose.runtime.key
import androidx.compose.runtime.remember
import java.lang.foreign.Arena
import java.lang.foreign.MemorySegment
import java.lang.ref.Cleaner
import koral.ui.DockSide
import koral.ui.interop.KuiLayouts
import koral.ui.interop.KuiNative

private val dockCleaner: Cleaner = Cleaner.create()

/**
 * kui::DockLayout: how a dock space is arranged — which panels are tabbed together, how the space is split,
 * what floats and what is closed. Dragging changes it; [save] and [load] carry it between runs.
 *
 * ```
 * val layout = rememberDockLayout {
 *     dock("scene")
 *     dock("inspector", DockSide.eRight, "scene", 0.25f)
 *     dock("log", DockSide.eBottom, "scene", 0.3f)
 * }
 * ```
 */
class DockLayout {
    internal val native: MemorySegment = KuiNative.kui_dock_layout_new().also { koral.checkLastError() }

    init {
        val handle = native
        dockCleaner.register(this) { KuiNative.kui_dock_layout_release(handle) }
    }

    /** Puts [panel] on [side] of [relativeTo] (of the whole space when null); eCenter makes it a tab of that group. */
    fun dock(panel: String, side: DockSide = DockSide.eCenter, relativeTo: String? = null, fraction: Float = 0.25f) = apply {
        KuiNative.kui_dock_layout_dock(native, panel, side.value, relativeTo, fraction)
    }
    /** Floats [panel] over the dock space. */
    fun float(panel: String, rect: Rect) = apply { Arena.ofConfined().use { a -> KuiNative.kui_dock_layout_float(native, panel, rect.native(a)) } }
    /** Gives [panel] a window of its own, where the dock space can open windows. */
    fun popOut(panel: String, width: Float = 480f, height: Float = 360f) = apply {
        Arena.ofConfined().use { a -> KuiNative.kui_dock_layout_pop_out(native, panel, vec2(a, width, height)) }
    }
    fun close(panel: String) = KuiNative.kui_dock_layout_close(native, panel)
    fun open(panel: String) = KuiNative.kui_dock_layout_open(native, panel)
    fun activate(panel: String) = KuiNative.kui_dock_layout_activate(native, panel)
    fun isOpen(panel: String): Boolean = KuiNative.kui_dock_layout_is_open(native, panel)
    fun isFloating(panel: String): Boolean = KuiNative.kui_dock_layout_is_floating(native, panel)
    /** The arrangement as text, for a settings file. */
    fun save(): String = KuiNative.kui_dock_layout_save(native)
    /** Takes an arrangement [save] gave. False (and unchanged) when it is not one. */
    fun load(text: String): Boolean = KuiNative.kui_dock_layout_load(native, text)
}

/** A [DockLayout] kept across recompositions, arranged by [arrange] the first time. */
@Composable
fun rememberDockLayout(arrange: DockLayout.() -> Unit = {}): DockLayout = remember { DockLayout().apply(arrange) }

/** What a [DockSpace]'s content says: its panels. */
interface DockScope {
    /** A panel: one tab, known to the layout by [id], showing [content]. */
    fun panel(id: String, title: String, closable: Boolean = true, content: @Composable () -> Unit)
}

private data class PanelMeta(val id: String, val title: String, val closable: Boolean)

/**
 * A space of docked panels, filling what it is given. Drag a tab onto another group's bar to tab it there,
 * near an edge to split, into the middle to float it, and out of the window to give it a window of its own.
 * A panel keeps its state wherever it goes.
 *
 * ```
 * DockSpace(layout) {
 *     panel("scene", "Scene", closable = false) { SceneView() }
 *     panel("inspector", "Inspector") { Inspector() }
 * }
 * ```
 */
@Composable
fun DockSpace(layout: DockLayout, modifier: Modifier = Modifier, multiViewport: Boolean = true, onPanelClosed: ((id: String) -> Unit)? = null,
              onLayoutChanged: (() -> Unit)? = null, panels: DockScope.() -> Unit) {
    val list = ArrayList<Pair<PanelMeta, @Composable () -> Unit>>()
    object : DockScope {
        override fun panel(id: String, title: String, closable: Boolean, content: @Composable () -> Unit) {
            list += PanelMeta(id, title, closable) to content
        }
    }.panels()
    val metas = list.map { it.first }
    Node(modifier, Triple(layout, metas, multiViewport), { node, kids ->
        Arena.ofConfined().use { a ->
            val size = KuiLayouts.KuiDockPanel.byteSize()
            val array = a.allocate(KuiLayouts.KuiDockPanel, maxOf(1, metas.size).toLong())
            metas.forEachIndexed { i, meta ->
                Struct(array.asSlice(i * size, size), KuiLayouts.KuiDockPanel).address("id", a.allocateFrom(meta.id))
                    .address("title", a.allocateFrom(meta.title)).address("content", kids.getOrElse(i) { MemorySegment.NULL })
                    .bool("fixed", !meta.closable)
            }
            val options = Struct(a, KuiLayouts.KuiDockOptions).bool("single_viewport", !multiViewport)
                .struct("on_closed", Callbacks.make(a, KuiLayouts.KuiTextAction, Callbacks.textAction, { id: String -> node.onText?.invoke(id) }))
                .struct("on_changed", Callbacks.make(a, KuiLayouts.KuiAction, Callbacks.action, { node.onClick?.invoke() }))
            KuiNative.kui_dock_space(layout.native, array, metas.size.toLong(), options.segment)
        }
    }, update = { onText = onPanelClosed; onClick = onLayoutChanged }) {
        // One node per panel, in order: the panel's content, whatever it is, as one widget.
        for ((meta, content) in list) key(meta.id) {
            Node(Modifier, Unit, { _, kids -> stack(kids, Alignment.TopStart) }) { content() }
        }
    }
}
