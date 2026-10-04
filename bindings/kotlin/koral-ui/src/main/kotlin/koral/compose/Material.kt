package koral.compose

import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.State
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.snapshots.SnapshotStateList
import androidx.compose.runtime.staticCompositionLocalOf
import koral.ui.interop.KuiNative

// What Compose's Material library adds over the foundation, under its names and with its parameters: the theme
// as MaterialTheme reads it, Surface, Card, Scaffold, dividers, tabs, dialogs, the text field and the round
// buttons. They are made of what koral-ui has; none is a new control.

// ---- the theme, as Material names it ---------------------------------------------------------------------------

/** The theme's colours under Material's names. */
class ColorScheme(val primary: Color, val onPrimary: Color, val primaryContainer: Color, val onPrimaryContainer: Color,
                  val secondary: Color, val onSecondary: Color, val background: Color, val onBackground: Color,
                  val surface: Color, val onSurface: Color, val surfaceVariant: Color, val onSurfaceVariant: Color,
                  val outline: Color, val outlineVariant: Color, val error: Color, val onError: Color)

/** The theme's text sizes under Material's names: each the theme's own size, scaled. */
class Typography(val displayLarge: TextStyle, val displayMedium: TextStyle, val displaySmall: TextStyle,
                 val headlineLarge: TextStyle, val headlineMedium: TextStyle, val headlineSmall: TextStyle,
                 val titleLarge: TextStyle, val titleMedium: TextStyle, val titleSmall: TextStyle,
                 val bodyLarge: TextStyle, val bodyMedium: TextStyle, val bodySmall: TextStyle,
                 val labelLarge: TextStyle, val labelMedium: TextStyle, val labelSmall: TextStyle)

class Shapes(val extraSmall: Shape, val small: Shape, val medium: Shape, val large: Shape, val extraLarge: Shape)

/** The theme, read as Compose reads Material's: `MaterialTheme.colorScheme.primary`, `MaterialTheme.typography.titleLarge`. */
object MaterialTheme {
    val colorScheme: ColorScheme
        @Composable get() = LocalTheme.current.let { t ->
            ColorScheme(t.primary, t.onPrimary, t.primaryPressed, t.onPrimary, t.primaryHover, t.onPrimary, t.background, t.text,
                        t.surface, t.text, t.surfaceHover, t.textMuted, t.border, t.border, Color(0xFFF2554B), Color.White)
        }
    val typography: Typography
        @Composable get() = LocalTheme.current.fontSize.value.let { base ->
            fun style(scale: Float) = TextStyle(fontSize = (base * scale).sp)
            Typography(style(3.8f), style(3f), style(2.4f), style(2.1f), style(1.9f), style(1.6f), style(1.45f), style(1.1f), style(0.95f),
                       style(1.07f), style(1f), style(0.85f), style(0.95f), style(0.85f), style(0.75f))
        }
    val shapes: Shapes
        @Composable get() = LocalTheme.current.radius.let { r ->
            Shapes(RoundedCornerShape(4.dp), RoundedCornerShape(8.dp), RoundedCornerShape(12.dp), RoundedCornerShape(16.dp), RoundedCornerShape(r))
        }
}

// ---- surfaces --------------------------------------------------------------------------------------------------

/** A line of a width and a colour: a border's. */
data class BorderStroke(val width: Dp, val color: Color)

fun Modifier.border(border: BorderStroke, shape: Shape = RectangleShape): Modifier = border(border.width, border.color, shape)

/** A piece of the interface's ground: a colour in a shape, a shadow under it when raised, and its content's colour. */
@Composable
fun Surface(modifier: Modifier = Modifier, shape: Shape = RectangleShape, color: Color = MaterialTheme.colorScheme.surface,
            contentColor: Color = MaterialTheme.colorScheme.onSurface, @Suppress("UNUSED_PARAMETER") tonalElevation: Dp = 0.dp, shadowElevation: Dp = 0.dp,
            border: BorderStroke? = null, content: @Composable () -> Unit) {
    var all = modifier.shadow(shadowElevation, shape).background(color, shape)
    if (border != null) all = all.border(border, shape)
    Box(all) { CompositionLocalProvider(LocalContentColor provides contentColor, content = content) }
}

