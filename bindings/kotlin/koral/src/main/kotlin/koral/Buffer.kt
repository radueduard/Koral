package koral

import java.lang.foreign.Arena
import java.lang.foreign.MemorySegment
import java.lang.foreign.ValueLayout
import koral.interop.KoralNative

/**
 * How a value of type T sits in GPU memory: its size and alignment, and how it is written and read. What gives
 * a buffer typed contents — the JVM has no structs to copy as they are.
 *
 * Usually derived for you: anything that takes buffer data takes a vector, a matrix, a data class, or a list
 * or array of them, and lays it out with [of]. Write one by hand only for a layout no class describes:
 *
 * ```
 * data class Vertex(val position: Vec3, val color: Vec3)
 * Buffer.Builder().setData(vertices)                       // derived: 24 bytes each, as the C++ struct
 *
 * val Packed = GpuLayout<Vertex>(16,                       // by hand: colour packed into 4 bytes
 *     write = { s, at, v -> s.putVec3(at, v.position); s.set(ValueLayout.JAVA_INT, at + 12, packUnorm4x8(Vec4(v.color, 1f))) })
 * ```
 */
class GpuLayout<T>(val size: Long, val write: (MemorySegment, Long, T) -> Unit, val read: ((MemorySegment, Long) -> T)? = null,
                   val alignment: Long = 4) {
    /** How far apart consecutive values sit in an array: the size, rounded up to the alignment. */
    val stride: Long get() = (size + alignment - 1) / alignment * alignment

    /** [values], laid out one after another in [arena]. */
    fun pack(arena: Arena, values: List<T>): MemorySegment {
        val segment = arena.allocate(maxOf(1L, stride * values.size), 16)
        values.forEachIndexed { i, v -> write(segment, i * stride, v) }
        return segment
    }

    /** [count] values read from [segment]. */
    fun unpack(segment: MemorySegment, count: Int): List<T> {
        val reader = read ?: throw KoralException("this GpuLayout cannot read")
        return List(count) { reader(segment, it * stride) }
    }

    /** [values] as bytes. */
    fun toBytes(values: List<T>): ByteArray = Arena.ofConfined().use { a -> pack(a, values).asSlice(0, stride * values.size).toArray(ValueLayout.JAVA_BYTE) }
    /** The values in [bytes]. */
    fun fromBytes(bytes: ByteArray): List<T> = Arena.ofConfined().use { a ->
        unpack(a.allocateFrom(ValueLayout.JAVA_BYTE, *bytes), (bytes.size / stride).toInt())
    }

    companion object {
        /**
         * The layout of [type], derived: a number, a vector, a quaternion or a matrix as Koral's own; an enum as its
         * value; any other class (a data class, typically) as its fields in declaration order, each at its alignment
         * under [packing] — [GpuPacking.C] by default, the same bytes as the same struct in C++ and C#. Values are
         * read back through the constructor that takes every field. Derived once per class.
         */
        fun <T : Any> of(type: kotlin.reflect.KClass<T>, packing: GpuPacking = GpuPacking.C): GpuLayout<T> = AutoLayouts.of(type.java, packing)

        val Float = GpuLayout<Float>(4, { s, at, v -> s.set(ValueLayout.JAVA_FLOAT, at, v) }, { s, at -> s.get(ValueLayout.JAVA_FLOAT, at) })
        val Int = GpuLayout<Int>(4, { s, at, v -> s.set(ValueLayout.JAVA_INT, at, v) }, { s, at -> s.get(ValueLayout.JAVA_INT, at) })
        val Vec2 = GpuLayout<Vec2>(8, { s, at, v -> s.putVec2(at, v) }, { s, at -> s.getVec2(at) })
        /** A vec3 alone: 12 bytes. In a std140/std430 array, each takes 16 — use [Vec4]. */
        val Vec3 = GpuLayout<Vec3>(12, { s, at, v -> s.putVec3(at, v) }, { s, at -> s.getVec3(at) })
        val Vec4 = GpuLayout<Vec4>(16, { s, at, v -> s.putVec4(at, v) }, { s, at -> s.getVec4(at) })
        val Mat4 = GpuLayout<Mat4>(64, { s, at, v -> s.putMat4(at, v) }, { s, at -> s.getMat4(at) })
    }
}

