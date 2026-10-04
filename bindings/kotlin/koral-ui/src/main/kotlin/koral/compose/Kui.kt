package koral.compose

import java.lang.foreign.Arena
import java.lang.foreign.FunctionDescriptor
import java.lang.foreign.MemoryLayout
import java.lang.foreign.MemorySegment
import java.lang.foreign.SegmentAllocator
import java.lang.foreign.StructLayout
import java.lang.foreign.ValueLayout
import java.lang.foreign.ValueLayout.ADDRESS
import java.lang.foreign.ValueLayout.JAVA_BOOLEAN
import java.lang.foreign.ValueLayout.JAVA_FLOAT
import java.lang.invoke.MethodHandles
import koral.Handles
import koral.Log
import koral.interop.Native
import koral.ui.interop.KuiLayouts

/** A struct's fields, written by name at C's offsets. */
internal class Struct(val segment: MemorySegment, private val layout: StructLayout) {
    constructor(allocator: SegmentAllocator, layout: StructLayout) : this(allocator.allocate(layout), layout)

    // Where a field is, worked out once a layout: every widget made writes a handful of them.
    private val offsets = known.getOrPut(layout) { HashMap() }
    private fun offset(path: String): Long = offsets.getOrPut(path) {
        layout.byteOffset(*path.split('.').map { MemoryLayout.PathElement.groupElement(it) }.toTypedArray())
    }

    private companion object {
        val known = java.util.IdentityHashMap<StructLayout, HashMap<String, Long>>()
    }
    fun float(field: String, value: Float) = apply { segment.set(JAVA_FLOAT, offset(field), value) }
    fun int(field: String, value: Int) = apply { segment.set(ValueLayout.JAVA_INT, offset(field), value) }
    fun bool(field: String, value: Boolean) = apply { segment.set(JAVA_BOOLEAN, offset(field), value) }
    fun address(field: String, value: MemorySegment) = apply { segment.set(ADDRESS, offset(field), value) }
    fun color(field: String, c: Color) = apply {
        val o = offset(field)
        segment.set(JAVA_FLOAT, o, c.red); segment.set(JAVA_FLOAT, o + 4, c.green)
        segment.set(JAVA_FLOAT, o + 8, c.blue); segment.set(JAVA_FLOAT, o + 12, c.alpha)
    }
    /** Copies [value], a struct of the field's own layout, into it. */
    fun struct(field: String, value: MemorySegment) = apply { MemorySegment.copy(value, 0, segment, offset(field), value.byteSize()) }
    fun floats(field: String, vararg values: Float) = apply {
        val o = offset(field)
        values.forEachIndexed { i, v -> segment.set(JAVA_FLOAT, o + i * 4L, v) }
    }
}

/** The arena [scratch] hands out: one for a whole rebuild, however many widgets it makes. */
internal object Scratch {
    var arena: Arena? = null
    var depth = 0
    var owner: Thread? = null
}

/**
 * Memory for what a native call is handed — a struct, a string — good until the outermost [scratch] returns. Making
 * a widget takes a few such calls, and a rebuild makes thousands of widgets: they share one arena, opened by
 * whoever asks first and closed when it is done, where each would otherwise open and close its own.
 */
internal inline fun <T> scratch(block: (Arena) -> T): T {
    val thread = Thread.currentThread()
    if (Scratch.depth > 0 && Scratch.owner !== thread) return Arena.ofConfined().use(block)   // another thread's: one of its own
    if (Scratch.depth == 0) { Scratch.arena = Arena.ofConfined(); Scratch.owner = thread }
    Scratch.depth++
    try {
        return block(Scratch.arena!!)
    } finally {
        if (--Scratch.depth == 0) { Scratch.arena!!.close(); Scratch.arena = null; Scratch.owner = null }
    }
}

internal fun vec2(a: SegmentAllocator, x: Float, y: Float): MemorySegment = Struct(a, KuiLayouts.KuiVec2).float("x", x).float("y", y).segment
internal fun radii(a: SegmentAllocator, shape: Shape?): MemorySegment {
    val r = when (shape) {
        is RoundedCornerShape -> floatArrayOf(shape.topStart.value, shape.topEnd.value, shape.bottomEnd.value, shape.bottomStart.value)
        else -> floatArrayOf(0f, 0f, 0f, 0f)
    }
    return Struct(a, KuiLayouts.KuiRadii).floats("top_left", *r).segment
}

