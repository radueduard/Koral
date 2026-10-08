package koral

import java.lang.foreign.Arena
import java.lang.foreign.MemorySegment
import java.lang.foreign.ValueLayout
import java.lang.invoke.MethodHandle
import java.lang.invoke.MethodHandles
import java.lang.reflect.Constructor
import java.lang.reflect.Modifier
import java.util.concurrent.ConcurrentHashMap
import kotlin.reflect.KClass

/**
 * How values are laid out in GPU memory — what a data class becomes in a buffer.
 *
 * - [C]: as the same struct is in C++ and C# — every value at its own alignment (4 for a float, a vector or a
 *   matrix), nothing more. What vertex buffers want, and what a C++ `struct { kor::Vec3 p; float w; }` is.
 * - [Std430]: a shader's storage buffers (`StructuredBuffer`, `buffer` blocks): vec2 aligned to 8, vec3 and vec4
 *   to 16, a mat3's columns padded to vec4s.
 * - [Std140]: uniform buffers (`cbuffer`, `uniform` blocks): std430, and every struct aligned to 16.
 */
enum class GpuPacking { C, Std430, Std140 }

/** The bytes of [data] as the GPU would read them: see [Buffer.Builder.setData] for what [data] may be. */
fun gpuBytes(data: Any, packing: GpuPacking = GpuPacking.C): ByteArray = Arena.ofConfined().use { a ->
    segmentOf(a, data, packing).toArray(ValueLayout.JAVA_BYTE)
}

/** Values of [T] back from [bytes] laid out with [packing]: what [gpuBytes] made, or a buffer read back. */
inline fun <reified T : Any> fromGpuBytes(bytes: ByteArray, packing: GpuPacking = GpuPacking.C): List<T> =
    GpuLayout.of(T::class, packing).fromBytes(bytes)

// ---- conversions between vectors and primitive arrays ------------------------------------------------------

@JvmName("vec2sToFloatArray") fun Iterable<Vec2>.toFloatArray(): FloatArray = flatMap { listOf(it.x, it.y) }.toFloatArray()
@JvmName("vec3sToFloatArray") fun Iterable<Vec3>.toFloatArray(): FloatArray = flatMap { listOf(it.x, it.y, it.z) }.toFloatArray()
@JvmName("vec4sToFloatArray") fun Iterable<Vec4>.toFloatArray(): FloatArray = flatMap { listOf(it.x, it.y, it.z, it.w) }.toFloatArray()
@JvmName("quatsToFloatArray") fun Iterable<Quat>.toFloatArray(): FloatArray = flatMap { listOf(it.x, it.y, it.z, it.w) }.toFloatArray()
@JvmName("mat4sToFloatArray") fun Iterable<Mat4>.toFloatArray(): FloatArray = flatMap { it.toArray().asIterable() }.toFloatArray()
@JvmName("ivec2sToIntArray") fun Iterable<IVec2>.toIntArray(): IntArray = flatMap { listOf(it.x, it.y) }.toIntArray()
@JvmName("ivec3sToIntArray") fun Iterable<IVec3>.toIntArray(): IntArray = flatMap { listOf(it.x, it.y, it.z) }.toIntArray()
@JvmName("ivec4sToIntArray") fun Iterable<IVec4>.toIntArray(): IntArray = flatMap { listOf(it.x, it.y, it.z, it.w) }.toIntArray()
@JvmName("uvec2sToIntArray") fun Iterable<UVec2>.toIntArray(): IntArray = flatMap { listOf(it.x.toInt(), it.y.toInt()) }.toIntArray()
@JvmName("uvec3sToIntArray") fun Iterable<UVec3>.toIntArray(): IntArray = flatMap { listOf(it.x.toInt(), it.y.toInt(), it.z.toInt()) }.toIntArray()
@JvmName("uvec4sToIntArray") fun Iterable<UVec4>.toIntArray(): IntArray = flatMap { listOf(it.x.toInt(), it.y.toInt(), it.z.toInt(), it.w.toInt()) }.toIntArray()