/** A [Surface] that is pressed. */
@Composable
fun Surface(onClick: () -> Unit, modifier: Modifier = Modifier, enabled: Boolean = true, shape: Shape = RectangleShape,
            color: Color = MaterialTheme.colorScheme.surface, contentColor: Color = MaterialTheme.colorScheme.onSurface, tonalElevation: Dp = 0.dp,
            shadowElevation: Dp = 0.dp, border: BorderStroke? = null, content: @Composable () -> Unit) =
    Surface(modifier.clickable(enabled, onClick), shape, color, contentColor, tonalElevation, shadowElevation, border, content)

/** A surface with rounded corners holding a column: what groups a few related things. */
@Composable
fun Card(modifier: Modifier = Modifier, shape: Shape = MaterialTheme.shapes.medium, border: BorderStroke? = null,
         content: @Composable ColumnScope.() -> Unit) =
    Surface(modifier, shape, MaterialTheme.colorScheme.surfaceVariant, MaterialTheme.colorScheme.onSurface, border = border) { Column(content = content) }

@Composable
fun OutlinedCard(modifier: Modifier = Modifier, shape: Shape = MaterialTheme.shapes.medium,
                 border: BorderStroke = BorderStroke(1.dp, MaterialTheme.colorScheme.outline), content: @Composable ColumnScope.() -> Unit) =
    Surface(modifier, shape, MaterialTheme.colorScheme.surface, MaterialTheme.colorScheme.onSurface, border = border) { Column(content = content) }

/**
 * A screen's frame: [topBar] over [content] over [bottomBar], with [floatingActionButton] at the content's
 * bottom end. [content] is given the padding the bars would need were it under them — none: it is between them.
 */
@Composable
fun Scaffold(modifier: Modifier = Modifier, topBar: @Composable () -> Unit = {}, bottomBar: @Composable () -> Unit = {},
             snackbarHost: @Composable () -> Unit = {}, floatingActionButton: @Composable () -> Unit = {},
             containerColor: Color = MaterialTheme.colorScheme.background, contentColor: Color = MaterialTheme.colorScheme.onBackground,
             content: @Composable (PaddingValues) -> Unit) =
    Column(modifier.fillMaxSize().background(containerColor)) {
        CompositionLocalProvider(LocalContentColor provides contentColor) {
            topBar()
            Box(Modifier.weight(1f).fillMaxWidth()) {
                content(PaddingValues(0.dp))
                Box(Modifier.align(Alignment.BottomEnd).padding(16.dp)) { floatingActionButton() }
                Box(Modifier.align(Alignment.BottomCenter).padding(16.dp)) { snackbarHost() }
            }
            bottomBar()
        }
    }

/** A line across, between two things. */
@Composable
fun HorizontalDivider(modifier: Modifier = Modifier, thickness: Dp = 1.dp, color: Color = Color.Unspecified) =
    if (color.isSpecified) Box(modifier.fillMaxWidth().height(thickness).background(color)) else Divider(modifier, thickness)

/** A line down, between two things side by side: in a Row that says how tall it is. */
@Composable
fun VerticalDivider(modifier: Modifier = Modifier, thickness: Dp = 1.dp, color: Color = Color.Unspecified) =
    if (color.isSpecified) Box(modifier.fillMaxHeight().width(thickness).background(color)) else Divider(modifier, thickness, vertical = true)

// ---- buttons ---------------------------------------------------------------------------------------------------

/** A round button that is only its content: an icon's. */
@Composable
fun IconButton(onClick: () -> Unit, modifier: Modifier = Modifier, enabled: Boolean = true, content: @Composable () -> Unit) =
    Box(modifier.size(40.dp).clip(CircleShape).clickable(enabled, onClick), contentAlignment = Alignment.Center) { content() }

