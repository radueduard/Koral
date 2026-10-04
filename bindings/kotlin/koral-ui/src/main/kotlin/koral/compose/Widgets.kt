package koral.compose

import androidx.compose.runtime.Composable
import androidx.compose.runtime.ComposeNode
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.snapshots.SnapshotStateObserver
import androidx.compose.runtime.staticCompositionLocalOf
import java.lang.foreign.Arena
import java.lang.foreign.MemorySegment
import koral.ui.ButtonStyle
import koral.ui.CrossAxisAlignment
import koral.ui.MainAxisSize
import koral.ui.interop.KuiLayouts
import koral.ui.interop.KuiNative

/** The theme of the interface being composed: what [setContent] was given. */
val LocalTheme = staticCompositionLocalOf { KoralDarkTheme }

/**
 * Emits one node. Whatever [make] depends on goes in [key]: the node's widget is made again only when that
 * changes, so a recomposition that changes nothing hands koral-ui nothing new.
 */
@Composable
internal fun Node(modifier: Modifier, key: Any?, make: (UiNode, List<MemorySegment>) -> MemorySegment,
                 update: UiNode.() -> Unit = {}, content: @Composable () -> Unit = {}) {
    ComposeNode<UiNode, UiApplier>(
        factory = { UiNode() },
        update = {
            set(modifier) { this.modifier = it }
            set(key) { this.make = make }
            reconcile { update() }
        },
        content = content,
    )
}

private fun Alignment.Vertical.cross() = cross(bias)
private fun Alignment.Horizontal.cross() = cross(bias)
private fun cross(bias: Float) = when {
    bias < 0f -> CrossAxisAlignment.eStart
    bias > 0f -> CrossAxisAlignment.eEnd
    else -> CrossAxisAlignment.eCenter
}.value

private fun flex(kids: List<MemorySegment>, vertical: Boolean, main: Int, spacing: Dp, cross: Int): MemorySegment =
    scratch { a ->
        val options = Struct(a, KuiLayouts.KuiFlexOptions).int("main_axis_alignment", main).int("cross_axis_alignment", cross)
            .int("main_axis_size", MainAxisSize.eMin.value).float("gap", spacing.value).segment
        if (vertical) KuiNative.kui_column(handles(a, kids), kids.size.toLong(), options)
        else KuiNative.kui_row(handles(a, kids), kids.size.toLong(), options)
    }

// ---- layout ---------------------------------------------------------------------------------------------

/** Its children over each other, placed by [contentAlignment] (or each one's own `Modifier.align`). */
@Composable
fun Box(modifier: Modifier = Modifier, contentAlignment: Alignment = Alignment.TopStart, content: @Composable BoxScope.() -> Unit = {}) =
    Node(modifier, contentAlignment, { _, kids -> stack(kids, contentAlignment) }) { BoxScopeInstance.content() }

/** Its children side by side, as wide as they are unless [modifier] says otherwise. */
@Composable
fun Row(modifier: Modifier = Modifier, horizontalArrangement: Arrangement.Horizontal = Arrangement.Start,
        verticalAlignment: Alignment.Vertical = Alignment.Top, content: @Composable RowScope.() -> Unit) =
    Node(modifier, horizontalArrangement to verticalAlignment,
         { _, kids -> flex(kids, false, horizontalArrangement.kind, horizontalArrangement.spacing, verticalAlignment.cross()) }) {
        RowScopeInstance.content()
    }

/** Its children one above the other, as tall as they are unless [modifier] says otherwise. */
@Composable
fun Column(modifier: Modifier = Modifier, verticalArrangement: Arrangement.Vertical = Arrangement.Top,
           horizontalAlignment: Alignment.Horizontal = Alignment.Start, content: @Composable ColumnScope.() -> Unit) =
    Node(modifier, verticalArrangement to horizontalAlignment,
         { _, kids -> flex(kids, true, verticalArrangement.kind, verticalArrangement.spacing, horizontalAlignment.cross()) }) {
        ColumnScopeInstance.content()
    }

/** Empty space, as big as [modifier] makes it: `Spacer(Modifier.height(8.dp))`, or `Modifier.weight(1f)` in a Row. */
@Composable
fun Spacer(modifier: Modifier) = Node(modifier, Unit, { _, _ -> KuiNative.kui_sized_box(0f, 0f, MemorySegment.NULL) })

