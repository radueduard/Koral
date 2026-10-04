package koral.compose

import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import koral.ui.interop.KuiNative

// The keyboard given and taken, a theme for part of an interface, and a box that knows how much room it has —
// each as Compose writes it.

// ---- focus --------------------------------------------------------------------------------------------------

/**
 * Asks for the keyboard, for the field it is on:
 *
 * ```
 * val focus = remember { FocusRequester() }
 * TextField(name, { name = it }, Modifier.focusRequester(focus))
 * Button(onClick = { focus.requestFocus() }) { Text("Rename") }
 * ```
 */
class FocusRequester {
    internal var token by mutableIntStateOf(0)
        private set

    /** Gives its field the keyboard, from the next frame. */
    fun requestFocus() { token = ++asked }

    private companion object { var asked = 0 }
}

internal class FocusRequesterElement(val requester: FocusRequester) : Modifier.Element {
    override fun equals(other: Any?) = other is FocusRequesterElement && other.requester === requester
    override fun hashCode() = System.identityHashCode(requester)
}

/** Makes the field this is on the one [focusRequester] gives the keyboard to. */
fun Modifier.focusRequester(focusRequester: FocusRequester): Modifier = then(FocusRequesterElement(focusRequester))

/** Who has the keyboard, as far as it can be changed from outside. */
interface FocusManager {
    /** Takes the keyboard from whatever has it. */
    fun clearFocus(force: Boolean = false)
}

/** The interface's focus manager: `LocalFocusManager.current.clearFocus()`. */
object LocalFocusManager {
    val current: FocusManager
        @Composable get() {
            val ui = LocalComposeUi.current
            return remember(ui) { object : FocusManager { override fun clearFocus(force: Boolean) = ui.clearFocus() } }
        }
}

// ---- a theme for part of an interface -----------------------------------------------------------------------

/**
 * [content] in [theme], whatever the interface around it is in: koral-ui's own controls, and everything that
 * reads the theme.
 *
 * ```
 * KoralTheme(Themes.cupertino(dark = false)) { Preview() }
 * ```
 */
@Composable
fun KoralTheme(theme: Theme, content: @Composable () -> Unit) =
    Node(Modifier, theme, { _, kids ->
        scratch { a ->
            val child = stack(kids, Alignment.TopStart)
            KuiNative.kui_themed(theme.native(a), child).also { KuiNative.kui_widget_release(child) }
        }
    }) { CompositionLocalProvider(LocalTheme provides theme, content = content) }

// ---- a box that knows its room -------------------------------------------------------------------------------

/** What a [BoxWithConstraints]' content is written in: how much room there is. */
interface BoxWithConstraintsScope : BoxScope {
    val constraints: Constraints
    val minWidth: Dp
    val maxWidth: Dp
    val minHeight: Dp
    val maxHeight: Dp
}

private class BoxWithConstraintsScopeImpl(override val constraints: Constraints, scope: BoxScope) : BoxWithConstraintsScope, BoxScope by scope {
    private fun dp(v: Int) = if (v == Constraints.Infinity) Float.POSITIVE_INFINITY.dp else v.dp
    override val minWidth get() = dp(constraints.minWidth)
    override val maxWidth get() = dp(constraints.maxWidth)
    override val minHeight get() = dp(constraints.minHeight)
    override val maxHeight get() = dp(constraints.maxHeight)
}

/**
 * A [Box] whose content is told how much room it may take — `if (maxWidth < 400.dp) Narrow() else Wide()`. The room
 * is known once the box has been laid out: its content is composed from the frame after, and again when the room
 * changes.
 */
@Composable
fun BoxWithConstraints(modifier: Modifier = Modifier, contentAlignment: Alignment = Alignment.TopStart,
                       @Suppress("UNUSED_PARAMETER") propagateMinConstraints: Boolean = false, content: @Composable BoxWithConstraintsScope.() -> Unit) {
    var room by remember { mutableStateOf<Constraints?>(null) }
    Layout({
        val known = room
        if (known != null) Box(contentAlignment = contentAlignment) { BoxWithConstraintsScopeImpl(known, this).content() }
    }, modifier) { measurables, constraints ->
        if (constraints != room) room = constraints
        val placed = measurables.map { it.measure(constraints) }
        layout(constraints.constrainWidth(placed.maxOfOrNull { it.width } ?: 0), constraints.constrainHeight(placed.maxOfOrNull { it.height } ?: 0)) {
            placed.forEach { it.place(0, 0) }
        }
    }
}