/** The one thing a screen is for, raised over its bottom end. */
@Composable
fun FloatingActionButton(onClick: () -> Unit, modifier: Modifier = Modifier, shape: Shape = RoundedCornerShape(16.dp),
                         containerColor: Color = MaterialTheme.colorScheme.primary, contentColor: Color = MaterialTheme.colorScheme.onPrimary,
                         content: @Composable () -> Unit) =
    Box(modifier.size(56.dp).shadow(6.dp, shape).clip(shape).background(containerColor, shape).clickable(onClick = onClick), contentAlignment = Alignment.Center) {
        CompositionLocalProvider(LocalContentColor provides contentColor, content = content)
    }

// ---- tabs ------------------------------------------------------------------------------------------------------

/** A row of [Tab]s over a line: `TabRow(selected) { titles.forEachIndexed { i, t -> Tab(selected == i, { selected = i }, text = { Text(t) }) } }`. */
@Composable
fun TabRow(@Suppress("UNUSED_PARAMETER") selectedTabIndex: Int, modifier: Modifier = Modifier, tabs: @Composable () -> Unit) =
    Column(modifier) {
        Row(horizontalArrangement = Arrangement.spacedBy(2.dp)) { tabs() }
        HorizontalDivider()
    }

/** One of a [TabRow]'s: its [text] (and [icon] over it), in the content colour while [selected], over a mark in the accent. */
@Composable
fun Tab(selected: Boolean, onClick: () -> Unit, modifier: Modifier = Modifier, enabled: Boolean = true, text: (@Composable () -> Unit)? = null,
        icon: (@Composable () -> Unit)? = null) {
    val theme = LocalTheme.current
    Column(modifier.clip(RoundedCornerShape(8.dp)).clickable(enabled, onClick).padding(horizontal = 12.dp), horizontalAlignment = Alignment.CenterHorizontally) {
        CompositionLocalProvider(LocalContentColor provides if (selected) theme.text else theme.textMuted) {
            Box(Modifier.height(theme.controlHeight - 3.dp), contentAlignment = Alignment.Center) {
                Column(horizontalAlignment = Alignment.CenterHorizontally) { icon?.invoke(); text?.invoke() }
            }
        }
        Box(Modifier.size(24.dp, 2.dp).background(if (selected) theme.primary else Color.Transparent, RoundedCornerShape(1.dp)))
        Spacer(Modifier.height(1.dp))
    }
}

// ---- dialogs ---------------------------------------------------------------------------------------------------

/** Whether what is outside a [Dialog] dismisses it. */
data class DialogProperties(val dismissOnBackPress: Boolean = true, val dismissOnClickOutside: Boolean = true)

internal class DialogEntry(val dismiss: State<() -> Unit>, val content: State<@Composable () -> Unit>, val properties: DialogProperties)

internal val LocalDialogs = staticCompositionLocalOf<SnapshotStateList<DialogEntry>> { error("a Dialog is shown in an interface: none is composing") }

/** The interface, and over all of it the dialog that was shown last, if any is. */
@Composable
internal fun DialogLayer(content: @Composable () -> Unit) {
    val dialogs = remember { mutableStateListOf<DialogEntry>() }
    CompositionLocalProvider(LocalDialogs provides dialogs) {
        val top = dialogs.lastOrNull()
        DialogHost(top != null, { top?.takeIf { it.properties.dismissOnClickOutside }?.dismiss?.value?.invoke() },
                   if (top != null) Modifier.fillMaxSize() else Modifier, dialog = { top?.content?.value?.invoke() }, content = content)
    }
}

/**
 * A dialog over the whole interface for as long as it is composed, as Compose's is: on a card in the middle, what
 * is behind it dimmed and deaf. A press outside it, where [properties] allow, calls [onDismissRequest] — which is
 * where the state that shows it is put back.
 *
 * ```
 * if (asking) Dialog(onDismissRequest = { asking = false }) { Text("Really?") }
 * ```
 */