fun FloatArray.toVec2List(): List<Vec2> = List(size / 2) { Vec2(this[it * 2], this[it * 2 + 1]) }
fun FloatArray.toVec3List(): List<Vec3> = List(size / 3) { Vec3(this[it * 3], this[it * 3 + 1], this[it * 3 + 2]) }
fun FloatArray.toVec4List(): List<Vec4> = List(size / 4) { Vec4(this[it * 4], this[it * 4 + 1], this[it * 4 + 2], this[it * 4 + 3]) }
fun FloatArray.toMat4List(): List<Mat4> = List(size / 16) { Mat4(copyOfRange(it * 16, it * 16 + 16)) }
fun IntArray.toIVec2List(): List<IVec2> = List(size / 2) { IVec2(this[it * 2], this[it * 2 + 1]) }
fun IntArray.toIVec3List(): List<IVec3> = List(size / 3) { IVec3(this[it * 3], this[it * 3 + 1], this[it * 3 + 2]) }
fun IntArray.toIVec4List(): List<IVec4> = List(size / 4) { IVec4(this[it * 4], this[it * 4 + 1], this[it * 4 + 2], this[it * 4 + 3]) }

// ---- what segmentOf accepts --------------------------------------------------------------------------------

/**
 * Bytes of [data], in [arena]: a MemorySegment as it is; a primitive (or unsigned) array, or a ByteBuffer, as its
 * elements; anything else — a vector, a matrix, a data class, or a list or array of them — laid out by
 * [GpuLayout.of] with [packing].
 */
@OptIn(ExperimentalUnsignedTypes::class)
internal fun segmentOf(arena: Arena, data: Any, packing: GpuPacking = GpuPacking.C): MemorySegment = when (data) {
    is MemorySegment -> data
    is ByteArray -> arena.allocateFrom(ValueLayout.JAVA_BYTE, *data)
    is FloatArray -> arena.allocateFrom(ValueLayout.JAVA_FLOAT, *data)
    is IntArray -> arena.allocateFrom(ValueLayout.JAVA_INT, *data)
    is ShortArray -> arena.allocateFrom(ValueLayout.JAVA_SHORT, *data)
    is LongArray -> arena.allocateFrom(ValueLayout.JAVA_LONG, *data)
    is DoubleArray -> arena.allocateFrom(ValueLayout.JAVA_DOUBLE, *data)
    is UByteArray -> arena.allocateFrom(ValueLayout.JAVA_BYTE, *data.asByteArray())
    is UShortArray -> arena.allocateFrom(ValueLayout.JAVA_SHORT, *data.asShortArray())
    is UIntArray -> arena.allocateFrom(ValueLayout.JAVA_INT, *data.asIntArray())
    is ULongArray -> arena.allocateFrom(ValueLayout.JAVA_LONG, *data.asLongArray())
    is java.nio.ByteBuffer -> arena.allocate(data.remaining().toLong().coerceAtLeast(1)).also {
        MemorySegment.copy(MemorySegment.ofBuffer(data), 0, it, 0, data.remaining().toLong())
    }.asSlice(0, data.remaining().toLong())
    is Iterable<*> -> packAll(arena, data.toList(), packing)
    is Array<*> -> packAll(arena, data.asList(), packing)
    else -> packAll(arena, listOf(data), packing)
}

private fun packAll(arena: Arena, values: List<Any?>, packing: GpuPacking): MemorySegment {
    if (values.isEmpty()) return arena.allocate(1).asSlice(0, 0)
    val type = values.first()?.javaClass ?: throw IllegalArgumentException("a null in buffer data")
    require(values.all { it != null && it.javaClass == type }) {
        "buffer data mixes types (${values.mapNotNull { it?.javaClass?.simpleName }.distinct()}): give values of one type"
    }
    @Suppress("UNCHECKED_CAST")
    val layout = GpuLayout.of(type.kotlin, packing) as GpuLayout<Any>
    return layout.pack(arena, values.filterNotNull()).asSlice(0, layout.stride * values.size)
}

