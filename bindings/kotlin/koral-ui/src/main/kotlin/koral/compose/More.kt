package koral.compose

import androidx.compose.runtime.Composable
import androidx.compose.runtime.rememberUpdatedState
import java.lang.foreign.Arena
import java.lang.foreign.MemorySegment
import java.lang.foreign.SegmentAllocator
import java.lang.foreign.ValueLayout
import koral.ui.CrossAxisAlignment
import koral.ui.MainAxisSize
import koral.ui.interop.KuiLayouts
import koral.ui.interop.KuiNative

// The rest of the controls: what a tools interface is made of besides buttons and fields. Each keeps nothing
// of its own — whether a header is open, which tab is in front — as Compose's do: the caller's state says, and
// hears when it should change.

private fun color(a: SegmentAllocator, c: Color): MemorySegment = Struct(a, KuiLayouts.KuiColor).color("r", c).segment

/** The children one above the other, stretched to the same width: what a header or a tree's node has under it. */
private fun column(kids: List<MemorySegment>): MemorySegment = scratch { a ->
    val options = Struct(a, KuiLayouts.KuiFlexOptions).int("main_axis_alignment", 0).int("cross_axis_alignment", CrossAxisAlignment.eStretch.value)
        .int("main_axis_size", MainAxisSize.eMin.value).float("gap", 0f).segment
    KuiNative.kui_column(handles(a, kids), kids.size.toLong(), options)
}

private fun menuItems(a: Arena, items: List<MenuItem>, run: (Int) -> Unit): MemorySegment {
    val size = KuiLayouts.KuiMenuItem.byteSize()
    val array = a.allocate(KuiLayouts.KuiMenuItem, maxOf(1, items.size).toLong())
    items.forEachIndexed { i, item ->
        Struct(array.asSlice(i * size, size), KuiLayouts.KuiMenuItem).address("label", a.allocateFrom(item.label))
            .struct("on_selected", Callbacks.make(a, KuiLayouts.KuiAction, Callbacks.action, { run(i) }))
            .bool("disabled", !item.enabled).bool("separator", item.separator)
    }
    return array
}

/** A line between two things: across a Column, as wide as it — or, [vertical], down a Row that says how tall. */
@Composable
fun Divider(modifier: Modifier = Modifier, thickness: Dp = 1.dp, vertical: Boolean = false) =
    Node(modifier, thickness to vertical, { _, _ -> KuiNative.kui_separator(vertical, thickness.value) })

/** [content] faded, and deaf to the pointer, while not [enabled]. */
@Composable
fun Enabled(enabled: Boolean, modifier: Modifier = Modifier, content: @Composable () -> Unit) =
    Node(modifier, enabled, { _, kids ->
        val child = stack(kids, Alignment.TopStart)
        KuiNative.kui_disabled(child, !enabled).also { KuiNative.kui_widget_release(child) }
    }) { content() }

/** One of several choices: a ring, filled when it is the one [selected], and [label] after it. */
@Composable
fun RadioButton(selected: Boolean, onClick: (() -> Unit)?, modifier: Modifier = Modifier, enabled: Boolean = true, label: String = "") =
    Node(modifier, Triple(selected, label, enabled), { node, _ ->
        scratch { a ->
            whenEnabled(enabled, KuiNative.kui_radio_button(selected, Callbacks.make(a, KuiLayouts.KuiAction, Callbacks.action,
                { node.onClick?.invoke() }), label))
        }
    }, update = { this.onClick = onClick })

/** A line of a list that can be picked: lit under the pointer, in the accent while [selected]. */
@Composable
fun Selectable(label: String, selected: Boolean, onClick: () -> Unit, modifier: Modifier = Modifier) =
    Node(modifier, label to selected, { node, _ ->
        scratch { a ->
            KuiNative.kui_selectable(label, selected, Callbacks.make(a, KuiLayouts.KuiAction, Callbacks.action, { node.onClick?.invoke() }))
        }
    }, update = { this.onClick = onClick })