@Composable
fun Dialog(onDismissRequest: () -> Unit, properties: DialogProperties = DialogProperties(), content: @Composable () -> Unit) {
    val dialogs = LocalDialogs.current
    val dismiss = rememberUpdatedState(onDismissRequest)
    val shown = rememberUpdatedState(content)
    DisposableEffect(properties) {
        val entry = DialogEntry(dismiss, shown, properties)
        dialogs += entry
        onDispose { dialogs -= entry }
    }
}

/** A [Dialog] of a title, a text and its buttons. */
@Composable
fun AlertDialog(onDismissRequest: () -> Unit, confirmButton: @Composable () -> Unit, modifier: Modifier = Modifier,
                dismissButton: (@Composable () -> Unit)? = null, icon: (@Composable () -> Unit)? = null, title: (@Composable () -> Unit)? = null,
                text: (@Composable () -> Unit)? = null, properties: DialogProperties = DialogProperties()) =
    Dialog(onDismissRequest, properties) {
        Column(modifier.widthIn(min = 280.dp, max = 560.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            icon?.invoke()
            if (title != null) ProvideTextStyle(MaterialTheme.typography.titleLarge, title)
            if (text != null) CompositionLocalProvider(LocalContentColor provides LocalTheme.current.textMuted, content = text)
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.End) {
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) { dismissButton?.invoke(); confirmButton() }
            }
        }
    }

// ---- progress --------------------------------------------------------------------------------------------------

/** A ring that turns for as long as something is being waited for. */
@Composable
fun CircularProgressIndicator(modifier: Modifier = Modifier, color: Color = MaterialTheme.colorScheme.primary, strokeWidth: Dp = 4.dp) {
    // Read where it is drawn, not where it is composed: each frame draws the ring again and composes nothing.
    val turn = rememberInfiniteTransition().animateFloat(0f, 360f, infiniteRepeatable(tween(1100, easing = LinearEasing)))
    Canvas(modifier.size(40.dp)) {
        val inset = strokeWidth.value / 2f
        drawArc(color, turn.value - 90f, 270f, useCenter = false, topLeft = Offset(inset, inset),
                size = Size(size.width - 2f * inset, size.height - 2f * inset), style = Stroke(strokeWidth.value, cap = StrokeCap.Round))
    }
}

/** A ring filled to [progress], from 0 to 1. */
@Composable
fun CircularProgressIndicator(progress: () -> Float, modifier: Modifier = Modifier, color: Color = MaterialTheme.colorScheme.primary,
                              strokeWidth: Dp = 4.dp, trackColor: Color = MaterialTheme.colorScheme.surfaceVariant) =
    Canvas(modifier.size(40.dp)) {
        val inset = strokeWidth.value / 2f
        val box = Size(size.width - 2f * inset, size.height - 2f * inset)
        drawArc(trackColor, 0f, 360f, useCenter = false, topLeft = Offset(inset, inset), size = box, style = Stroke(strokeWidth.value))
        drawArc(color, -90f, 360f * progress().coerceIn(0f, 1f), useCenter = false, topLeft = Offset(inset, inset), size = box,
                style = Stroke(strokeWidth.value, cap = StrokeCap.Round))
    }

// ---- the text field --------------------------------------------------------------------------------------------

/** What the keyboard's action key does. Enter is every one of them here: the first that is given is called. */
class KeyboardActions(val onDone: (() -> Unit)? = null, val onGo: (() -> Unit)? = null, val onNext: (() -> Unit)? = null,
                      val onPrevious: (() -> Unit)? = null, val onSearch: (() -> Unit)? = null, val onSend: (() -> Unit)? = null) {
    internal val onEnter: (() -> Unit)? get() = onDone ?: onGo ?: onSearch ?: onSend ?: onNext
    companion object { val Default = KeyboardActions() }
}

/** What kind of keyboard a field wants. There is one keyboard: taken, so that Compose's code reads the same. */
data class KeyboardOptions(val autoCorrect: Boolean = true) {
    companion object { val Default = KeyboardOptions() }
}

