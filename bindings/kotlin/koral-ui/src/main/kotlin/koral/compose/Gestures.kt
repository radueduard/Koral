package koral.compose

import java.lang.foreign.MemorySegment
import java.lang.foreign.SegmentAllocator
import kotlin.coroutines.Continuation
import kotlin.coroutines.EmptyCoroutineContext
import kotlin.coroutines.startCoroutine
import koral.ui.interop.KuiLayouts

// The pointer, as Compose's Modifier.pointerInput takes it:
//
//     Modifier.pointerInput(Unit) { detectTapGestures(onTap = { at -> ... }, onDoubleTap = { ... }) }
//     Modifier.pointerInput(Unit) { detectDragGestures { change, dragAmount -> offset += dragAmount } }
//
// The block is run once a key, as Compose's is — but here it only says what to call: koral-ui's gesture
// detector hears the pointer and calls it. There is no awaitPointerEventScope: a gesture is one of those
// detect* names, not a loop over events.

/** One pointer's change: where it is and was, and whether its button is and was down. */
class PointerInputChange(val position: Offset, val previousPosition: Offset, val pressed: Boolean = true, val previousPressed: Boolean = true) {
    var isConsumed = false
        private set
    fun consume() { isConsumed = true }
}

/** What a [pointerInput] block is written in: where the gestures it wants are said. */
class PointerInputScope internal constructor() {
    internal var onTap: ((Offset) -> Unit)? = null
    internal var onDoubleTap: ((Offset) -> Unit)? = null
    internal var onPress: ((Offset) -> Unit)? = null
    internal var onLongPress: ((Offset) -> Unit)? = null
    internal var longPressed = false
    internal var onDragStart: ((Offset) -> Unit)? = null
    internal var onDrag: ((PointerInputChange, Offset) -> Unit)? = null
    internal var onDragEnd: (() -> Unit)? = null
    // Where the pointer last went down, and when it last tapped: a tap is told where, and two close together are one double tap.
    internal var down = Offset.Zero
    internal var lastTap = 0L
    internal var last = Offset.Zero
    internal var pressed = false
    // What awaitPointerEvent is waiting in, if anything is: the next thing the pointer does goes there.
    internal var waiting: kotlin.coroutines.Continuation<PointerEvent>? = null

    internal fun deliver(type: PointerEventType, at: Offset, down: Boolean) {
        val change = PointerInputChange(at, last, down, pressed)
        last = at
        pressed = down
        val to = waiting ?: return
        waiting = null
        to.resumeWith(Result.success(PointerEvent(listOf(change), type)))
    }

    /** The pointer an event at a time: `awaitPointerEventScope { while (true) { val event = awaitPointerEvent(); ... } }`. */
    suspend fun <R> awaitPointerEventScope(block: suspend AwaitPointerEventScope.() -> R): R = AwaitPointerEventScope(this).block()
}

enum class PointerEventType { Unknown, Press, Release, Move, Enter, Exit }

/** Something the pointer did: its [changes] (one pointer: one change) and what kind of thing it was. */
class PointerEvent(val changes: List<PointerInputChange>, val type: PointerEventType)

fun PointerInputChange.changedToDown(): Boolean = pressed && !previousPressed
fun PointerInputChange.changedToUp(): Boolean = !pressed && previousPressed
fun PointerInputChange.positionChange(): Offset = position - previousPosition

/** Where the pointer is waited for. */
class AwaitPointerEventScope internal constructor(private val scope: PointerInputScope) {
    /** The next thing the pointer does over this: a press, a release, a move, coming in or going out. */
    suspend fun awaitPointerEvent(): PointerEvent = kotlin.coroutines.suspendCoroutine { scope.waiting = it }

    suspend fun awaitFirstDown(@Suppress("UNUSED_PARAMETER") requireUnconsumed: Boolean = true): PointerInputChange {
        while (true) awaitPointerEvent().changes.firstOrNull { it.changedToDown() }?.let { return it }
    }

    suspend fun waitForUpOrCancellation(): PointerInputChange? {
        while (true) {
            val event = awaitPointerEvent()
            if (event.type == PointerEventType.Exit && !scope.pressed) return null
            event.changes.firstOrNull { it.changedToUp() }?.let { return it }
        }
    }
}

/**
 * Taps: [onPress] when the pointer goes down, [onTap] when it comes up where it went down, [onDoubleTap] for the
 * second of two in quick succession, [onLongPress] once it has been held down half a second without moving — and
 * then letting go is no tap.
 */
@Suppress("RedundantSuspendModifier")
suspend fun PointerInputScope.detectTapGestures(onDoubleTap: ((Offset) -> Unit)? = null, onLongPress: ((Offset) -> Unit)? = null,
                                                onPress: ((Offset) -> Unit)? = null, onTap: ((Offset) -> Unit)? = null) {
    this.onDoubleTap = onDoubleTap
    this.onLongPress = onLongPress
    this.onPress = onPress
    this.onTap = onTap
}