// ---- text -----------------------------------------------------------------------------------------------

// ---- buttons --------------------------------------------------------------------------------------------

@Composable
private fun ButtonNode(onClick: () -> Unit, modifier: Modifier, enabled: Boolean, style: ButtonStyle, contentColor: Color,
                       contentPadding: PaddingValues?, content: @Composable RowScope.() -> Unit) {
    // By default as tall as the theme's controls, as a labelled koral-ui button is.
    val theme = LocalTheme.current
    val padding = contentPadding ?: PaddingValues(if (style == ButtonStyle.ePlain) 8.dp else 14.dp,
        maxOf(0f, (theme.controlHeight.value - theme.fontSize.value * 1.25f) / 2f).dp)
    Node(modifier, listOf(enabled, style, padding), { node, kids ->
        scratch { a ->
            val row = flex(kids, false, 2, 8.dp, CrossAxisAlignment.eCenter.value)
            val options = Struct(a, KuiLayouts.KuiButtonOptions).int("style", style.value).float("width", Float.NaN)
                .bool("enabled", enabled).bool("has_padding", true)
                .floats("padding", padding.start.value, padding.top.value, padding.end.value, padding.bottom.value)
            val action = Callbacks.make(a, KuiLayouts.KuiAction, Callbacks.action, { node.onClick?.invoke() })
            KuiNative.kui_button_with_child(row, action, options.segment).also { KuiNative.kui_widget_release(row) }
        }
    }, update = { this.onClick = onClick }) {
        CompositionLocalProvider(LocalContentColor provides contentColor) { RowScopeInstance.content() }
    }
}

/** The padding inside a button, around its content. */
data class PaddingValues(val start: Dp, val top: Dp, val end: Dp, val bottom: Dp) {
    constructor(all: Dp) : this(all, all, all, all)
    constructor(horizontal: Dp, vertical: Dp) : this(horizontal, vertical, horizontal, vertical)
}

/** A button's colours, as Compose's: what it is filled with and what its content is drawn in, enabled and not. */
class ButtonColors(val containerColor: Color, val contentColor: Color, val disabledContainerColor: Color, val disabledContentColor: Color)

/** What a button is by default, and how to say otherwise: `ButtonDefaults.buttonColors(containerColor = Color.Red)`. */
object ButtonDefaults {
    val ContentPadding = PaddingValues(14.dp, 8.dp)
    val TextButtonContentPadding = PaddingValues(8.dp, 8.dp)
    val shape: Shape @Composable get() = RoundedCornerShape(LocalTheme.current.radius)
    val outlinedShape: Shape @Composable get() = shape
    val textShape: Shape @Composable get() = shape
    @Composable
    fun buttonColors(containerColor: Color = Color.Unspecified, contentColor: Color = Color.Unspecified,
                     disabledContainerColor: Color = Color.Unspecified, disabledContentColor: Color = Color.Unspecified): ButtonColors {
        val t = LocalTheme.current
        return ButtonColors(containerColor.takeIf { it.isSpecified } ?: t.primary, contentColor.takeIf { it.isSpecified } ?: t.onPrimary,
                            disabledContainerColor.takeIf { it.isSpecified } ?: t.surfaceHover, disabledContentColor.takeIf { it.isSpecified } ?: t.textMuted)
    }
    @Composable
    fun outlinedButtonColors(containerColor: Color = Color.Transparent, contentColor: Color = Color.Unspecified,
                             disabledContainerColor: Color = Color.Transparent, disabledContentColor: Color = Color.Unspecified): ButtonColors {
        val t = LocalTheme.current
        return ButtonColors(containerColor, contentColor.takeIf { it.isSpecified } ?: t.text, disabledContainerColor,
                            disabledContentColor.takeIf { it.isSpecified } ?: t.textMuted)
    }
    @Composable
    fun textButtonColors(containerColor: Color = Color.Transparent, contentColor: Color = Color.Unspecified,
                         disabledContainerColor: Color = Color.Transparent, disabledContentColor: Color = Color.Unspecified): ButtonColors {
        val t = LocalTheme.current
        return ButtonColors(containerColor, contentColor.takeIf { it.isSpecified } ?: t.primary, disabledContainerColor,
                            disabledContentColor.takeIf { it.isSpecified } ?: t.textMuted)
    }
    @Composable
    fun outlinedButtonBorder(enabled: Boolean = true): BorderStroke = BorderStroke(1.dp, LocalTheme.current.border.copy(alpha = if (enabled) 1f else 0.5f))
}

