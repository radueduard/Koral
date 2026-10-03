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
import koral.ui.TextAlign
import koral.ui.interop.KuiLayouts
import koral.ui.interop.KuiNative

/** The theme of the interface being composed: what [setContent] was given. */
val LocalTheme = staticCompositionLocalOf { Theme.Dark }

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
    Arena.ofConfined().use { a ->
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

/**
 * [text], in [color] — or the content colour around it ([LocalContentColor]: a Button's), or the theme's.
 * [fontSize] is the theme's unless given.
 */
@Composable
fun Text(text: String, modifier: Modifier = Modifier, color: Color = Color.Unspecified, fontSize: TextUnit = TextUnit.Unspecified,
         textAlign: TextAlign = TextAlign.eStart, softWrap: Boolean = true, letterSpacing: TextUnit = TextUnit.Unspecified,
         lineHeight: Float = 1.25f) {
    val theme = LocalTheme.current
    val resolved = when {
        color.isSpecified -> color
        LocalContentColor.current.isSpecified -> LocalContentColor.current
        else -> theme.text
    }
    val size = if (fontSize.value.isNaN()) theme.fontSize.value else fontSize.value
    val spacing = if (letterSpacing.value.isNaN()) 0f else letterSpacing.value
    Node(modifier, listOf(text, resolved, size, textAlign, softWrap, spacing, lineHeight), { _, _ ->
        Arena.ofConfined().use { a ->
            val style = Struct(a, KuiLayouts.KuiTextStyle).float("size", size).color("color", resolved)
                .float("line_height", lineHeight).float("letter_spacing", spacing).segment
            KuiNative.kui_text(text, style, textAlign.value, softWrap)
        }
    })
}

// ---- buttons --------------------------------------------------------------------------------------------

@Composable
private fun ButtonNode(onClick: () -> Unit, modifier: Modifier, enabled: Boolean, style: ButtonStyle, contentColor: Color,
                       contentPadding: PaddingValues?, content: @Composable RowScope.() -> Unit) {
    // By default as tall as the theme's controls, as a labelled koral-ui button is.
    val theme = LocalTheme.current
    val padding = contentPadding ?: PaddingValues(if (style == ButtonStyle.ePlain) 8.dp else 14.dp,
        maxOf(0f, (theme.controlHeight.value - theme.fontSize.value * 1.25f) / 2f).dp)
    Node(modifier, listOf(enabled, style, padding), { node, kids ->
        Arena.ofConfined().use { a ->
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

/** A filled button holding [content] — anything — laid out in a row. */
@Composable
fun Button(onClick: () -> Unit, modifier: Modifier = Modifier, enabled: Boolean = true, contentPadding: PaddingValues? = null,
           content: @Composable RowScope.() -> Unit) =
    ButtonNode(onClick, modifier, enabled, ButtonStyle.ePrimary, LocalTheme.current.onPrimary, contentPadding, content)

/** A button drawn as an outline. */
@Composable
fun OutlinedButton(onClick: () -> Unit, modifier: Modifier = Modifier, enabled: Boolean = true, contentPadding: PaddingValues? = null,
                   content: @Composable RowScope.() -> Unit) =
    ButtonNode(onClick, modifier, enabled, ButtonStyle.eSecondary, LocalTheme.current.text, contentPadding, content)

/** A button that is only its content, in the theme's primary colour. */
@Composable
fun TextButton(onClick: () -> Unit, modifier: Modifier = Modifier, enabled: Boolean = true, contentPadding: PaddingValues? = null,
               content: @Composable RowScope.() -> Unit) =
    ButtonNode(onClick, modifier, enabled, ButtonStyle.ePlain, LocalTheme.current.primary, contentPadding, content)

// ---- controls -------------------------------------------------------------------------------------------

@Composable
fun Checkbox(checked: Boolean, onCheckedChange: ((Boolean) -> Unit)?, modifier: Modifier = Modifier) =
    Node(modifier, checked, { node, _ ->
        Arena.ofConfined().use { a ->
            KuiNative.kui_checkbox(checked, Callbacks.make(a, KuiLayouts.KuiBoolAction, Callbacks.boolAction,
                { v: Boolean -> node.onBool?.invoke(v) }), "")
        }
    }, update = { onBool = onCheckedChange })

@Composable
fun Switch(checked: Boolean, onCheckedChange: ((Boolean) -> Unit)?, modifier: Modifier = Modifier) =
    Node(modifier, checked, { node, _ ->
        Arena.ofConfined().use { a ->
            KuiNative.kui_switch(checked, Callbacks.make(a, KuiLayouts.KuiBoolAction, Callbacks.boolAction,
                { v: Boolean -> node.onBool?.invoke(v) }))
        }
    }, update = { onBool = onCheckedChange })

@Composable
fun Slider(value: Float, onValueChange: (Float) -> Unit, modifier: Modifier = Modifier,
           valueRange: ClosedFloatingPointRange<Float> = 0f..1f) =
    Node(modifier, listOf(value, valueRange), { node, _ ->
        Arena.ofConfined().use { a ->
            KuiNative.kui_slider(value, Callbacks.make(a, KuiLayouts.KuiFloatAction, Callbacks.floatAction,
                { v: Float -> node.onFloat?.invoke(v) }), valueRange.start, valueRange.endInclusive)
        }
    }, update = { onFloat = onValueChange })

/** A bar filled to [progress], from 0 to 1. */
@Composable
fun LinearProgressIndicator(progress: () -> Float, modifier: Modifier = Modifier) {
    val value = progress()
    Node(modifier, value, { _, _ -> KuiNative.kui_progress_bar(value) })
}

/**
 * A one-line text field showing [value], as Compose's does: what is typed reaches [onValueChange], and shows
 * once it comes back as [value] — so `onValueChange = { text = it.uppercase() }` shows capitals.
 */
@Composable
fun TextField(value: String, onValueChange: (String) -> Unit, modifier: Modifier = Modifier, placeholder: String = "",
              width: Dp = Dp.Unspecified, onSubmit: ((String) -> Unit)? = null) =
    Node(modifier, listOf(value, placeholder, width), { node, _ ->
        Arena.ofConfined().use { a ->
            val options = Struct(a, KuiLayouts.KuiTextFieldOptions)
                .address("text", a.allocateFrom(value)).address("placeholder", a.allocateFrom(placeholder))
                .float("width", if (width.value.isNaN()) -1f else width.value).bool("controlled", true)
            options.segment.asSlice(KuiLayouts.KuiTextFieldOptions.byteOffset(
                java.lang.foreign.MemoryLayout.PathElement.groupElement("on_changed")), KuiLayouts.KuiTextAction)
                .copyFrom(Callbacks.make(a, KuiLayouts.KuiTextAction, Callbacks.textAction, { s: String ->
                    node.onText?.invoke(s)
                    node.invalidate()   // made again from what value is next frame: a refused edit goes back
                }))
            options.segment.asSlice(KuiLayouts.KuiTextFieldOptions.byteOffset(
                java.lang.foreign.MemoryLayout.PathElement.groupElement("on_submitted")), KuiLayouts.KuiTextAction)
                .copyFrom(Callbacks.make(a, KuiLayouts.KuiTextAction, Callbacks.textAction, { s: String -> node.onSubmit?.invoke(s) }))
            KuiNative.kui_text_field(options.segment)
        }
    }, update = { onText = onValueChange; this.onSubmit = onSubmit })

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
        Arena.ofConfined().use { a ->
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
 * (its own size otherwise). It shows what the image holds each time the interface is drawn.
 */
@Composable
@Suppress("UNUSED_PARAMETER")
fun Image(image: koral.Image, contentDescription: String?, modifier: Modifier = Modifier, contentScale: ContentScale = ContentScale.Fit) =
    Node(modifier, image to contentScale, { _, _ ->
        Arena.ofConfined().use { a -> KuiNative.kui_image(image.nativeHandle, contentScale.fit.value, vec2(a, -1f, -1f)) }
    })