/** Drags: [onDrag] hears how far the pointer moved, each time it does, between [onDragStart] and [onDragEnd]. */
@Suppress("RedundantSuspendModifier")
suspend fun PointerInputScope.detectDragGestures(onDragStart: (Offset) -> Unit = {}, onDragEnd: () -> Unit = {}, onDragCancel: () -> Unit = {},
                                                 onDrag: (change: PointerInputChange, dragAmount: Offset) -> Unit) {
    this.onDragStart = onDragStart
    this.onDragEnd = { onDragEnd(); if (false) onDragCancel() }
    this.onDrag = onDrag
}

// Equal while its keys are: a new block each recomposition is the same input, as it is in Compose.
internal class PointerInputElement(val keys: List<Any?>, block: suspend PointerInputScope.() -> Unit) : Modifier.Element {
    val scope = PointerInputScope().also { scope ->
        // Run to where it would wait — which, saying only what to call, is its end.
        block.startCoroutine(scope, Continuation(EmptyCoroutineContext) { result -> result.exceptionOrNull()?.let { throw it } })
    }
    override fun equals(other: Any?) = other is PointerInputElement && other.keys == keys
    override fun hashCode() = keys.hashCode()
}

/** Hears the pointer over this, as [block] says: see [detectTapGestures] and [detectDragGestures]. Said again when [key1] changes. */
fun Modifier.pointerInput(key1: Any?, block: suspend PointerInputScope.() -> Unit): Modifier = then(PointerInputElement(listOf(key1), block))
fun Modifier.pointerInput(key1: Any?, key2: Any?, block: suspend PointerInputScope.() -> Unit): Modifier = then(PointerInputElement(listOf(key1, key2), block))
fun Modifier.pointerInput(vararg keys: Any?, block: suspend PointerInputScope.() -> Unit): Modifier = then(PointerInputElement(keys.toList(), block))

/** The press that may yet be a long one: asked every frame whether it has been held long enough. */
internal object LongPress {
    private var scope: PointerInputScope? = null
    private var since = 0L
    fun begin(to: PointerInputScope) { if (to.onLongPress != null) { scope = to; since = System.nanoTime() } }
    fun end(of: PointerInputScope) { if (scope === of) scope = null }
    fun poll(now: Long) {
        val held = scope ?: return
        if (now - since < 500_000_000L) return
        scope = null
        held.longPressed = true
        held.onLongPress?.invoke(held.down)
    }
}

/** koral-ui's gesture options, calling whichever scope [current] gives when the pointer does something. */
internal fun pointerOptions(a: SegmentAllocator, current: () -> PointerInputScope?): MemorySegment {
    val options = Struct(a, KuiLayouts.KuiGestureOptions).bool("opaque", true)
    options.struct("on_tap_down", Callbacks.make(a, KuiLayouts.KuiPointAction, Callbacks.pointAction, { at: Offset ->
        current()?.let { it.down = at; it.longPressed = false; LongPress.begin(it); it.onPress?.invoke(at); it.deliver(PointerEventType.Press, at, true) }
    }))
    options.struct("on_tap_up", Callbacks.make(a, KuiLayouts.KuiAction, Callbacks.action, {
        current()?.let { LongPress.end(it); it.deliver(PointerEventType.Release, it.last, false) }
    }))
    options.struct("on_hover", Callbacks.make(a, KuiLayouts.KuiPointAction, Callbacks.pointAction, { at: Offset ->
        current()?.let { if (!it.pressed) it.deliver(PointerEventType.Move, at, false) }
    }))
    options.struct("on_enter", Callbacks.make(a, KuiLayouts.KuiAction, Callbacks.action, { current()?.let { it.deliver(PointerEventType.Enter, it.last, it.pressed) } }))
    options.struct("on_exit", Callbacks.make(a, KuiLayouts.KuiAction, Callbacks.action, { current()?.let { it.deliver(PointerEventType.Exit, it.last, it.pressed) } }))
    options.struct("on_tap", Callbacks.make(a, KuiLayouts.KuiAction, Callbacks.action, {
        current()?.let { scope ->
            LongPress.end(scope)
            if (scope.longPressed) { scope.longPressed = false; return@let }
            val now = System.nanoTime()
            val double = scope.onDoubleTap
            if (double != null && now - scope.lastTap < 300_000_000L) { scope.lastTap = 0L; double(scope.down) }
            else { scope.lastTap = now; scope.onTap?.invoke(scope.down) }
        }
    }))
    options.struct("on_pan_start", Callbacks.make(a, KuiLayouts.KuiPointAction, Callbacks.pointAction, { at: Offset ->
        current()?.let { LongPress.end(it); it.onDragStart?.invoke(at) }
    }))
    options.struct("on_pan_update", Callbacks.make(a, KuiLayouts.KuiPanAction, Callbacks.panAction, { delta: Offset, at: Offset ->
        current()?.let { scope -> scope.onDrag?.invoke(PointerInputChange(at, scope.last), delta); scope.deliver(PointerEventType.Move, at, true) }
    }))
    options.struct("on_pan_end", Callbacks.make(a, KuiLayouts.KuiAction, Callbacks.action, {
        current()?.let { it.onDragEnd?.invoke(); it.deliver(PointerEventType.Release, it.last, false) }
    }))
    return options.segment
}
