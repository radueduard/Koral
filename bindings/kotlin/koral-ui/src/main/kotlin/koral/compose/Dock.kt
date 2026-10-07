package koral.compose

import androidx.compose.runtime.Composable
import androidx.compose.runtime.key
import androidx.compose.runtime.remember
import java.lang.foreign.Arena
import java.lang.foreign.MemorySegment
import java.lang.ref.Cleaner
import koral.ui.interop.KuiLayouts
import koral.ui.interop.KuiNative

private val dockCleaner: Cleaner = Cleaner.create()

/**
 * The five places a panel can be docked — as the tool windows of the JetBrains IDEs are: the middle, which is
 * whatever the others leave (the scene, where nothing is docked in it); down the left and down the right of the
 * space, each of which has levels, one over the other; and the two parts of the bottom, which runs under both.
 */
enum class DockArea(val value: Int) { Left(0), Right(1), BottomLeft(2), BottomRight(3), Center(4) }

/**
 * kui::DockLayout: how a dock space is arranged — which panels are in which area, which of them are open, how
 * big the areas are, what floats and what is closed. Dragging changes it; [save] and [load] carry it between runs.
 *
 * ```
 * val layout = rememberDockLayout {
 *     dock("project", DockArea.Left)
 *     dock("structure", DockArea.Left, level = 1)   // a second level of the left side, under the first
 *     dock("inspector", DockArea.Right)
 *     dock("log", DockArea.BottomLeft)
 * }
 * ```
 */
class DockLayout {
    internal val native: MemorySegment = KuiNative.kui_dock_layout_new().also { koral.checkLastError() }

    init {
        val handle = native
        dockCleaner.register(this) { KuiNative.kui_dock_layout_release(handle) }
    }