/**
 * A header that folds what is under it. Pressed, it tells [onExpandedChange] what it should be now; [content]
 * shows under it — and is composed — only while [expanded].
 *
 * ```
 * var open by remember { mutableStateOf(true) }
 * CollapsingHeader("Transform", open, { open = it }) { DragValue(x, { x = it }, label = "X") }
 * ```
 */
@Composable
fun CollapsingHeader(title: String, expanded: Boolean, onExpandedChange: (Boolean) -> Unit, modifier: Modifier = Modifier,
                     content: @Composable ColumnScope.() -> Unit) =
    Node(modifier, title to expanded, { node, kids ->
        scratch { a ->
            val child = if (kids.isEmpty()) MemorySegment.NULL else column(kids)
            KuiNative.kui_collapsing_header(title, expanded, Callbacks.make(a, KuiLayouts.KuiBoolAction, Callbacks.boolAction,
                { v: Boolean -> node.onBool?.invoke(v) }), child).also { if (child != MemorySegment.NULL) KuiNative.kui_widget_release(child) }
        }
    }, update = { onBool = onExpandedChange }) { if (expanded) ColumnScopeInstance.content() }

/**
 * A node of a tree: [label] after an arrow, and [content] — more nodes — under it, further in, while [expanded].
 * A [leaf] has a dot for an arrow and nothing under it. [onClick] hears every press on it.
 *
 * ```
 * TreeNode("Scene", open, { open = it }) {
 *     TreeNode("Camera", leaf = true, selected = picked == "Camera", onClick = { picked = "Camera" })
 * }
 * ```
 */
@Composable
fun TreeNode(label: String, expanded: Boolean = false, onExpandedChange: (Boolean) -> Unit = {}, modifier: Modifier = Modifier,
             leaf: Boolean = false, selected: Boolean = false, onClick: (() -> Unit)? = null, content: @Composable () -> Unit = {}) =
    Node(modifier, listOf(label, expanded, leaf, selected), { node, kids ->
        scratch { a ->
            KuiNative.kui_tree_node(label, expanded, Callbacks.make(a, KuiLayouts.KuiBoolAction, Callbacks.boolAction, { v: Boolean -> node.onBool?.invoke(v) }),
                handles(a, kids), kids.size.toLong(), leaf, selected,
                Callbacks.make(a, KuiLayouts.KuiAction, Callbacks.action, { node.onClick?.invoke() }))
        }
    }, update = { onBool = onExpandedChange; this.onClick = onClick }) { if (expanded && !leaf) content() }

/** A row of titles, the one at [selected] underlined in the accent; pressing another tells [onSelected] its index. */
@Composable
fun TabRow(tabs: List<String>, selected: Int, onSelected: (Int) -> Unit, modifier: Modifier = Modifier) =
    Node(modifier, tabs to selected, { node, _ ->
        scratch { a ->
            val names = a.allocate(ValueLayout.ADDRESS, maxOf(1, tabs.size).toLong())
            tabs.forEachIndexed { i, tab -> names.setAtIndex(ValueLayout.ADDRESS, i.toLong(), a.allocateFrom(tab)) }
            KuiNative.kui_tab_bar(names, tabs.size.toLong(), selected, Callbacks.make(a, KuiLayouts.KuiFloatAction, Callbacks.floatAction,
                { v: Float -> node.onFloat?.invoke(v) }))
        }
    }, update = { onFloat = { onSelected(it.toInt()) } })

/** [content]; while the pointer is over it, [text] shows by the pointer. */
@Composable
fun Tooltip(text: String, modifier: Modifier = Modifier, content: @Composable () -> Unit) =
    Node(modifier, text, { _, kids ->
        val child = stack(kids, Alignment.TopStart)
        KuiNative.kui_tooltip(text, child).also { KuiNative.kui_widget_release(child) }
    }) { content() }

/**
 * [content] and, while [open], [dialog] on a card in the middle of it — over a shade that dims the content and
 * keeps the pointer from it. A press on the shade calls [onDismissRequest]. It covers what it is put round, where
 * [Dialog] — Compose's — covers the whole interface from wherever it is called.
 *
 * ```
 * DialogHost(asking, onDismissRequest = { asking = false }, dialog = { Text("Really?") }) { Panel() }
 * ```
 */