/** [content] deaf to the pointer: what is drawn over something that must still be pressed. */
@Composable
internal fun IgnorePointer(content: @Composable () -> Unit) =
    Node(Modifier, Unit, { _, kids ->
        val child = stack(kids, Alignment.TopStart)
        KuiNative.kui_ignore_pointer(child).also { KuiNative.kui_widget_release(child) }
    }) { content() }

/**
 * A text field, as Compose's: what is typed reaches [onValueChange] and shows once it comes back as [value].
 * [label] is over it, [placeholder] in it while it is empty, [leadingIcon] and [trailingIcon] either side.
 * As Compose's, it takes as many lines as are typed: Enter starts another, and it grows from [minLines] to
 * [maxLines] and scrolls past that. With [singleLine] it is one line, and Enter calls [keyboardActions]' action
 * (and [onSubmit], koral-ui's own, with the text). [width] is its width where the modifier does not say; a
 * `Modifier.focusRequester` gives it the keyboard when its [FocusRequester] is asked.
 */
@Composable
@Suppress("UNUSED_PARAMETER")
fun TextField(value: String, onValueChange: (String) -> Unit, modifier: Modifier = Modifier, enabled: Boolean = true, readOnly: Boolean = false,
              textStyle: TextStyle = LocalTextStyle.current, label: (@Composable () -> Unit)? = null, placeholder: (@Composable () -> Unit)? = null,
              leadingIcon: (@Composable () -> Unit)? = null, trailingIcon: (@Composable () -> Unit)? = null, isError: Boolean = false,
              keyboardOptions: KeyboardOptions = KeyboardOptions.Default, keyboardActions: KeyboardActions = KeyboardActions.Default,
              singleLine: Boolean = false, maxLines: Int = Int.MAX_VALUE, minLines: Int = 1,
              width: Dp = Dp.Unspecified, onSubmit: ((String) -> Unit)? = null) {
    val theme = LocalTheme.current
    val focus: Modifier = modifier.elements().filterIsInstance<FocusRequesterElement>().lastOrNull() ?: Modifier
    Column(modifier, verticalArrangement = Arrangement.spacedBy(4.dp)) {
        if (label != null) CompositionLocalProvider(LocalContentColor provides if (isError) MaterialTheme.colorScheme.error else theme.textMuted) {
            ProvideTextStyle(MaterialTheme.typography.labelMedium, label)
        }
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            leadingIcon?.invoke()
            Box(contentAlignment = Alignment.CenterStart) {
                BasicTextField(value, { if (!readOnly) onValueChange(it) }, focus, enabled = enabled, singleLine = singleLine,
                               maxLines = if (singleLine) 1 else maxLines, minLines = minLines, width = width,
                               onSubmit = { text -> keyboardActions.onEnter?.invoke(); onSubmit?.invoke(text) })
                // In the field, where its text starts, while it has none — and not in the pointer's way.
                if (value.isEmpty() && placeholder != null) IgnorePointer {
                    Box(Modifier.padding(start = 14.dp)) { CompositionLocalProvider(LocalContentColor provides theme.textMuted, content = placeholder) }
                }
            }
            trailingIcon?.invoke()
        }
    }
}

/** Compose's outlined field: koral-ui's fields are drawn one way, so it is [TextField]. */
@Composable
fun OutlinedTextField(value: String, onValueChange: (String) -> Unit, modifier: Modifier = Modifier, enabled: Boolean = true, readOnly: Boolean = false,
                      textStyle: TextStyle = LocalTextStyle.current, label: (@Composable () -> Unit)? = null, placeholder: (@Composable () -> Unit)? = null,
                      leadingIcon: (@Composable () -> Unit)? = null, trailingIcon: (@Composable () -> Unit)? = null, isError: Boolean = false,
                      keyboardOptions: KeyboardOptions = KeyboardOptions.Default, keyboardActions: KeyboardActions = KeyboardActions.Default,
                      singleLine: Boolean = false, maxLines: Int = Int.MAX_VALUE, minLines: Int = 1) =
    TextField(value, onValueChange, modifier, enabled, readOnly, textStyle, label, placeholder, leadingIcon, trailingIcon, isError, keyboardOptions,
              keyboardActions, singleLine, maxLines, minLines)