    /**
     * Floats [panel] over the dock space at ([x], [y]), as big as what it shows: its content says how big it
     * wants to be, and the float is that (and its bar and frame, when it has them). It follows its content when
     * that changes size, and has no corner to resize it by.
     */
    fun float(panel: String, x: Float = 40f, y: Float = 40f) = apply {
        Arena.ofConfined().use { a -> KuiNative.kui_dock_layout_float_at(native, panel, vec2(a, x, y)) }
    }
    /** Floats [panel] over the dock space, in [rect]: as big as that, whatever it shows. */
    fun float(panel: String, rect: Rect) = apply { Arena.ofConfined().use { a -> KuiNative.kui_dock_layout_float(native, panel, rect.native(a)) } }
    /** Floats [panel] outside the window, over the desktop, where the dock space can (see [DockSpace]'s multiViewport). */
    fun popOut(panel: String, width: Float = 480f, height: Float = 360f) = apply {
        Arena.ofConfined().use { a -> KuiNative.kui_dock_layout_pop_out(native, panel, vec2(a, width, height)) }
    }
    fun close(panel: String) = KuiNative.kui_dock_layout_close(native, panel)
    fun open(panel: String) = KuiNative.kui_dock_layout_open(native, panel)
    /** Shows [panel] in its area, in front of whatever of that area was shown. */
    fun activate(panel: String) = KuiNative.kui_dock_layout_activate(native, panel)
    /** Folds [panel]'s area away, when it is the one shown there: its button stays in the stripe. */
    fun hide(panel: String) = KuiNative.kui_dock_layout_hide(native, panel)
    /** Whether [panel] is to be seen: floating, or the one its area shows — and not closed. */
    fun isShown(panel: String): Boolean = KuiNative.kui_dock_layout_is_shown(native, panel)
    /**
     * Docks [panel] in [area] — the middle, the left, the right, or the bottom's left or right part — and opens it
     * there. The left and the right have levels, one over the other, each showing one of its panels: [level] says
     * which, counted from the top (one that is not there yet is made); a line between their buttons in the stripe
     * separates one level's from the next's.
     */
    fun dock(panel: String, area: DockArea = DockArea.Center, level: Int = 0) = apply {
        KuiNative.kui_dock_layout_dock_in(native, panel, area.value, level)
    }
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

/**
 * The sizes a [DockSpace] is drawn and handled with. Every one defaults to what it always was.
 *
 * ```
 * DockSpace(layout, style = DockStyle(titleBarHeight = 34.dp, radius = 14.dp)) { ... }
 * ```
 */
data class DockStyle(
    /** A panel's title bar, and the row of tabs in the middle. */
    val titleBarHeight: Dp = 28.dp,
    /** A stripe of buttons down a side, before [DockSpace]'s stripeGap is added to it. */
    val stripeWidth: Dp = 38.dp,
    /** A stripe's square buttons. They shrink when a stripe has more than fit. */
    val buttonSize: Dp = 30.dp,
    /** Between one button of a stripe and the next. */
    val buttonGap: Dp = 4.dp,
    /** Between the buttons of one level of a side and the next, where the line is. */
    val separatorGap: Dp = 9.dp,
    /** Either side of a title, in a title bar and in a tab. */
    val tabPadding: Dp = 10.dp,
    /** The corner of a float that resizes it. */
    val resizeGrip: Dp = 14.dp,
    /** The least a float can be dragged down to, each way. */
    val minFloatSize: Dp = 120.dp,
    /** The least an area round the edge can be dragged down to. */
    val minAreaSize: Dp = 24.dp,
    /** The corners of a docked panel. */
    val radius: Dp = 10.dp,
    /** How much of the space, from its left, right and bottom edges, docks a panel dropped there on that side. */
    val edgeDropMargin: Float = 0.15f,
    /** How much of the middle, about its centre, docks a panel dropped there in the middle. */
    val centerDropSize: Float = 0.30f,
    /** From how far down a side's panel a drop goes under it, as a level of its own, rather than joining it. */
    val underDropStart: Float = 0.60f,
) {
    companion object { val Default = DockStyle() }
}

/** What a [DockSpace]'s content says: its panels. */
interface DockScope {
    /**
     * A panel, known to the layout by [id], showing [content]. Docked, it is a square button in the strip down its
     * group's left side, showing [icon] (`Icons.Filled.Tune`, or any [ImageVector]) in the colour the button's state
     * gives it — or, with none, the first letter of [title]. Floating, it has
     * a title bar instead: a sliver along its top with [title] on the left and its buttons on the right.
     *
     * [dockable] false: it never docks — it floats on its own, and nothing can be docked into it.
     * [showTitleBar] false: floating on its own it is only its content, with no bar, frame or surface, at the
     * size the layout floats it at; it is moved by dragging the content — its padding, and whatever else of
     * it takes no press (a Button in it is still a button).
     *
     * [titleBar]: what its title bar shows between its title, at the start, and its buttons, at the end — a
     * toolbar, say. It is given that room's width, and the bar is as tall as it wants. Where it takes no press,
     * pressing it picks the panel up, as the rest of the bar does. None: nothing there.
     *
     * ```
     * panel("viewport", "Viewport", titleBar = {
     *     Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.Center) {
     *         IconButton(onClick = play) { Icon(Icons.Filled.PlayArrow, "Play") }
     *     }
     * }) { Viewport() }
     * ```
     */
    fun panel(id: String, title: String, closable: Boolean = true, dockable: Boolean = true, showTitleBar: Boolean = true,
              icon: ImageVector? = null, titleBar: (@Composable () -> Unit)? = null, content: @Composable () -> Unit)
}

private data class PanelMeta(val id: String, val title: String, val closable: Boolean, val dockable: Boolean, val showTitleBar: Boolean,
                             val icon: ImageVector?, val hasTitleBar: Boolean)

/**
 * A space of docked panels, filling what it is given, arranged as the tool windows of the JetBrains IDEs are.
 *
 * Down each side is a stripe of square buttons, one glyph each: a docked panel's. A button opens its panel, in
 * front of whichever of its area was open; the button of the panel that is open folds the area away. The buttons
 * from the top of a stripe open down that side — which can be in several parts, one over the other, a line
 * between their buttons; those at its foot open along the bottom, which runs under both sides: the left stripe's
 * in its left part, the right's in its right. The middle is whatever they leave: empty, it shows the scene and
 * lets the pointer through. An open panel has a title bar: its title, and buttons that fold it away and close it.
 *
 * Drag a panel's button, or its title, onto a stripe to put it among those buttons (between a part's: into that
 * part; on the line between two parts, or under the last: a part of its own; at the foot: that end of the bottom);
 * onto a docked panel to join its group, in front of it — or, down a side and over the lower part of the panel,
 * to dock under it as a part of its own; into a margin of the space (within 15% of its left, right or bottom) to
 * dock it on that side; into the middle (onto its title bar, or the 30% about its centre) to dock it there, a tab
 * among whatever else is in the middle; anywhere else to
 * float it; and out of the window to float it over the desktop. A float is moved by its title bar, in the window
 * and out of it. A panel keeps its state wherever it goes.
 *
 * [style]: every other size it is drawn and handled with — title bar height, button size, corner radius, how much of
 * an edge takes a drop: see [DockStyle].
 *
 * [stripeGap]: space added round a stripe's buttons, half on each side: they are as far from the window's edge as
 * from the panels beside them.
 *
 * [gap]: the space between two areas that are next to each other — which is also what is dragged to resize
 * them: over it the pointer turns into the arrows that say which way.
 *
 * [multiViewport]: whether panels can leave the window. Out of it they float in one see-through window that
 * covers every monitor, stays above everything and lets the pointer through wherever no panel is. A float is
 * drawn there while it is moved; put down, it is in the space when all of it is inside the window. Where
 * the display cannot show such a window, panels stay in the space.
 *
 * ```
 * DockSpace(layout) {
 *     panel("scene", "Scene", closable = false) { SceneView() }
 *     panel("inspector", "Inspector") { Inspector() }
 * }
 * ```
 */
@Composable
fun DockSpace(layout: DockLayout, modifier: Modifier = Modifier, multiViewport: Boolean = true, gap: Dp = 6.dp, stripeGap: Dp = 3.dp, style: DockStyle = DockStyle.Default,
              onPanelClosed: ((id: String) -> Unit)? = null, onLayoutChanged: (() -> Unit)? = null, panels: DockScope.() -> Unit) {
    val list = ArrayList<Triple<PanelMeta, @Composable () -> Unit, (@Composable () -> Unit)?>>()
    object : DockScope {
        override fun panel(id: String, title: String, closable: Boolean, dockable: Boolean, showTitleBar: Boolean, icon: ImageVector?,
                           titleBar: (@Composable () -> Unit)?, content: @Composable () -> Unit) {
            list += Triple(PanelMeta(id, title, closable, dockable, showTitleBar, icon, titleBar != null), content, titleBar)
        }
    }.panels()
    val metas = list.map { it.first }
    Node(modifier, listOf(layout, metas, multiViewport, gap, stripeGap, style), { node, kids ->
        Arena.ofConfined().use { a ->
            val size = KuiLayouts.KuiDockPanel.byteSize()
            val array = a.allocate(KuiLayouts.KuiDockPanel, maxOf(1, metas.size).toLong())
            // The contents first, one a panel; then the title bars', for the panels that have one.
            var bars = metas.size
            metas.forEachIndexed { i, meta ->
                Struct(array.asSlice(i * size, size), KuiLayouts.KuiDockPanel).address("id", a.allocateFrom(meta.id))
                    .address("title", a.allocateFrom(meta.title)).address("content", kids.getOrElse(i) { MemorySegment.NULL })
                    .bool("fixed", !meta.closable).bool("undockable", !meta.dockable).bool("no_title_bar", !meta.showTitleBar)
                    .address("icon", meta.icon?.handle ?: MemorySegment.NULL)
                    .address("title_bar", if (meta.hasTitleBar) kids.getOrElse(bars++) { MemorySegment.NULL } else MemorySegment.NULL)
            }
            val options = Struct(a, KuiLayouts.KuiDockOptions).bool("single_viewport", !multiViewport).float("gap", gap.value).float("stripe_gap", stripeGap.value)
                .floats("style", style.titleBarHeight.value, style.stripeWidth.value, style.buttonSize.value, style.buttonGap.value,
                        style.separatorGap.value, style.tabPadding.value, style.resizeGrip.value, style.minFloatSize.value,
                        style.minAreaSize.value, style.radius.value, style.edgeDropMargin, style.centerDropSize, style.underDropStart)
                .struct("on_closed", Callbacks.make(a, KuiLayouts.KuiTextAction, Callbacks.textAction, { id: String -> node.onText?.invoke(id) }))
                .struct("on_changed", Callbacks.make(a, KuiLayouts.KuiAction, Callbacks.action, { node.onClick?.invoke() }))
            KuiNative.kui_dock_space(layout.native, array, metas.size.toLong(), options.segment)
        }
    }, update = { onText = onPanelClosed; onClick = onLayoutChanged }) {
        // One node per panel, in order: the panel's content, whatever it is, as one widget. Then one per title bar.
        for ((meta, content) in list) key(meta.id) {
            Node(Modifier, Unit, { _, kids -> stack(kids, Alignment.TopStart) }) { content() }
        }
        for ((meta, _, titleBar) in list) if (titleBar != null) key(meta.id to "titleBar") {
            Node(Modifier, Unit, { _, kids -> stack(kids, Alignment.TopStart) }) { titleBar() }
        }
    }
}