// ---- automatic layouts --------------------------------------------------------------------------------------

internal object AutoLayouts {
    private val cache = ConcurrentHashMap<Pair<Class<*>, GpuPacking>, GpuLayout<*>>()

    private val F = ValueLayout.JAVA_FLOAT_UNALIGNED
    private val I = ValueLayout.JAVA_INT_UNALIGNED

    private fun floats(size: Long, align: Long, n: Int, toArray: (Any) -> FloatArray, from: (FloatArray) -> Any) =
        GpuLayout(size, { s, at, v -> toArray(v).forEachIndexed { i, f -> s.set(F, at + i * 4L, f) } },
                  { s, at -> from(FloatArray(n) { s.get(F, at + it * 4L) }) }, align)
    private fun ints(size: Long, align: Long, n: Int, toArray: (Any) -> IntArray, from: (IntArray) -> Any) =
        GpuLayout(size, { s, at, v -> toArray(v).forEachIndexed { i, x -> s.set(I, at + i * 4L, x) } },
                  { s, at -> from(IntArray(n) { s.get(I, at + it * 4L) }) }, align)

    @Suppress("UNCHECKED_CAST")
    fun <T : Any> of(type: Class<T>, packing: GpuPacking): GpuLayout<T> = cache.getOrPut(type to packing) { make(type, packing) } as GpuLayout<T>