@Composable
fun DialogHost(open: Boolean, onDismissRequest: () -> Unit, modifier: Modifier = Modifier, dialog: @Composable ColumnScope.() -> Unit,
               content: @Composable () -> Unit) =
    Node(modifier, open, { node, kids ->
        scratch { a ->
            KuiNative.kui_modal(open && kids.size > 1, kids.firstOrNull() ?: MemorySegment.NULL, kids.getOrNull(1) ?: MemorySegment.NULL,
                Callbacks.make(a, KuiLayouts.KuiAction, Callbacks.action, { node.onClick?.invoke() }))
        }
    }, update = { this.onClick = onDismissRequest }) {
        Box { content() }
        if (open) Column(verticalArrangement = Arrangement.spacedBy(12.dp)) { dialog() }
    }

/** One menu of a [MenuBar]: its title in the bar, and what opens under it. */
class Menu(val title: String, val items: List<MenuItem>)

/**
 * A row of titles, each opening its menu under itself when pressed.
 *
 * ```
 * MenuBar(listOf(Menu("File", listOf(MenuItem("Open") { open() }, MenuItem.Divider, MenuItem("Quit") { quit() }))))
 * ```
 */
@Composable
fun MenuBar(menus: List<Menu>, modifier: Modifier = Modifier) {
    // What each line runs is looked up when it is picked: the bar need not be made again when only that changed.
    val current = rememberUpdatedState(menus)
    Node(modifier, menus.map { menu -> menu.title to menu.items.map { Triple(it.label, it.enabled, it.separator) } }, { _, _ ->
        scratch { a ->
            val size = KuiLayouts.KuiMenu.byteSize()
            val array = a.allocate(KuiLayouts.KuiMenu, maxOf(1, menus.size).toLong())
            menus.forEachIndexed { m, menu ->
                val items = menuItems(a, menu.items) { i -> current.value.getOrNull(m)?.items?.getOrNull(i)?.onClick?.invoke() }
                val slot = array.asSlice(m * size, size)
                Struct(slot, KuiLayouts.KuiMenu).address("title", a.allocateFrom(menu.title)).address("items", items)
                slot.set(ValueLayout.JAVA_LONG, KuiLayouts.KuiMenu.byteOffset(java.lang.foreign.MemoryLayout.PathElement.groupElement("count")),
                         menu.items.size.toLong())
            }
            KuiNative.kui_menu_bar(array, menus.size.toLong())
        }
    })
}

/**
 * Picks a colour: a square of every saturation and brightness of a hue, a bar of hues under it and, with
 * [alpha], one of how see-through it is; [hex] shows a swatch and the colour as #RRGGBB(AA) under them.
 */
@Composable
fun ColorPicker(color: Color, onColorChange: (Color) -> Unit, modifier: Modifier = Modifier, alpha: Boolean = true, hex: Boolean = true,
                width: Dp = 220.dp) =
    Node(modifier, listOf(color, alpha, hex, width), { node, _ ->
        scratch { a ->
            KuiNative.kui_color_picker(color(a, color), Callbacks.make(a, KuiLayouts.KuiColorAction, Callbacks.colorAction,
                { c: Color -> node.onColor?.invoke(c) }), alpha, hex, width.value)
        }
    }, update = { onColor = onColorChange })

/** A swatch of [color], and [label] after it; pressed, it opens a [ColorPicker] under itself. */
@Composable
fun ColorEdit(color: Color, onColorChange: (Color) -> Unit, modifier: Modifier = Modifier, label: String = "", alpha: Boolean = true) =
    Node(modifier, listOf(color, label, alpha), { node, _ ->
        scratch { a ->
            KuiNative.kui_color_edit(color(a, color), Callbacks.make(a, KuiLayouts.KuiColorAction, Callbacks.colorAction,
                { c: Color -> node.onColor?.invoke(c) }), label, alpha)
        }
    }, update = { onColor = onColorChange })

/** How a [Plot] draws its values. */
enum class PlotKind(internal val value: Int) { Lines(0), Histogram(1) }

/**
 * [values] drawn as a line through them, or as bars — frame times, a histogram. The foot and the top of the
 * plot are [range]'s, or the least and the greatest of the values; [overlay] is written along its top. As wide
 * as it is given room for unless [modifier] says, and [height] tall.
 */