/**
 * The C callbacks koral-ui calls back into Kotlin with: one upcall stub per signature, shared by every
 * widget, each finding its target through the `user` handle.
 */
internal object Callbacks {
    private val lookup = MethodHandles.lookup()
    private fun stub(name: String, descriptor: FunctionDescriptor): MemorySegment =
        Native.linker.upcallStub(lookup.findStatic(Callbacks::class.java, name, descriptor.toMethodType()), descriptor, Arena.global())

    val action by lazy { stub("onAction", FunctionDescriptor.ofVoid(ADDRESS)) }
    val boolAction by lazy { stub("onBool", FunctionDescriptor.ofVoid(JAVA_BOOLEAN, ADDRESS)) }
    val floatAction by lazy { stub("onFloat", FunctionDescriptor.ofVoid(JAVA_FLOAT, ADDRESS)) }
    val textAction by lazy { stub("onText", FunctionDescriptor.ofVoid(ADDRESS, ADDRESS)) }
    val colorAction by lazy { stub("onColor", FunctionDescriptor.ofVoid(JAVA_FLOAT, JAVA_FLOAT, JAVA_FLOAT, JAVA_FLOAT, ADDRESS)) }
    val layoutRule by lazy {
        stub("onLayout", FunctionDescriptor.ofVoid(ADDRESS, JAVA_FLOAT, JAVA_FLOAT, JAVA_FLOAT, JAVA_FLOAT, ADDRESS, ADDRESS, ADDRESS))
    }
    val pointAction by lazy { stub("onPoint", FunctionDescriptor.ofVoid(JAVA_FLOAT, JAVA_FLOAT, ADDRESS)) }
    val panAction by lazy { stub("onPan", FunctionDescriptor.ofVoid(JAVA_FLOAT, JAVA_FLOAT, JAVA_FLOAT, JAVA_FLOAT, ADDRESS)) }
    val sizeAction by lazy { stub("onSize", FunctionDescriptor.ofVoid(JAVA_FLOAT, JAVA_FLOAT, JAVA_FLOAT, JAVA_FLOAT, ADDRESS)) }
    val stopsAction by lazy { stub("onStops", FunctionDescriptor.ofVoid(ADDRESS, ValueLayout.JAVA_LONG, ADDRESS)) }
    val painter by lazy { stub("onPaint", FunctionDescriptor.ofVoid(ADDRESS, JAVA_FLOAT, JAVA_FLOAT, ADDRESS)) }
    val free by lazy { stub("onFree", FunctionDescriptor.ofVoid(ADDRESS)) }
    val dropAction by lazy { stub("onDrop", FunctionDescriptor.ofVoid(ADDRESS, ADDRESS, ADDRESS, JAVA_FLOAT, JAVA_FLOAT, ADDRESS)) }
    val itemBuilder by lazy { stub("onBuildItem", FunctionDescriptor.of(ADDRESS, ValueLayout.JAVA_LONG, ADDRESS)) }
    val index by lazy { stub("onIndex", FunctionDescriptor.ofVoid(ValueLayout.JAVA_LONG, JAVA_FLOAT, ADDRESS)) }
    val range by lazy { stub("onRange", FunctionDescriptor.ofVoid(ValueLayout.JAVA_LONG, ValueLayout.JAVA_LONG, ADDRESS)) }

    /** A {invoke, user, destroy} callback struct of [layout] calling [target]; freed when koral-ui lets it go. */
    fun make(a: SegmentAllocator, layout: StructLayout, invoke: MemorySegment, target: Any?): MemorySegment {
        val s = Struct(a, layout)
        if (target == null) return s.segment
        // Every callback struct is {function, user, destroy}, whatever its function is called.
        val (function, user, destroy) = layout.memberLayouts().map { it.name().get() }
        s.address(function, invoke).address(user, Handles.put(target)).address(destroy, free)
        return s.segment
    }