    private fun make(type: Class<*>, packing: GpuPacking): GpuLayout<*> {
        val std = packing != GpuPacking.C
        fun vecAlign(n: Int) = if (!std) 4L else if (n == 2) 8L else 16L
        return when (type) {
            java.lang.Float::class.java, Float::class.javaPrimitiveType -> GpuLayout<Float>(4, { s, at, v -> s.set(F, at, v) }, { s, at -> s.get(F, at) }, 4)
            java.lang.Integer::class.java, Int::class.javaPrimitiveType -> GpuLayout<Int>(4, { s, at, v -> s.set(I, at, v) }, { s, at -> s.get(I, at) }, 4)
            java.lang.Short::class.java, Short::class.javaPrimitiveType ->
                GpuLayout<Short>(2, { s, at, v -> s.set(ValueLayout.JAVA_SHORT_UNALIGNED, at, v) }, { s, at -> s.get(ValueLayout.JAVA_SHORT_UNALIGNED, at) }, 2)
            java.lang.Byte::class.java, Byte::class.javaPrimitiveType ->
                GpuLayout<Byte>(1, { s, at, v -> s.set(ValueLayout.JAVA_BYTE, at, v) }, { s, at -> s.get(ValueLayout.JAVA_BYTE, at) }, 1)
            java.lang.Long::class.java, Long::class.javaPrimitiveType ->
                GpuLayout<Long>(8, { s, at, v -> s.set(ValueLayout.JAVA_LONG_UNALIGNED, at, v) }, { s, at -> s.get(ValueLayout.JAVA_LONG_UNALIGNED, at) }, 8)
            java.lang.Double::class.java, Double::class.javaPrimitiveType ->
                GpuLayout<Double>(8, { s, at, v -> s.set(ValueLayout.JAVA_DOUBLE_UNALIGNED, at, v) }, { s, at -> s.get(ValueLayout.JAVA_DOUBLE_UNALIGNED, at) }, 8)
            // C++'s bool is a byte; a shader's is 4.
            java.lang.Boolean::class.java, Boolean::class.javaPrimitiveType ->
                if (std) GpuLayout<Boolean>(4, { s, at, v -> s.set(I, at, if (v) 1 else 0) }, { s, at -> s.get(I, at) != 0 }, 4)
                else GpuLayout<Boolean>(1, { s, at, v -> s.set(ValueLayout.JAVA_BYTE, at, if (v) 1 else 0) }, { s, at -> s.get(ValueLayout.JAVA_BYTE, at).toInt() != 0 }, 1)
            Vec2::class.java -> floats(8, vecAlign(2), 2, { (it as Vec2).toArray() }) { Vec2(it[0], it[1]) }
            Vec3::class.java -> floats(12, vecAlign(3), 3, { (it as Vec3).toArray() }) { Vec3(it[0], it[1], it[2]) }
            Vec4::class.java -> floats(16, vecAlign(4), 4, { (it as Vec4).toArray() }) { Vec4(it[0], it[1], it[2], it[3]) }
            Quat::class.java -> floats(16, vecAlign(4), 4, { (it as Quat).toArray() }) { Quat(it[0], it[1], it[2], it[3]) }
            IVec2::class.java -> ints(8, vecAlign(2), 2, { v -> (v as IVec2).let { intArrayOf(it.x, it.y) } }) { IVec2(it[0], it[1]) }
            IVec3::class.java -> ints(12, vecAlign(3), 3, { v -> (v as IVec3).let { intArrayOf(it.x, it.y, it.z) } }) { IVec3(it[0], it[1], it[2]) }
            IVec4::class.java -> ints(16, vecAlign(4), 4, { v -> (v as IVec4).let { intArrayOf(it.x, it.y, it.z, it.w) } }) { IVec4(it[0], it[1], it[2], it[3]) }
            UVec2::class.java -> ints(8, vecAlign(2), 2, { v -> (v as UVec2).let { intArrayOf(it.x.toInt(), it.y.toInt()) } }) { UVec2(it[0], it[1]) }
            UVec3::class.java -> ints(12, vecAlign(3), 3, { v -> (v as UVec3).let { intArrayOf(it.x.toInt(), it.y.toInt(), it.z.toInt()) } }) { UVec3(it[0], it[1], it[2]) }
            UVec4::class.java -> ints(16, vecAlign(4), 4, { v -> (v as UVec4).let { intArrayOf(it.x.toInt(), it.y.toInt(), it.z.toInt(), it.w.toInt()) } }) { UVec4(it[0], it[1], it[2], it[3]) }
            Mat4::class.java -> floats(64, vecAlign(4), 16, { (it as Mat4).toArray() }) { Mat4(it) }
            // A mat3's columns: three floats each in C++, padded to vec4s in a shader's block.
            Mat3::class.java ->
                if (!std) floats(36, 4, 9, { (it as Mat3).toArray() }) { Mat3(it) }
                else GpuLayout<Mat3>(48, { s, at, m -> for (c in 0 until 3) m[c].toArray().forEachIndexed { r, f -> s.set(F, at + c * 16L + r * 4L, f) } },
                                     { s, at -> Mat3(FloatArray(9) { s.get(F, at + (it / 3) * 16L + (it % 3) * 4L) }) }, 16)
            else -> when {
                type.isEnum -> enumLayout(type)
                type.isArray || Collection::class.java.isAssignableFrom(type) ->
                    throw IllegalArgumentException("${type.simpleName}: an array or list inside a struct has no fixed size in GPU memory; use separate fields, or a buffer of its own")
                else -> struct(type, packing)
            }
        }
    }

    /** An enum as 4 bytes: its `value` (Koral's enums carry the C++ one) or else its ordinal. */
    private fun enumLayout(type: Class<*>): GpuLayout<Any> {
        val constants = type.enumConstants.toList()
        val valueOf = type.methods.firstOrNull { it.name == "getValue" && it.parameterCount == 0 && it.returnType == Int::class.javaPrimitiveType }
        fun number(e: Any) = valueOf?.invoke(e) as Int? ?: (e as Enum<*>).ordinal
        val byNumber = constants.associateBy { number(it!!) }
        return GpuLayout(4, { s, at, v -> s.set(I, at, number(v)) }, { s, at -> byNumber[s.get(I, at)] ?: constants.first()!! }, 4)
    }

    private class Member(val name: String, val layout: GpuLayout<Any>, val offset: Long, val get: MethodHandle)