@Composable
fun Plot(values: FloatArray, modifier: Modifier = Modifier, kind: PlotKind = PlotKind.Lines, range: ClosedFloatingPointRange<Float>? = null,
         height: Dp = 60.dp, overlay: String = "", color: Color = Color.Unspecified) {
    // An array is the same array whatever is in it: what it holds is what says whether to draw again.
    val key = listOf(values.contentHashCode(), kind, range, height, overlay, color)
    Node(modifier, key, { _, _ ->
        scratch { a ->
            val data = a.allocate(ValueLayout.JAVA_FLOAT, maxOf(1, values.size).toLong())
            MemorySegment.copy(values, 0, data, ValueLayout.JAVA_FLOAT, 0L, values.size)
            KuiNative.kui_plot(data, values.size.toLong(), kind.value, range?.start ?: Float.NaN, range?.endInclusive ?: Float.NaN,
                vec2(a, -1f, height.value), overlay, color(a, if (color.isSpecified) color else Color.Transparent))
        }
    })
}

/** A column of a [Table]: its title, and how wide — [width], or a share of what the fixed ones leave by [weight]. */
class TableColumn(val title: String, val width: Dp = Dp.Unspecified, val weight: Float = 1f)

/**
 * Rows of cells under columns that line up. [cell] is asked for what row r shows in column c.
 *
 * ```
 * Table(listOf(TableColumn("Name"), TableColumn("Size", width = 80.dp)), files.size) { row, column ->
 *     Text(if (column == 0) files[row].name else files[row].size)
 * }
 * ```
 */
@Composable
fun Table(columns: List<TableColumn>, rowCount: Int, modifier: Modifier = Modifier, header: Boolean = true, striped: Boolean = true,
          borders: Boolean = true, rowHeight: Dp = Dp.Unspecified, cell: @Composable (row: Int, column: Int) -> Unit) =
    Node(modifier, listOf(columns.map { Triple(it.title, it.width, it.weight) }, rowCount, header, striped, borders, rowHeight), { _, kids ->
        scratch { a ->
            val size = KuiLayouts.KuiTableColumn.byteSize()
            val array = a.allocate(KuiLayouts.KuiTableColumn, maxOf(1, columns.size).toLong())
            columns.forEachIndexed { i, column ->
                Struct(array.asSlice(i * size, size), KuiLayouts.KuiTableColumn).address("title", a.allocateFrom(column.title))
                    .float("width", if (column.width.value.isNaN()) -1f else column.width.value).float("flex", column.weight)
            }
            // One child a cell, a row after another: whole rows only, should the two ever disagree.
            val rows = if (columns.isEmpty()) 0 else minOf(rowCount, kids.size / columns.size)
            KuiNative.kui_table(array, columns.size.toLong(), handles(a, kids), rows.toLong(), header, striped, borders,
                if (rowHeight.value.isNaN()) -1f else rowHeight.value)
        }
    }) {
        for (row in 0 until rowCount) for (column in columns.indices) Box { cell(row, column) }
    }

/**
 * A slider that stops only at its steps: a wide rounded track of [steps] places and, in it, a rounded thumb as
 * wide as one of them, at [value] (from 0). Pressed or dragged, it tells [onValueChange] the step under the
 * pointer. [labels] are written in the steps, one each; a step with none shows a dot. As wide as it is given
 * room for, unless [width] says.
 *
 * ```
 * var quality by remember { mutableStateOf(1) }
 * StepSlider(quality, 3, { quality = it }, labels = listOf("Low", "Medium", "High"))
 * ```
 */