    private inline fun <T> call(user: MemorySegment, body: (T) -> Unit) {
        val target = Handles.get<T>(user) ?: return
        try { body(target) } catch (e: Throwable) { Log.error("[koral.compose] a callback threw $e\n${e.stackTraceToString()}") }
    }

    @JvmStatic fun onAction(user: MemorySegment) = call<() -> Unit>(user) { it() }
    @JvmStatic fun onBool(value: Boolean, user: MemorySegment) = call<(Boolean) -> Unit>(user) { it(value) }
    @JvmStatic fun onFloat(value: Float, user: MemorySegment) = call<(Float) -> Unit>(user) { it(value) }
    @JvmStatic fun onText(text: MemorySegment, user: MemorySegment) = call<(String) -> Unit>(user) { it(Native.kString(text)) }
    @JvmStatic fun onColor(r: Float, g: Float, b: Float, a: Float, user: MemorySegment) = call<(Color) -> Unit>(user) { it(Color(r, g, b, a)) }
    @JvmStatic fun onLayout(context: MemorySegment, minWidth: Float, maxWidth: Float, minHeight: Float, maxHeight: Float,
                            outWidth: MemorySegment, outHeight: MemorySegment, user: MemorySegment) =
        call<(MemorySegment, FloatArray) -> Size>(user) {
            val size = it(context, floatArrayOf(minWidth, maxWidth, minHeight, maxHeight))
            outWidth.reinterpret(4).set(JAVA_FLOAT, 0, size.width)
            outHeight.reinterpret(4).set(JAVA_FLOAT, 0, size.height)
        }
    @JvmStatic fun onPoint(x: Float, y: Float, user: MemorySegment) = call<(Offset) -> Unit>(user) { it(Offset(x, y)) }
    @JvmStatic fun onPan(dx: Float, dy: Float, x: Float, y: Float, user: MemorySegment) =
        call<(Offset, Offset) -> Unit>(user) { it(Offset(dx, dy), Offset(x, y)) }
    @JvmStatic fun onSize(width: Float, height: Float, pixelWidth: Float, pixelHeight: Float, user: MemorySegment) =
        call<(Size, Size) -> Unit>(user) { it(Size(width, height), Size(pixelWidth, pixelHeight)) }
    @JvmStatic fun onStops(stops: MemorySegment, count: Long, user: MemorySegment) = call<(List<ColorStop>) -> Unit>(user) {
        // Five floats a stop: where it is, and its colour.
        val data = stops.reinterpret(count * 5 * 4)
        fun f(i: Long) = data.getAtIndex(JAVA_FLOAT, i)
        it(List(count.toInt()) { n -> val at = n * 5L; ColorStop(f(at), Color(f(at + 1), f(at + 2), f(at + 3), f(at + 4))) })
    }
    @JvmStatic fun onPaint(canvas: MemorySegment, width: Float, height: Float, user: MemorySegment) =
        call<(DrawScope) -> Unit>(user) { it(DrawScope(canvas, Size(width, height))) }
    @JvmStatic fun onFree(user: MemorySegment) = Handles.free(user)
    @JvmStatic fun onDrop(type: MemorySegment, text: MemorySegment, payload: MemorySegment, x: Float, y: Float, user: MemorySegment) =
        call<(DragData, Offset) -> Unit>(user) {
            // A drag begun in Kotlin: the DragData itself. One from elsewhere: its kind and text.
            val data = (if (payload != MemorySegment.NULL) Handles.get<Any>(payload) as? DragData else null)
                ?: DragData(Native.kString(type), Native.kString(text))
            it(data, Offset(x, y))
        }
    @JvmStatic fun onIndex(index: Long, offset: Float, user: MemorySegment) = call<(Long, Float) -> Unit>(user) { it(index, offset) }
    @JvmStatic fun onRange(first: Long, last: Long, user: MemorySegment) = call<(Long, Long) -> Unit>(user) { it(first, last) }
    @JvmStatic fun onBuildItem(index: Long, user: MemorySegment): MemorySegment {
        val build = Handles.get<(Long) -> MemorySegment>(user) ?: return MemorySegment.NULL
        return try { build(index) } catch (e: Throwable) {
            Log.error("[koral.compose] building a list item threw $e\n${e.stackTraceToString()}")
            MemorySegment.NULL
        }
    }
}