    /** A class's fields, in declaration order, each at its alignment; constructed back through its constructor. */
    private fun struct(type: Class<*>, packing: GpuPacking): GpuLayout<Any> {
        val lookup = MethodHandles.lookup()
        val fields = type.declaredFields.filter { !Modifier.isStatic(it.modifiers) && !it.isSynthetic }
        require(fields.isNotEmpty()) {
            "${type.simpleName} has no fields to lay out: a buffer takes numbers, vectors, matrices, and classes made of them"
        }
        var offset = 0L
        var align = 1L
        val members = fields.map { f ->
            f.isAccessible = true
            @Suppress("UNCHECKED_CAST")
            val layout = try { of(f.type, packing) as GpuLayout<Any> } catch (e: IllegalArgumentException) {
                throw IllegalArgumentException("${type.simpleName}.${f.name}: ${e.message}", e)
            }
            offset = (offset + layout.alignment - 1) / layout.alignment * layout.alignment
            align = maxOf(align, layout.alignment)
            Member(f.name, layout, offset, lookup.unreflectGetter(f)).also { offset += layout.size }
        }
        if (packing == GpuPacking.Std140) align = maxOf(align, 16)
        val size = (offset + align - 1) / align * align

        // To read back: the constructor taking every field in order (Kotlin adds a marker parameter when one
        // is an unsigned or other inline-class type), or else a no-argument one and the fields set one by one.
        val types = fields.map { it.type }
        @Suppress("UNCHECKED_CAST")
        val constructor = type.declaredConstructors.firstOrNull { c ->
            val p = c.parameterTypes.toList()
            p == types || (p.size == types.size + 1 && p.dropLast(1) == types && p.last().name == "kotlin.jvm.internal.DefaultConstructorMarker")
        }?.also { it.isAccessible = true } as Constructor<Any>?
        val noArgs = type.declaredConstructors.firstOrNull { it.parameterCount == 0 }?.also { it.isAccessible = true }
        val read: ((MemorySegment, Long) -> Any)? = when {
            constructor != null -> { s, at ->
                val args = members.map { it.layout.read!!(s, at + it.offset) }
                if (constructor.parameterCount == args.size) constructor.newInstance(*args.toTypedArray())
                else constructor.newInstance(*(args + listOf(null)).toTypedArray())
            }
            noArgs != null -> { s, at -> noArgs.newInstance().also { o -> members.zip(fields).forEach { (m, f) -> f.set(o, m.layout.read!!(s, at + m.offset)) } } }
            else -> null
        }
        return GpuLayout(size, { s, at, v -> members.forEach { m -> m.layout.write(s, at + m.offset, m.get.invoke(v)) } }, read, align)
    }
}

/** The layout of [T], derived from its class: see [GpuLayout.of]. */
inline fun <reified T : Any> gpuLayout(packing: GpuPacking = GpuPacking.C): GpuLayout<T> = GpuLayout.of(T::class, packing)

/** Room for [count] values of [T] laid out with [packing]. */
inline fun <reified T : Any> Buffer.Builder.setInstanceCount(count: Long, packing: GpuPacking = GpuPacking.C) =
    setInstanceCount(count, GpuLayout.of(T::class, packing))

/** [count] values of [T] (laid out with [packing]) from the [offset]th (-1: the rest). */
inline fun <reified T : Any> Buffer.readAs(count: Int = -1, offset: Long = 0, packing: GpuPacking = GpuPacking.C): List<T> =
    read(GpuLayout.of(T::class, packing), count, offset)

/** [Buffer.readAsync], as values of [T]. */
suspend inline fun <reified T : Any> Buffer.readAsyncAs(count: Int = -1, offset: Long = 0, packing: GpuPacking = GpuPacking.C): List<T> =
    readAsync(GpuLayout.of(T::class, packing), count, offset)

/** The mapped memory, as values of [T]. */
inline fun <reified T : Any> Buffer.Mapping.readAs(packing: GpuPacking = GpuPacking.C): List<T> {
    val layout = GpuLayout.of(T::class, packing)
    return layout.unpack(segment, (size / layout.stride).toInt())
}