// ---- the theme, set for part of the interface ---------------------------------------------------------------------

/** Material's dark colours ([Themes.material]'s), with whichever of them are given in their place. */
@Composable
fun darkColorScheme(primary: Color = Color.Unspecified, onPrimary: Color = Color.Unspecified, background: Color = Color.Unspecified,
                    onBackground: Color = Color.Unspecified, surface: Color = Color.Unspecified, onSurface: Color = Color.Unspecified,
                    surfaceVariant: Color = Color.Unspecified, onSurfaceVariant: Color = Color.Unspecified, outline: Color = Color.Unspecified,
                    error: Color = Color.Unspecified): ColorScheme =
    scheme(MaterialDarkTheme, primary, onPrimary, background, onBackground, surface, onSurface, surfaceVariant, onSurfaceVariant, outline, error)

@Composable
fun lightColorScheme(primary: Color = Color.Unspecified, onPrimary: Color = Color.Unspecified, background: Color = Color.Unspecified,
                     onBackground: Color = Color.Unspecified, surface: Color = Color.Unspecified, onSurface: Color = Color.Unspecified,
                     surfaceVariant: Color = Color.Unspecified, onSurfaceVariant: Color = Color.Unspecified, outline: Color = Color.Unspecified,
                     error: Color = Color.Unspecified): ColorScheme =
    scheme(MaterialLightTheme, primary, onPrimary, background, onBackground, surface, onSurface, surfaceVariant, onSurfaceVariant, outline, error)

private fun scheme(t: Theme, primary: Color, onPrimary: Color, background: Color, onBackground: Color, surface: Color, onSurface: Color,
                   surfaceVariant: Color, onSurfaceVariant: Color, outline: Color, error: Color): ColorScheme {
    fun Color.or(other: Color) = if (isSpecified) this else other
    return ColorScheme(primary.or(t.primary), onPrimary.or(t.onPrimary), t.primaryPressed, t.onPrimary, t.primaryHover, t.onPrimary,
                       background.or(t.background), onBackground.or(t.text), surface.or(t.surface), onSurface.or(t.text),
                       surfaceVariant.or(t.surfaceHover), onSurfaceVariant.or(t.textMuted), outline.or(t.border), t.border,
                       error.or(Color(0xFFF2554B)), Color.White)
}

/**
 * [content] in [colorScheme]'s colours, [typography]'s text size and [shapes]' corners: everything inside it is drawn
 * in them, koral-ui's own controls — a Slider, a Switch, a field — too. Whatever else the theme around it says
 * (how tall a control is, its font) it keeps; [KoralTheme] sets a whole theme for part of an interface.
 */
@Composable
fun MaterialTheme(colorScheme: ColorScheme = MaterialTheme.colorScheme, typography: Typography = MaterialTheme.typography,
                  shapes: Shapes = MaterialTheme.shapes, content: @Composable () -> Unit) {
    val around = LocalTheme.current
    val accent = around.withAccent(colorScheme.primary)
    KoralTheme(around.copy(primary = colorScheme.primary, primaryHover = accent.primaryHover, primaryPressed = accent.primaryPressed,
                           onPrimary = colorScheme.onPrimary, focus = accent.focus, background = colorScheme.background,
                           surface = colorScheme.surface, surfaceHover = colorScheme.surfaceVariant, text = colorScheme.onSurface,
                           textMuted = colorScheme.onSurfaceVariant, border = colorScheme.outline,
                           radius = (shapes.medium as? RoundedCornerShape)?.topStart ?: around.radius,
                           fontSize = typography.bodyMedium.fontSize.takeIf { it.isSpecified } ?: around.fontSize,
                           fontFamily = typography.bodyMedium.fontFamily ?: around.fontFamily), content)
}