@Composable
fun StepSlider(value: Int, steps: Int, onValueChange: (Int) -> Unit, modifier: Modifier = Modifier, labels: List<String> = emptyList(),
               width: Dp = Dp.Unspecified) =
    Node(modifier, listOf(value, steps, labels, width), { node, _ ->
        scratch { a ->
            val names = a.allocate(ValueLayout.ADDRESS, maxOf(1, labels.size).toLong())
            labels.forEachIndexed { i, label -> names.setAtIndex(ValueLayout.ADDRESS, i.toLong(), a.allocateFrom(label)) }
            KuiNative.kui_step_slider(value, steps, Callbacks.make(a, KuiLayouts.KuiFloatAction, Callbacks.floatAction, { v: Float -> node.onFloat?.invoke(v) }),
                names, labels.size.toLong(), if (width.value.isNaN()) -1f else width.value)
        }
    }, update = { onFloat = { onValueChange(it.toInt()) } })

/** A stop of a gradient: where along it, from 0 to 1, and the colour it has there. */
data class ColorStop(val offset: Float, val color: Color)

/**
 * Edits a gradient's [stops]: a bar showing it, and under the bar a handle for each stop. A handle is picked by
 * pressing it and moved by dragging it; a press on the bar where there is none adds a stop there; the right
 * button on a handle, or the Remove button, takes its stop away while more than two are left. With [picker], a
 * [ColorPicker] under the bar changes the colour of the stop that is picked. [onStopsChange] hears the stops,
 * in order, after every change. A gradient has at most eight.
 *
 * ```
 * var stops by remember { mutableStateOf(listOf(ColorStop(0f, Color.Black), ColorStop(1f, Color.White))) }
 * GradientEditor(stops, { stops = it })
 * ```
 */
@Composable
fun GradientEditor(stops: List<ColorStop>, onStopsChange: (List<ColorStop>) -> Unit, modifier: Modifier = Modifier, width: Dp = 260.dp,
                   picker: Boolean = true) =
    Node(modifier, listOf(stops, width, picker), { node, _ ->
        scratch { a ->
            val data = a.allocate(ValueLayout.JAVA_FLOAT, maxOf(1, stops.size * 5).toLong())
            stops.forEachIndexed { i, stop ->
                floatArrayOf(stop.offset, stop.color.red, stop.color.green, stop.color.blue, stop.color.alpha)
                    .forEachIndexed { j, v -> data.setAtIndex(ValueLayout.JAVA_FLOAT, i * 5L + j, v) }
            }
            KuiNative.kui_gradient_editor(data, stops.size.toLong(), Callbacks.make(a, KuiLayouts.KuiStopsAction, Callbacks.stopsAction,
                { now: List<ColorStop> -> node.onStops?.invoke(now) }), width.value, picker)
        }
    }, update = { onStops = onStopsChange })

/**
 * The window's title bar, drawn by the interface in place of the system's: a row along the top of the window,
 * in the theme's background — [leading] (an icon, a [MenuBar]), [title], room that moves the window when
 * dragged and maximizes it when pressed twice, [trailing], and the window's own [buttons]: minimize, maximize
 * or restore, close.
 *
 * Showing one is what takes the system's title bar away. Put it first in a Column that fills the window; the
 * window is still resized by its edges, and snapped by the system.
 *
 * ```
 * Column(Modifier.fillMaxSize()) {
 *     TitleBar("My editor", leading = { MenuBar(menus) })
 *     Screen(Modifier.weight(1f))
 * }
 * ```
 */
@Composable
fun TitleBar(title: String, modifier: Modifier = Modifier, height: Dp = 36.dp, buttons: Boolean = true,
             leading: @Composable RowScope.() -> Unit = {}, trailing: @Composable RowScope.() -> Unit = {}) =
    Node(modifier, listOf(title, height, buttons), { _, kids ->
        // Its two children: what is before the title, and what is after it — a Row each.
        KuiNative.kui_title_bar(title, kids.getOrNull(0) ?: MemorySegment.NULL, kids.getOrNull(1) ?: MemorySegment.NULL, height.value, buttons)
    }) {
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp), content = leading)
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp), content = trailing)
    }

/** What a [StatusBar]'s line is: something said, something to look at, something that went wrong. */
enum class StatusLevel(internal val value: Int) { Info(0), Warning(1), Error(2) }

/**
 * A bar along the foot of a window showing the last thing that was said — [message] after a mark of its
 * [level], in the level's colour — as an editor's does. It is the colour of the window behind the docked
 * panels, and nothing in it is pressed. [trailing] is at its right end: a frame rate, a progress bar.
 *
 * ```
 * Column(Modifier.fillMaxSize()) {
 *     Screen(Modifier.weight(1f))
 *     StatusBar(log.last().text, log.last().level) { Text("60 fps") }
 * }
 * ```
 */
