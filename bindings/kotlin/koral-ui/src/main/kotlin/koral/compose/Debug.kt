package koral.compose

import koral.ui.interop.KuiNative

/** Debugging aids for interfaces. They are the process's: every interface in it shows them. */
object UiDebug {
    /**
     * Whether every container is outlined where it was laid out, over what it paints — what holds others in
     * one colour, what holds nothing (a Text, a control) in a fainter one, and what is painted into a layer
     * of its own in a third. For seeing where a Column or a Box really is, and how big. Off by default; it
     * can be turned on and off while the program runs.
     *
     * ```
     * override fun initialize() {
     *     UiDebug.paintBounds = true
     *     setContent { Panel() }
     * }
     * ```
     */
    var paintBounds: Boolean
        get() = KuiNative.kui_debug_paint_bounds()
        set(value) = KuiNative.kui_debug_set_paint_bounds(value)
}
