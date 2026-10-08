package koral

import java.lang.foreign.Arena
import java.lang.foreign.MemoryLayout
import java.lang.foreign.MemorySegment
import java.lang.foreign.StructLayout
import java.lang.foreign.ValueLayout
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicLong
import koral.interop.KoralNative
import koral.interop.Native

/** What a failed call into Koral threw: koral_last_error's reason. */
class KoralException(message: String) : RuntimeException(message)

/** Throws what the last call on this thread failed with, if it did. */
fun checkLastError() {
    val error = KoralNative.koral_last_error()
    if (error.isNotEmpty()) throw KoralException(error)
}

internal fun checked(handle: MemorySegment, what: String): MemorySegment {
    if (handle == MemorySegment.NULL) {
        val error = KoralNative.koral_last_error()
        throw KoralException(error.ifEmpty { "$what failed" })
    }
    return handle
}

internal fun checked(status: Int, what: String) {
    if (status != 0) throw KoralException(KoralNative.koral_last_error().ifEmpty { "$what failed" })
}

/** kor::log: into Koral's log, as C++ code writes it. */
object Log {
    fun info(message: String) = KoralNative.koral_log(0, message)
    fun warn(message: String) = KoralNative.koral_log(1, message)
    fun error(message: String) = KoralNative.koral_log(2, message)
}

/**
 * Objects the native side holds onto through a callback's `user` pointer: an id, not an address, so
 * nothing on the JVM moves or is pinned. What lets Kotlin lambdas and objects be C callbacks' data.
 */
object Handles {
    private val next = AtomicLong(1)
    private val objects = ConcurrentHashMap<Long, Any>()

    fun put(target: Any): MemorySegment {
        val id = next.getAndIncrement()
        objects[id] = target
        return MemorySegment.ofAddress(id)
    }

    @Suppress("UNCHECKED_CAST")
    fun <T> get(user: MemorySegment): T? = objects[user.address()] as T?

    fun free(user: MemorySegment) {
        objects.remove(user.address())
    }
}

/** Writes and reads a struct's fields by name — `a.b` for a nested one — at C's offsets. */
internal class Fields(val segment: MemorySegment, private val layout: StructLayout) {
    constructor(allocator: java.lang.foreign.SegmentAllocator, layout: StructLayout) : this(allocator.allocate(layout), layout)

    private fun offset(field: String): Long =
        layout.byteOffset(*field.split('.').map { MemoryLayout.PathElement.groupElement(it) }.toTypedArray())
    fun int(field: String, value: Int, index: Int = 0) = apply { segment.set(ValueLayout.JAVA_INT, offset(field) + index * 4L, value) }
    fun long(field: String, value: Long) = apply { segment.set(ValueLayout.JAVA_LONG, offset(field), value) }
    fun byte(field: String, value: Int) = apply { segment.set(ValueLayout.JAVA_BYTE, offset(field), value.toByte()) }
    fun float(field: String, value: Float) = apply { segment.set(ValueLayout.JAVA_FLOAT, offset(field), value) }
    fun bool(field: String, value: Boolean) = apply { segment.set(ValueLayout.JAVA_BOOLEAN, offset(field), value) }
    fun address(field: String, value: MemorySegment) = apply { segment.set(ValueLayout.ADDRESS, offset(field), value) }
    fun string(arena: Arena, field: String, value: String?) = address(field, Native.cString(arena, value))
    fun ints(field: String, vararg values: Int) = apply { values.forEachIndexed { i, v -> int(field, v, i) } }
    fun floats(field: String, vararg values: Float) = apply {
        val o = offset(field)
        values.forEachIndexed { i, v -> segment.set(ValueLayout.JAVA_FLOAT, o + i * 4L, v) }
    }
    /** Copies [value], a struct of the field's own layout, into it. */
    fun struct(field: String, value: MemorySegment) = apply { MemorySegment.copy(value, 0, segment, offset(field), value.byteSize()) }
    fun readInt(field: String, index: Int = 0): Int = segment.get(ValueLayout.JAVA_INT, offset(field) + index * 4L)
    fun readLong(field: String): Long = segment.get(ValueLayout.JAVA_LONG, offset(field))
    fun readFloat(field: String, index: Int = 0): Float = segment.get(ValueLayout.JAVA_FLOAT, offset(field) + index * 4L)
    fun readBool(field: String): Boolean = segment.get(ValueLayout.JAVA_BOOLEAN, offset(field))
    /** A `const char*` field, as a String ("" for null). */
    fun readString(field: String): String {
        val pointer = segment.get(ValueLayout.ADDRESS, offset(field))
        return if (pointer == MemorySegment.NULL) "" else pointer.reinterpret(Long.MAX_VALUE).getString(0)
    }
}

/** Reads the two uint32/float outputs a C function writes through pointers. */
internal inline fun <T> twoInts(read: (MemorySegment, MemorySegment) -> Unit, make: (Int, Int) -> T): T =
    Arena.ofConfined().use { a ->
        val x = a.allocate(ValueLayout.JAVA_INT)
        val y = a.allocate(ValueLayout.JAVA_INT)
        read(x, y)
        make(x.get(ValueLayout.JAVA_INT, 0), y.get(ValueLayout.JAVA_INT, 0))
    }

internal inline fun <T> twoFloats(read: (MemorySegment, MemorySegment) -> Unit, make: (Float, Float) -> T): T =
    Arena.ofConfined().use { a ->
        val x = a.allocate(ValueLayout.JAVA_FLOAT)
        val y = a.allocate(ValueLayout.JAVA_FLOAT)
        read(x, y)
        make(x.get(ValueLayout.JAVA_FLOAT, 0), y.get(ValueLayout.JAVA_FLOAT, 0))
    }

/**
 * Who closes what a library makes: the same rule Koral's resources follow. Made while a scene is being
 * made or in one of its hooks, it is that scene's, closed when the scene shuts down; made anywhere else,
 * it is the application's, closed before the device goes.
 */
object Ownership {
    internal val constructing = ThreadLocal<MutableList<AutoCloseable>?>()

    /** Hands [closeable] to its owner, and returns it. */
    fun <T : AutoCloseable> adopt(closeable: T): T {
        constructing.get()?.let { it.add(closeable); return closeable }
        val owner: Owner? = Scene.current ?: App.current
        owner?.own(closeable)
        return closeable
    }

    /** It was closed on its own: its owner no longer needs to. */
    fun release(closeable: AutoCloseable) {
        constructing.get()?.removeIf { it === closeable }
        Scene.current?.disown(closeable)
        App.current?.disown(closeable)
    }
}

/** Something that closes what it owns, last made first. */
abstract class Owner {
    private val owned = mutableListOf<AutoCloseable>()
    internal fun own(closeable: AutoCloseable) { synchronized(owned) { owned.add(closeable) } }
    // By reference: two handles onto one resource are equal, but each is closed on its own.
    internal fun disown(closeable: AutoCloseable) { synchronized(owned) { owned.removeIf { it === closeable } } }
    internal fun closeOwned() {
        val left = synchronized(owned) { owned.toList().also { owned.clear() } }
        for (closeable in left.asReversed()) {
            try { closeable.close() } catch (e: Throwable) { Log.error("[koral] closing $closeable threw $e") }
        }
    }
}