/** A button of a shape and colours of the caller's own: made of a surface that is pressed, where koral-ui's own button is the theme's. */
@Composable
private fun ShapedButton(onClick: () -> Unit, modifier: Modifier, enabled: Boolean, shape: Shape, colors: ButtonColors, border: BorderStroke?,
                         contentPadding: PaddingValues, content: @Composable RowScope.() -> Unit) {
    var all = modifier.clip(shape).background(if (enabled) colors.containerColor else colors.disabledContainerColor, shape)
    if (border != null) all = all.border(border.width, border.color, shape)
    Row(all.clickable(enabled, onClick).padding(contentPadding), horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
        CompositionLocalProvider(LocalContentColor provides if (enabled) colors.contentColor else colors.disabledContentColor) { content() }
    }
}

/**
 * A filled button holding [content] — anything — laid out in a row. With no [shape], [colors] or [border] it is
 * koral-ui's own, in the theme's; with any of them, it is what they say.
 */
@Composable
fun Button(onClick: () -> Unit, modifier: Modifier = Modifier, enabled: Boolean = true, shape: Shape? = null, colors: ButtonColors? = null,
           border: BorderStroke? = null, contentPadding: PaddingValues? = null, content: @Composable RowScope.() -> Unit) =
    if (shape == null && colors == null && border == null)
        ButtonNode(onClick, modifier, enabled, ButtonStyle.ePrimary, LocalTheme.current.onPrimary, contentPadding, content)
    else ShapedButton(onClick, modifier, enabled, shape ?: ButtonDefaults.shape, colors ?: ButtonDefaults.buttonColors(), border,
                      contentPadding ?: ButtonDefaults.ContentPadding, content)

/** A button drawn as an outline. */
@Composable
fun OutlinedButton(onClick: () -> Unit, modifier: Modifier = Modifier, enabled: Boolean = true, shape: Shape? = null, colors: ButtonColors? = null,
                   border: BorderStroke? = null, contentPadding: PaddingValues? = null, content: @Composable RowScope.() -> Unit) =
    if (shape == null && colors == null && border == null)
        ButtonNode(onClick, modifier, enabled, ButtonStyle.eSecondary, LocalTheme.current.text, contentPadding, content)
    else ShapedButton(onClick, modifier, enabled, shape ?: ButtonDefaults.outlinedShape, colors ?: ButtonDefaults.outlinedButtonColors(),
                      border ?: ButtonDefaults.outlinedButtonBorder(enabled), contentPadding ?: ButtonDefaults.ContentPadding, content)

/** A button that is only its content, in the theme's primary colour. */
@Composable
fun TextButton(onClick: () -> Unit, modifier: Modifier = Modifier, enabled: Boolean = true, shape: Shape? = null, colors: ButtonColors? = null,
               border: BorderStroke? = null, contentPadding: PaddingValues? = null, content: @Composable RowScope.() -> Unit) =
    if (shape == null && colors == null && border == null)
        ButtonNode(onClick, modifier, enabled, ButtonStyle.ePlain, LocalTheme.current.primary, contentPadding, content)
    else ShapedButton(onClick, modifier, enabled, shape ?: ButtonDefaults.textShape, colors ?: ButtonDefaults.textButtonColors(), border,
                      contentPadding ?: ButtonDefaults.TextButtonContentPadding, content)

/** A filled button in a quieter colour: Material's tonal one. */
@Composable
fun FilledTonalButton(onClick: () -> Unit, modifier: Modifier = Modifier, enabled: Boolean = true, shape: Shape? = null, colors: ButtonColors? = null,
                      border: BorderStroke? = null, contentPadding: PaddingValues? = null, content: @Composable RowScope.() -> Unit) =
    ShapedButton(onClick, modifier, enabled, shape ?: ButtonDefaults.shape,
                 colors ?: ButtonDefaults.buttonColors(LocalTheme.current.surfacePressed, LocalTheme.current.text), border,
                 contentPadding ?: ButtonDefaults.ContentPadding, content)