fun MemorySegment.putVec2(at: Long, v: Vec2) { set(ValueLayout.JAVA_FLOAT, at, v.x); set(ValueLayout.JAVA_FLOAT, at + 4, v.y) }
fun MemorySegment.putVec3(at: Long, v: Vec3) { putVec2(at, Vec2(v.x, v.y)); set(ValueLayout.JAVA_FLOAT, at + 8, v.z) }
fun MemorySegment.putVec4(at: Long, v: Vec4) { putVec3(at, Vec3(v.x, v.y, v.z)); set(ValueLayout.JAVA_FLOAT, at + 12, v.w) }
fun MemorySegment.putMat4(at: Long, m: Mat4) = m.toArray().forEachIndexed { i, f -> set(ValueLayout.JAVA_FLOAT, at + i * 4L, f) }
fun MemorySegment.getVec2(at: Long) = Vec2(get(ValueLayout.JAVA_FLOAT, at), get(ValueLayout.JAVA_FLOAT, at + 4))
fun MemorySegment.getVec3(at: Long) = Vec3(get(ValueLayout.JAVA_FLOAT, at), get(ValueLayout.JAVA_FLOAT, at + 4), get(ValueLayout.JAVA_FLOAT, at + 8))
fun MemorySegment.getVec4(at: Long) = Vec4(get(ValueLayout.JAVA_FLOAT, at), get(ValueLayout.JAVA_FLOAT, at + 4),
                                           get(ValueLayout.JAVA_FLOAT, at + 8), get(ValueLayout.JAVA_FLOAT, at + 12))
fun MemorySegment.getMat4(at: Long) = Mat4(FloatArray(16) { get(ValueLayout.JAVA_FLOAT, at + it * 4L) })

/** Flags of [values], ORed: what a C function taking a kor::Flags<E> is given. */
internal fun bits(values: Array<out BufferUsage>) = values.fold(0) { acc, v -> acc or v.value }
internal fun bits(values: Array<out ImageUsage>) = values.fold(0) { acc, v -> acc or v.value }


/**
 * kor::Buffer: memory the GPU reads and writes.
 *
 * ```
 * data class Vertex(val position: Vec3, val uv: Vec2)
 * val vertices = Buffer.Builder()
 *     .setData(listOf(Vertex(Vec3(0f, 0.5f, 0f), Vec2(0.5f, 0f)), ...))   // or floatArrayOf(...), listOf(Vec3...)
 *     .setUsage(BufferUsage.eVertex, BufferUsage.eStorage)
 *     .setType(BufferType.eDeviceLocal)
 *     .build()
 *
 * uniforms.map { it.write(Camera(view, projection), packing = GpuPacking.Std140) }   // released at the end of the block
 * val back: List<Vertex> = vertices.readAs<Vertex>()
 * ```
 *
 * Offsets and counts are in bytes, except where a function takes typed values: then they count those.
 */
class Buffer internal constructor(native: MemorySegment) : Resource(native) {
    /** kor::Buffer::Builder: its size in bytes (setSize), or its contents (setData), which size it. */
    class Builder : koral.Builder(KoralNative.koral_buffer_builder_new()) {
        /** Its size in bytes. */
        fun setSize(bytes: Long) = apply { KoralNative.koral_buffer_builder_set_instance_count(native, bytes) }
        /** Room for [count] values of [layout]. */
        fun <T> setInstanceCount(count: Long, layout: GpuLayout<T>) = setSize(count * layout.stride)
        /**
         * Its contents, copied now; its size follows them. [data] is a primitive (or unsigned) array, a ByteBuffer or a
         * MemorySegment, as it is; or a vector, matrix, data class — or a list or array of them — laid out with
         * [packing] (see [GpuLayout.of]).
         */
        fun setData(data: Any, packing: GpuPacking = GpuPacking.C) = apply {
            Arena.ofConfined().use { a ->
                val s = segmentOf(a, data, packing)
                KoralNative.koral_buffer_builder_set_data(native, s, s.byteSize())
            }
        }
        /** [values], each laid out by [layout]. */
        fun <T> setData(values: List<T>, layout: GpuLayout<T>) = apply { Arena.ofConfined().use { a -> setData(layout.pack(a, values).asSlice(0, layout.stride * values.size)) } }
        fun setUsage(vararg usage: BufferUsage) = apply { KoralNative.koral_buffer_builder_set_usage(native, bits(usage)) }
        fun setType(type: BufferType) = apply { KoralNative.koral_buffer_builder_set_type(native, type.value) }
        fun setIsPerFrame(value: Boolean) = apply { KoralNative.koral_buffer_builder_set_is_per_frame(native, value) }
        fun setSharedAcrossQueues(shared: Boolean) = apply { KoralNative.koral_buffer_builder_set_shared_across_queues(native, shared) }
        fun build(): Buffer = built(KoralNative.koral_buffer_builder_build(native), "building a buffer")
    }

