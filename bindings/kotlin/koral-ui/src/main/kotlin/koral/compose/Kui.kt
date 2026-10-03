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

    private fun offset(path: String): Long {
        val parts = path.split('.')
        return layout.byteOffset(*parts.map { MemoryLayout.PathElement.groupElement(it) }.toTypedArray())
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
    val painter by lazy { stub("onPaint", FunctionDescriptor.ofVoid(ADDRESS, JAVA_FLOAT, JAVA_FLOAT, ADDRESS)) }
    val free by lazy { stub("onFree", FunctionDescriptor.ofVoid(ADDRESS)) }
    val dropAction by lazy { stub("onDrop", FunctionDescriptor.ofVoid(ADDRESS, ADDRESS, ADDRESS, JAVA_FLOAT, JAVA_FLOAT, ADDRESS)) }
    val itemBuilder by lazy { stub("onBuildItem", FunctionDescriptor.of(ADDRESS, ValueLayout.JAVA_LONG, ADDRESS)) }
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
    @JvmStatic fun onRange(first: Long, last: Long, user: MemorySegment) = call<(Long, Long) -> Unit>(user) { it(first, last) }
    @JvmStatic fun onBuildItem(index: Long, user: MemorySegment): MemorySegment {
        val build = Handles.get<(Long) -> MemorySegment>(user) ?: return MemorySegment.NULL
        return try { build(index) } catch (e: Throwable) {
            Log.error("[koral.compose] building a list item threw $e\n${e.stackTraceToString()}")
            MemorySegment.NULL
        }
    }
}