@Composable
fun ElevatedButton(onClick: () -> Unit, modifier: Modifier = Modifier, enabled: Boolean = true, shape: Shape? = null, colors: ButtonColors? = null,
                   border: BorderStroke? = null, contentPadding: PaddingValues? = null, content: @Composable RowScope.() -> Unit) =
    ShapedButton(onClick, modifier.shadow(3.dp, shape ?: ButtonDefaults.shape), enabled, shape ?: ButtonDefaults.shape,
                 colors ?: ButtonDefaults.buttonColors(LocalTheme.current.surfaceHover, LocalTheme.current.primary), border,
                 contentPadding ?: ButtonDefaults.ContentPadding, content)

// ---- controls -------------------------------------------------------------------------------------------

/** What is made, faded and deaf to the pointer when not [enabled]. */
internal fun whenEnabled(enabled: Boolean, widget: MemorySegment): MemorySegment =
    if (enabled) widget else KuiNative.kui_disabled(widget, true).also { KuiNative.kui_widget_release(widget) }

@Composable
fun Checkbox(checked: Boolean, onCheckedChange: ((Boolean) -> Unit)?, modifier: Modifier = Modifier, enabled: Boolean = true) =
    Node(modifier, checked to enabled, { node, _ ->
        scratch { a ->
            whenEnabled(enabled, KuiNative.kui_checkbox(checked, Callbacks.make(a, KuiLayouts.KuiBoolAction, Callbacks.boolAction,
                { v: Boolean -> node.onBool?.invoke(v) }), ""))
        }
    }, update = { onBool = onCheckedChange })

@Composable
fun Switch(checked: Boolean, onCheckedChange: ((Boolean) -> Unit)?, modifier: Modifier = Modifier, enabled: Boolean = true) =
    Node(modifier, checked to enabled, { node, _ ->
        scratch { a ->
            whenEnabled(enabled, KuiNative.kui_switch(checked, Callbacks.make(a, KuiLayouts.KuiBoolAction, Callbacks.boolAction,
                { v: Boolean -> node.onBool?.invoke(v) })))
        }
    }, update = { onBool = onCheckedChange })

/**
 * A slider, as Compose's: [steps] is how many places it stops at between the two ends (0: anywhere), and
 * [onValueChangeFinished] is called when it is let go of.
 */
@Composable
fun Slider(value: Float, onValueChange: (Float) -> Unit, modifier: Modifier = Modifier, enabled: Boolean = true,
           valueRange: ClosedFloatingPointRange<Float> = 0f..1f, steps: Int = 0, onValueChangeFinished: (() -> Unit)? = null) =
    Node(modifier, listOf(value, valueRange, enabled), { node, _ ->
        scratch { a ->
            whenEnabled(enabled, KuiNative.kui_slider_finished(value, Callbacks.make(a, KuiLayouts.KuiFloatAction, Callbacks.floatAction,
                { v: Float -> node.onFloat?.invoke(v) }), valueRange.start, valueRange.endInclusive,
                Callbacks.make(a, KuiLayouts.KuiAction, Callbacks.action, { node.onClick?.invoke() })))
        }
    }, update = {
        onClick = onValueChangeFinished
        onFloat = if (steps <= 0) onValueChange else { v ->
            // The nearest of the places it stops at: the two ends, and [steps] between them.
            val span = valueRange.endInclusive - valueRange.start
            val place = Math.round((v - valueRange.start) / span * (steps + 1)).toFloat() / (steps + 1)
            onValueChange(valueRange.start + place * span)
        }
    })

/**
 * A number in a field, changed by dragging across it sideways — ImGui's DragFloat. Each unit dragged to the
 * right adds [speed]; the value stops at the ends of [valueRange]. [label] is shown before the value.
 *
 * ```
 * var speed by remember { mutableStateOf(1f) }
 * DragValue(speed, { speed = it }, label = "Speed", speed = 0.05f, valueRange = 0f..10f)
 * ```
 */
@Composable
fun DragValue(value: Float, onValueChange: (Float) -> Unit, modifier: Modifier = Modifier, label: String = "", speed: Float = 0.01f,
              valueRange: ClosedFloatingPointRange<Float> = Float.NEGATIVE_INFINITY..Float.POSITIVE_INFINITY, decimals: Int = 2,
              width: Dp = Dp.Unspecified) =
    Node(modifier, listOf(value, label, speed, valueRange, decimals, width), { node, _ ->
        scratch { a ->
            KuiNative.kui_drag_value(value, Callbacks.make(a, KuiLayouts.KuiFloatAction, Callbacks.floatAction,
                { v: Float -> node.onFloat?.invoke(v) }), speed, valueRange.start, valueRange.endInclusive, decimals, label,
                if (width.value.isNaN()) -1f else width.value)
        }
    }, update = { onFloat = onValueChange })