    /** size(): in bytes. */
    val size: Long get() = KoralNative.koral_buffer_size(native).also { checkLastError() }
    val usage: Set<BufferUsage> get() = BufferUsage.flagsOf(KoralNative.koral_buffer_usage_flags(native))
    val memoryType: BufferType get() = BufferType.of(KoralNative.koral_buffer_memory_type(native))
    val isHostVisible: Boolean get() = KoralNative.koral_buffer_is_host_visible(native)
    val isPerFrame: Boolean get() = KoralNative.koral_buffer_is_per_frame(native)
    val isSharedAcrossQueues: Boolean get() = KoralNative.koral_buffer_is_shared_across_queues(native)
    val deviceAddress: Long get() = KoralNative.koral_buffer_device_address(native)
    val copyCount: Int get() = KoralNative.koral_buffer_copy_count(native)

    /** [bytes] bytes from [offset] (the rest, by default), read back. */
    fun read(bytes: Long = size, offset: Long = 0): ByteArray = Arena.ofConfined().use { a ->
        val into = a.allocate(maxOf(1L, bytes))
        checked(KoralNative.koral_buffer_read(native, into, bytes, offset), "reading a buffer")
        into.asSlice(0, bytes).toArray(ValueLayout.JAVA_BYTE)
    }

    /** [count] floats from the [offset]th (-1: the rest). */
    fun readFloats(count: Int = -1, offset: Long = 0): FloatArray =
        Arena.ofConfined().use { a ->
            val count = if (count < 0) ((size / 4) - offset).toInt() else count
            val into = a.allocate(maxOf(4L, count * 4L))
            checked(KoralNative.koral_buffer_read(native, into, count * 4L, offset * 4), "reading a buffer")
            into.asSlice(0, count * 4L).toArray(ValueLayout.JAVA_FLOAT)
        }

    fun readInts(count: Int = -1, offset: Long = 0): IntArray =
        Arena.ofConfined().use { a ->
            val count = if (count < 0) ((size / 4) - offset).toInt() else count
            val into = a.allocate(maxOf(4L, count * 4L))
            checked(KoralNative.koral_buffer_read(native, into, count * 4L, offset * 4), "reading a buffer")
            into.asSlice(0, count * 4L).toArray(ValueLayout.JAVA_INT)
        }

    /** [count] values of [layout], from the [offset]th (-1: the rest). */
    fun <T> read(layout: GpuLayout<T>, count: Int = -1, offset: Long = 0): List<T> =
        Arena.ofConfined().use { a ->
            val count = if (count < 0) ((size / layout.stride) - offset).toInt() else count
            val into = a.allocate(maxOf(1L, count * layout.stride), 16)
            checked(KoralNative.koral_buffer_read(native, into, count * layout.stride, offset * layout.stride), "reading a buffer")
            layout.unpack(into, count)
        }

    /** Writes [data] — anything [Builder.setData] takes — at byte [offset]. */
    fun write(data: Any, offset: Long = 0, packing: GpuPacking = GpuPacking.C) = Arena.ofConfined().use { a ->
        val s = segmentOf(a, data, packing)
        checked(KoralNative.koral_buffer_write(native, s, s.byteSize(), offset), "writing a buffer")
    }

    /** Writes [values] of [layout], from the [offset]th. */
    fun <T> write(values: List<T>, layout: GpuLayout<T>, offset: Long = 0) =
        Arena.ofConfined().use { a -> write(layout.pack(a, values).asSlice(0, layout.stride * values.size), offset * layout.stride) }

