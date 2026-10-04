package koral.compose

import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import java.lang.foreign.MemorySegment
import koral.ui.interop.KuiLayouts
import koral.ui.interop.KuiNative

// What is shown over everything from where it is called: Compose's Popup and DropdownMenu.
//
//     Box {
//         IconButton(onClick = { expanded = true }) { Icon(Icons.Default.MoreVert, "More") }
//         DropdownMenu(expanded, onDismissRequest = { expanded = false }) {
//             DropdownMenuItem(text = { Text("Rename") }, onClick = { expanded = false; rename() })
//         }
//     }
//
// As in Compose, the menu is put in the Box with what opens it, and opens under that Box, at its left.

/** Takes no room; [content], when there is any, is shown over everything, under ([below]) or over what this is in. */
@Composable
private fun Anchor(shown: Boolean, onDismissRequest: () -> Unit, offset: DpOffset, below: Boolean, content: @Composable () -> Unit) =
    Node(Modifier, Triple(shown, offset, below), { node, kids ->
        scratch { a ->
            val popup = if (kids.isEmpty()) MemorySegment.NULL else stack(kids, Alignment.TopStart)
            KuiNative.kui_popup_anchor(shown && kids.isNotEmpty(), popup,
                Callbacks.make(a, KuiLayouts.KuiAction, Callbacks.action, { node.onClick?.invoke() }),
                vec2(a, offset.x.value, offset.y.value), below).also { if (popup != MemorySegment.NULL) KuiNative.kui_widget_release(popup) }
        }
    }, update = { this.onClick = onDismissRequest }) { if (shown) content() }

/**
 * [content] over everything, at the top-left of what this is called in, moved by [offset]; a press outside it,
 * or Escape, calls [onDismissRequest]. It is shown for as long as it is composed.
 */
@Composable
fun Popup(alignment: Alignment = Alignment.TopStart, offset: IntOffset = IntOffset.Zero, onDismissRequest: (() -> Unit)? = null,
          content: @Composable () -> Unit) {
    @Suppress("UNUSED_VARIABLE") val unused = alignment   // it opens at the top-left of what it is in
    Anchor(true, { onDismissRequest?.invoke() }, DpOffset(offset.x.dp, offset.y.dp), below = false, content = content)
}

/** A menu, under what it is beside, while [expanded]: a column of [DropdownMenuItem]s on a raised card. */
@Composable
fun DropdownMenu(expanded: Boolean, onDismissRequest: () -> Unit, modifier: Modifier = Modifier, offset: DpOffset = DpOffset(0.dp, 0.dp),
                 content: @Composable ColumnScope.() -> Unit) {
    val theme = LocalTheme.current
    val shape = RoundedCornerShape(minOf(theme.radius.value, 14f).dp)
    Anchor(expanded, onDismissRequest, DpOffset(offset.x, offset.y + 4.dp), below = true) {
        Column(modifier.shadow(8.dp, shape).background(theme.surface, shape).border(1.dp, theme.border, shape).padding(6.dp).widthIn(min = 112.dp),
               content = content)
    }
}

/** One line of a [DropdownMenu]: [text], with [leadingIcon] before it and [trailingIcon] at its end. Pressed, it calls [onClick]. */
@Composable
fun DropdownMenuItem(text: @Composable () -> Unit, onClick: () -> Unit, modifier: Modifier = Modifier, leadingIcon: (@Composable () -> Unit)? = null,
                     trailingIcon: (@Composable () -> Unit)? = null, enabled: Boolean = true,
                     contentPadding: PaddingValues = PaddingValues(12.dp, 8.dp)) {
    val theme = LocalTheme.current
    Row(modifier.widthIn(min = 160.dp).clip(RoundedCornerShape(10.dp)).clickable(enabled, onClick).padding(contentPadding),
        horizontalArrangement = Arrangement.spacedBy(10.dp), verticalAlignment = Alignment.CenterVertically) {
        CompositionLocalProvider(LocalContentColor provides if (enabled) theme.text else theme.textMuted) {
            leadingIcon?.invoke()
            text()
            if (trailingIcon != null) { Spacer(Modifier.weight(1f)); trailingIcon() }
        }
    }
}