@Composable
fun StatusBar(message: String, level: StatusLevel = StatusLevel.Info, modifier: Modifier = Modifier, height: Dp = 26.dp,
              trailing: @Composable RowScope.() -> Unit = {}) =
    Node(modifier, listOf(message, level, height), { _, kids ->
        scratch { a ->
            val end = if (kids.isEmpty()) MemorySegment.NULL else {
                val options = Struct(a, KuiLayouts.KuiFlexOptions).int("main_axis_alignment", 0).int("cross_axis_alignment", CrossAxisAlignment.eCenter.value)
                    .int("main_axis_size", MainAxisSize.eMin.value).float("gap", 10f).segment
                KuiNative.kui_row(handles(a, kids), kids.size.toLong(), options)
            }
            KuiNative.kui_status_bar(message, level.value, end, height.value).also { if (end != MemorySegment.NULL) KuiNative.kui_widget_release(end) }
        }
    }) { RowScopeInstance.trailing() }

/** [text] after a dot: a line of a list. */
@Composable
fun BulletText(text: String, modifier: Modifier = Modifier, color: Color = Color.Unspecified) =
    Row(modifier, horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
        val dot = if (color.isSpecified) color else LocalTheme.current.textMuted
        Box(Modifier.size(5.dp).background(dot, CircleShape))
        Text(text, color = color)
    }

// ---- menus, as Compose for Desktop writes them ------------------------------------------------------------------
//
//     MenuBar {
//         Menu("File") {
//             Item("Open", onClick = { open() })
//             Separator()
//             Item("Quit", onClick = { quit() })
//         }
//     }
//     ContextMenuArea(items = { listOf(ContextMenuItem("Copy") { copy() }) }) { Text("Right-click me") }

/** One line of a context menu, as Compose for Desktop names it. */
class ContextMenuItem(val label: String, val onClick: () -> Unit)

/** [content], with the menu [items] gives where the right button is pressed on it. */
@Composable
fun ContextMenuArea(items: () -> List<ContextMenuItem>, modifier: Modifier = Modifier, enabled: Boolean = true, content: @Composable () -> Unit) =
    ContextMenuArea(if (enabled) items().map { MenuItem(it.label, onClick = it.onClick) } else emptyList(), modifier, content)

/** What a [MenuBar]'s block is written in: its menus. */
class MenuBarScope internal constructor() {
    internal val menus = mutableListOf<Menu>()
    fun Menu(text: String, enabled: Boolean = true, content: MenuScope.() -> Unit) {
        val items = MenuScope().apply(content).items
        menus += Menu(text, if (enabled) items else items.map { item -> MenuItem(item.label, false, item.onClick).also { it.separator = item.separator } })
    }
}

/** What a Menu's block is written in: its lines. */
class MenuScope internal constructor() {
    internal val items = mutableListOf<MenuItem>()
    fun Item(text: String, enabled: Boolean = true, onClick: () -> Unit) { items += MenuItem(text, enabled, onClick) }
    fun Separator() { items += MenuItem.Divider }
    /** A line that is on or off: a tick before it while it is on. */
    fun CheckboxItem(text: String, checked: Boolean, enabled: Boolean = true, onCheckedChange: (Boolean) -> Unit) {
        items += MenuItem((if (checked) "✓ " else "   ") + text, enabled) { onCheckedChange(!checked) }
    }
    /** One of several: a dot before the one that is chosen. */
    fun RadioButtonItem(text: String, selected: Boolean, enabled: Boolean = true, onClick: () -> Unit) {
        items += MenuItem((if (selected) "• " else "   ") + text, enabled, onClick)
    }
}

/** A row of menus, each opening under its title, written as Compose for Desktop writes a window's menu bar. */
@Composable
fun MenuBar(modifier: Modifier = Modifier, content: MenuBarScope.() -> Unit) = MenuBar(MenuBarScope().apply(content).menus, modifier)