/**
 * A field showing the one of [items] at [selected]; pressed, it opens the list of them under itself, and
 * [onSelected] hears the index of the one picked. [placeholder] shows while [selected] is none of them (-1).
 *
 * ```
 * var quality by remember { mutableStateOf(1) }
 * Dropdown(listOf("Low", "Medium", "High"), quality, { quality = it })
 * ```
 */
@Composable
fun Dropdown(items: List<String>, selected: Int, onSelected: (Int) -> Unit, modifier: Modifier = Modifier, placeholder: String = "",
             width: Dp = Dp.Unspecified) =
    Node(modifier, listOf(items, selected, placeholder, width), { node, _ ->
        scratch { a ->
            val names = a.allocate(java.lang.foreign.ValueLayout.ADDRESS, maxOf(1, items.size).toLong())
            items.forEachIndexed { i, item -> names.setAtIndex(java.lang.foreign.ValueLayout.ADDRESS, i.toLong(), a.allocateFrom(item)) }
            KuiNative.kui_dropdown(names, items.size.toLong(), selected, Callbacks.make(a, KuiLayouts.KuiFloatAction, Callbacks.floatAction,
                { v: Float -> node.onFloat?.invoke(v) }), if (width.value.isNaN()) -1f else width.value, placeholder)
        }
    }, update = { onFloat = { onSelected(it.toInt()) } })

/** One line of a menu: something to pick, which runs [onClick] — or [Divider], the line between two groups of them. */
class MenuItem(val label: String, val enabled: Boolean = true, val onClick: () -> Unit = {}) {
    internal var separator = false

    companion object {
        /** A line between two groups of items. */
        val Divider: MenuItem get() = MenuItem("").also { it.separator = true }
    }
}

/**
 * [content], with a menu of [items] that opens where the right button is pressed on it. Picking an item runs
 * it and closes the menu; so does pressing anywhere else, or Escape.
 *
 * ```
 * ContextMenuArea(listOf(MenuItem("Rename") { rename() }, MenuItem.Divider, MenuItem("Delete") { delete() })) {
 *     Text("Right-click me")
 * }
 * ```
 */
@Composable
fun ContextMenuArea(items: List<MenuItem>, modifier: Modifier = Modifier, content: @Composable () -> Unit) {
    // What each line runs is looked up when it is picked: the menu need not be made again when only that changed.
    val current = androidx.compose.runtime.rememberUpdatedState(items)
    Node(modifier, items.map { Triple(it.label, it.enabled, it.separator) }, { _, kids ->
        scratch { a ->
            val size = KuiLayouts.KuiMenuItem.byteSize()
            val array = a.allocate(KuiLayouts.KuiMenuItem, maxOf(1, items.size).toLong())
            items.forEachIndexed { i, item ->
                Struct(array.asSlice(i * size, size), KuiLayouts.KuiMenuItem).address("label", a.allocateFrom(item.label))
                    .struct("on_selected", Callbacks.make(a, KuiLayouts.KuiAction, Callbacks.action, { current.value.getOrNull(i)?.onClick?.invoke() }))
                    .bool("disabled", !item.enabled).bool("separator", item.separator)
            }
            val child = stack(kids, Alignment.TopStart)
            KuiNative.kui_context_menu(array, items.size.toLong(), child).also { KuiNative.kui_widget_release(child) }
        }
    }) { content() }
}

/** A bar filled to [progress], from 0 to 1. */
@Composable
fun LinearProgressIndicator(progress: () -> Float, modifier: Modifier = Modifier) {
    val value = progress()
    Node(modifier, value, { _, _ -> KuiNative.kui_progress_bar(value) })
}

/**
 * koral-ui's own field, showing [value]: what is typed reaches [onValueChange], and shows once it comes back as
 * [value] — so `onValueChange = { text = it.uppercase() }` shows capitals. As Compose's, it takes as many lines as
 * are typed, from [minLines] up to [maxLines], Enter starting another; with [singleLine] it is one, and Enter calls
 * [onSubmit]. [placeholder] is plain text; [TextField] is Compose's, with a composable one. A
 * `Modifier.focusRequester` on it gives it the keyboard when its [FocusRequester] is asked.
 */
