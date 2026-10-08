package koral.net

import koral.Quat
import koral.Vec2
import koral.Vec3
import koral.Vec4
import kotlin.math.abs
import kotlin.math.sqrt

/**
 * knet::BitWriter, in Kotlin: the same bits as C++'s for the same calls, so a message written here reads there.
 * Bit i of the stream is bit (i % 8) of byte (i / 8); values go low bit first. Unsigned values are carried in
 * Long/Int with their bits.
 */
class BitWriter {
    private var bytes = ByteArray(64)
    private var size = 0
    var bitCount: Long = 0
        private set

    fun writeBits(value: Long, bits: Int) {
        for (i in 0 until bits) {
            if ((bitCount and 7L) == 0L) {
                if (size == bytes.size) bytes = bytes.copyOf(bytes.size * 2)
                bytes[size++] = 0
            }
            if ((value ushr i) and 1L != 0L) bytes[size - 1] = (bytes[size - 1].toInt() or (1 shl (bitCount and 7L).toInt())).toByte()
            ++bitCount
        }
    }
    fun writeBool(value: Boolean) = writeBits(if (value) 1 else 0, 1)
    fun writeU8(v: Int) = writeBits(v.toLong() and 0xff, 8)
    fun writeU16(v: Int) = writeBits(v.toLong() and 0xffff, 16)
    fun writeU32(v: Int) = writeBits(v.toLong() and 0xffffffffL, 32)
    fun writeU64(v: Long) = writeBits(v, 64)
    fun writeVarUInt(value: Long) {
        var v = value
        do {
            val group = v and 0x7f
            v = v ushr 7
            writeBits(group or (if (v != 0L) 0x80 else 0), 8)
        } while (v != 0L)
    }
    fun writeVarInt(value: Long) = writeVarUInt((value shl 1) xor (value shr 63))
    fun writeFloat(value: Float) = writeBits(value.toRawBits().toLong() and 0xffffffffL, 32)
    fun writeDouble(value: Double) = writeBits(value.toRawBits(), 64)
    fun writeQuantized(value: Float, min: Float, max: Float, bits: Int) {
        val steps = (1L shl bits) - 1
        // std::fmin(std::fmax(v, min), max): a NaN becomes min.
        val clamped = if (value.isNaN()) min else value.coerceIn(min, max)
        val t = if (max > min) (clamped - min) / (max - min) else 0f
        writeBits(roundHalfAway(t.toDouble() * steps.toDouble()), bits)
    }
    fun writeString(text: String) = writeBytes(bytesOf(text))
    fun writeBytes(data: ByteArray) {
        writeVarUInt(data.size.toLong())
        for (b in data) writeBits(b.toLong() and 0xff, 8)
    }
    fun writeVec2(v: Vec2) { writeFloat(v.x); writeFloat(v.y) }
    fun writeVec3(v: Vec3) { writeFloat(v.x); writeFloat(v.y); writeFloat(v.z) }
    fun writeVec4(v: Vec4) { writeFloat(v.x); writeFloat(v.y); writeFloat(v.z); writeFloat(v.w) }
    /** Smallest three: 32 bits at the default. */
    fun writeQuat(q: Quat, bitsPerComponent: Int = 10) {
        val c = floatArrayOf(q.x, q.y, q.z, q.w)
        var largest = 0
        for (i in 1 until 4) if (abs(c[i]) > abs(c[largest])) largest = i
        val sign = if (c[largest] < 0f) -1f else 1f
        writeBits(largest.toLong(), 2)
        for (i in 0 until 4) if (i != largest) writeQuantized(c[i] * sign, -Bound, Bound, bitsPerComponent)
    }
    fun align() { while ((bitCount and 7L) != 0L) writeBits(0, 1) }

    fun toByteArray(): ByteArray = bytes.copyOf(size)
    fun clear() { size = 0; bitCount = 0 }

    internal companion object {
        const val Bound = 0.70710678f
        /** std::lround: halves away from zero. */
        fun roundHalfAway(v: Double): Long = if (v < 0) -kotlin.math.floor(-v + 0.5).toLong() else kotlin.math.floor(v + 0.5).toLong()
    }
}

/** knet::BitReader, in Kotlin. Never reads past its data: a short or malformed message makes it [failed], and reads return zeros. */
class BitReader(private val data: ByteArray) {
    private var bit = 0L
    var failed = false
        private set
    val bitsLeft: Long get() = if (failed) 0 else data.size * 8L - bit
    fun fail() { failed = true }

    fun readBits(bits: Int): Long {
        if (failed || bit + bits > data.size * 8L) { failed = true; return 0 }
        var value = 0L
        for (i in 0 until bits) {
            if ((data[(bit ushr 3).toInt()].toInt() ushr (bit and 7L).toInt()) and 1 != 0) value = value or (1L shl i)
            ++bit
        }
        return value
    }
    fun readBool() = readBits(1) != 0L
    fun readU8() = readBits(8).toInt()
    fun readU16() = readBits(16).toInt()
    fun readU32() = readBits(32).toInt()
    fun readU64() = readBits(64)
    fun readVarUInt(): Long {
        var value = 0L
        var shift = 0
        while (shift < 64) {
            val group = readBits(8)
            value = value or ((group and 0x7f) shl shift)
            if ((group and 0x80) == 0L || failed) return if (failed) 0 else value
            shift += 7
        }
        failed = true
        return 0
    }
    fun readVarInt(): Long { val z = readVarUInt(); return (z ushr 1) xor -(z and 1) }
    fun readFloat(): Float = Float.fromBits(readBits(32).toInt())
    fun readDouble(): Double = Double.fromBits(readBits(64))
    fun readQuantized(min: Float, max: Float, bits: Int): Float {
        val steps = (1L shl bits) - 1
        val q = readBits(bits)
        return min + (q.toDouble() / steps.toDouble()).toFloat() * (max - min)
    }
    fun readString(limit: Int = 1 shl 20): String = textOf(readBytes(limit))
    fun readBytes(limit: Int = 1 shl 26): ByteArray {
        val size = readVarUInt()
        if (size < 0 || size > limit || size * 8 > bitsLeft) { failed = true; return ByteArray(0) }
        return ByteArray(size.toInt()) { readBits(8).toByte() }
    }
    fun readVec2(): Vec2 { val x = readFloat(); return Vec2(x, readFloat()) }
    fun readVec3(): Vec3 { val x = readFloat(); val y = readFloat(); return Vec3(x, y, readFloat()) }
    fun readVec4(): Vec4 { val x = readFloat(); val y = readFloat(); val z = readFloat(); return Vec4(x, y, z, readFloat()) }
    fun readQuat(bitsPerComponent: Int = 10): Quat {
        val largest = readBits(2).toInt()
        val c = FloatArray(4)
        var sum = 0f
        for (i in 0 until 4) if (i != largest) { c[i] = readQuantized(-BitWriter.Bound, BitWriter.Bound, bitsPerComponent); sum += c[i] * c[i] }
        c[largest] = sqrt(maxOf(0f, 1f - sum))
        return koral.normalize(Quat(c[0], c[1], c[2], c[3]))
    }
    fun align() {
        while ((bit and 7L) != 0L) {
            if (bit >= data.size * 8L) { failed = true; return }
            ++bit
        }
    }
}