    /**
     * ReadAsync: [read] without making the CPU wait for the GPU — the copy out is started, and this resumes,
     * next frame or later, with what it read.
     */
    suspend fun readAsync(bytes: Long = size, offset: Long = 0): ByteArray {
        val done = Arena.ofConfined().use { a ->
            val out = a.allocate(ValueLayout.ADDRESS)
            val readback = checked(KoralNative.koral_buffer_read_async(native, bytes, offset, out), "reading a buffer")
            readback to Token(out.get(ValueLayout.ADDRESS, 0))
        }
        val (readback, token) = done
        try {
            token.await()
            return Arena.ofConfined().use { a ->
                val into = a.allocate(maxOf(1L, bytes))
                checked(KoralNative.koral_readback_read(readback, into), "reading a buffer")
                into.asSlice(0, bytes).toArray(ValueLayout.JAVA_BYTE)
            }
        } finally {
            KoralNative.koral_readback_destroy(readback)
        }
    }

    /** [readAsync], as values of [layout]. */
    suspend fun <T> readAsync(layout: GpuLayout<T>, count: Int = -1, offset: Long = 0): List<T> {
        @Suppress("NAME_SHADOWING") val count = if (count < 0) ((size / layout.stride) - offset).toInt() else count
        val bytes = readAsync(count * layout.stride, offset * layout.stride)
        return Arena.ofConfined().use { a -> layout.unpack(a.allocateFrom(ValueLayout.JAVA_BYTE, *bytes), count) }
    }

    /**
     * Map: the buffer's memory, for the length of [block] — a mutable mapping, whose writes reach a per-frame
     * buffer's other copies.
     */
    fun <R> map(bytes: Long = size, offset: Long = 0, block: (Mapping) -> R): R {
        val mapping = Mapping(checked(KoralNative.koral_buffer_map(native, bytes, offset, true), "mapping a buffer"), mutable = true)
        try { return block(mapping) } finally { mapping.release() }
    }

    /** The const Map: for reading; several may be open at once. */
    fun <R> mapConst(bytes: Long = size, offset: Long = 0, block: (Mapping) -> R): R {
        val mapping = Mapping(checked(KoralNative.koral_buffer_map(native, bytes, offset, false), "mapping a buffer"), mutable = false)
        try { return block(mapping) } finally { mapping.release() }
    }

    /**
     * kor::Buffer::MutableMapping / ConstMapping: valid inside the block that has it. [segment] is the memory
     * itself; [write] (rather than writing to it) also reaches a per-frame buffer's other copies.
     */
    class Mapping internal constructor(private var handle: MemorySegment, val mutable: Boolean) {
        val size: Long = KoralNative.koral_mapping_size(handle)
        /** The mapped memory: read it, or (mutable) write it directly — for this frame's copy only. */
        val segment: MemorySegment = KoralNative.koral_mapping_data(handle).reinterpret(size)

        /** Writes [data] — anything [Builder.setData] takes — at byte [offset]. */
        fun write(data: Any, offset: Long = 0, packing: GpuPacking = GpuPacking.C) {
            check(mutable) { "a const mapping cannot be written" }
            Arena.ofConfined().use { a ->
                val s = segmentOf(a, data, packing)
                checked(KoralNative.koral_mapping_write(live(), s, s.byteSize(), offset), "writing a mapping")
            }
        }

        fun <T> write(values: List<T>, layout: GpuLayout<T>, offset: Long = 0) =
            Arena.ofConfined().use { a -> write(layout.pack(a, values).asSlice(0, layout.stride * values.size), offset * layout.stride) }

        fun flush(offset: Long = 0, bytes: Long = size - offset) =
            checked(KoralNative.koral_mapping_flush(live(), offset, bytes), "flushing a mapping")
        fun invalidate(offset: Long = 0, bytes: Long = size - offset) =
            checked(KoralNative.koral_mapping_invalidate(live(), offset, bytes), "invalidating a mapping")

        private fun live(): MemorySegment {
            check(handle != MemorySegment.NULL) { "a mapping used after its block" }
            return handle
        }

        internal fun release() {
            if (handle == MemorySegment.NULL) return
            KoralNative.koral_mapping_release(handle)
            handle = MemorySegment.NULL
        }
    }

    /** kor::Buffer::Slice: a range of a buffer, for a descriptor. */
    data class Slice(val buffer: Buffer, val offset: Long = 0, val size: Long = 0)

    fun slice(offset: Long, size: Long = 0) = Slice(this, offset, size)

    companion object {
        /** kor::WholeSize: "the rest of it". */
        const val WholeSize: Long = -1L
    }
}