@Composable
fun BasicTextField(value: String, onValueChange: (String) -> Unit, modifier: Modifier = Modifier, enabled: Boolean = true,
                   singleLine: Boolean = false, maxLines: Int = if (singleLine) 1 else Int.MAX_VALUE, minLines: Int = 1,
                   placeholder: String = "", width: Dp = Dp.Unspecified, onSubmit: ((String) -> Unit)? = null) {
    val focus = modifier.elements().filterIsInstance<FocusRequesterElement>().lastOrNull()?.requester?.token ?: 0
    val several = !singleLine && maxLines > 1
    Node(modifier, listOf(value, placeholder, width, enabled, several, maxLines, minLines, focus), { node, _ ->
        scratch { a ->
            val options = Struct(a, KuiLayouts.KuiTextFieldOptions)
                .address("text", a.allocateFrom(value)).address("placeholder", a.allocateFrom(placeholder))
                .float("width", if (width.value.isNaN()) -1f else width.value).bool("controlled", true)
                .bool("multiline", several).int("min_lines", minLines).int("max_lines", minOf(maxOf(maxLines, minLines), 1000))
                .int("focus", focus)
            options.struct("on_changed", Callbacks.make(a, KuiLayouts.KuiTextAction, Callbacks.textAction, { s: String ->
                node.onText?.invoke(s)
                node.invalidate()   // made again from what value is next frame: a refused edit goes back
            }))
            options.struct("on_submitted", Callbacks.make(a, KuiLayouts.KuiTextAction, Callbacks.textAction, { s: String -> node.onSubmit?.invoke(s) }))
            whenEnabled(enabled, KuiNative.kui_text_field(options.segment))
        }
    }, update = { onText = onValueChange; this.onSubmit = onSubmit })
}

// ---- drawing --------------------------------------------------------------------------------------------

/**
 * Draws with [onDraw] into the space [modifier] gives it — `Canvas(Modifier.size(100.dp)) { drawCircle(Color.Red) }`.
 * It draws again when state it read while drawing changes, without composing again — Compose's draw phase.
 */
@Composable
fun Canvas(modifier: Modifier, onDraw: DrawScope.() -> Unit) {
    val reads = LocalDrawReads.current
    Node(modifier, onDraw, { node, _ ->
        node.onDispose = { reads.clear(node) }
        scratch { a ->
            KuiNative.kui_custom_paint(Callbacks.make(a, KuiLayouts.KuiPainter, Callbacks.painter, { scope: DrawScope ->
                reads.observeReads(node, Redraw) { scope.onDraw() }
            }), vec2(a, 0f, 0f), MemorySegment.NULL)
        }
    })
}

/** What a Canvas read while drawing: a change to it makes the Canvas's widget again, which koral-ui repaints. */
internal val LocalDrawReads = staticCompositionLocalOf<SnapshotStateObserver> { error("a Canvas outside setContent") }
private val Redraw: (UiNode) -> Unit = { it.invalidate() }

// ---- images ---------------------------------------------------------------------------------------------

/** How an image fills the space it is given: Compose's names for koral-ui's ImageFit. */
enum class ContentScale(internal val fit: koral.ui.ImageFit) {
    /** All of it, as large as fits, its proportions kept. */
    Fit(koral.ui.ImageFit.eContain),
    /** All the space, its proportions kept, the overflow cut. */
    Crop(koral.ui.ImageFit.eCover),
    /** All the space, stretched. */
    FillBounds(koral.ui.ImageFit.eFill),
    /** Its own size, centred. */
    None(koral.ui.ImageFit.eNone),
}

/**
 * A Koral image — a texture, or what a View or a pass draws into — shown in the space [modifier] gives it
 * (its own size otherwise). It shows what the image holds each time the interface is drawn. It is Compose's
 * Image under another name: `Image` is Koral's own, the thing this shows.
 */
@Composable
@Suppress("UNUSED_PARAMETER")
fun Picture(image: koral.Image, contentDescription: String?, modifier: Modifier = Modifier, contentScale: ContentScale = ContentScale.Fit) =
    Node(modifier, image to contentScale, { _, _ ->
        scratch { a -> KuiNative.kui_image(image.nativeHandle, contentScale.fit.value, vec2(a, -1f, -1f)) }
    })
