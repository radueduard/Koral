package koral

import kotlin.math.cos
import kotlin.math.ln
import kotlin.math.sin
import kotlin.math.sqrt

/**
 * kor::Random: PCG32, seedable — the same seed gives the same sequence here, in C++, C and C#. Not for
 * cryptography. Exactly reproducible across them: the integer draws, nextFloat/nextDouble/nextBool, the
 * rejection-sampled insideUnitCircle/insideUnitSphere, shuffle and pick; the rest go through kotlin.math and may
 * differ in the last bit.
 */
class Random(seed: ULong = DefaultSeed, stream: ULong = DefaultStream) {
    private var state = 0uL
    private var increment = 0uL

    init { seed(seed, stream) }

    fun seed(seed: ULong, stream: ULong = DefaultStream) {
        state = 0uL
        increment = (stream shl 1) or 1uL
        nextU32()
        state += seed
        nextU32()
    }

    fun nextU32(): UInt {
        val old = state
        state = old * Multiplier + increment
        val xorShifted = (((old shr 18) xor old) shr 27).toUInt()
        val rot = (old shr 59).toInt()
        return (xorShifted shr rot) or (xorShifted shl ((-rot) and 31))
    }
    /** Two nextU32s, the first in the high half. */
    fun nextU64(): ULong { val hi = nextU32().toULong(); return (hi shl 32) or nextU32().toULong() }
    /** Uniform in [0, bound), without modulo bias. */
    fun nextU32(bound: UInt): UInt {
        if (bound == 0u) return 0u
        val threshold = (0u - bound) % bound
        while (true) {
            val r = nextU32()
            if (r >= threshold) return r % bound
        }
    }
    /** Uniform in [min, maxExclusive). */
    fun nextInt(min: Int, maxExclusive: Int): Int =
        if (maxExclusive <= min) min else (min.toLong() + nextU32((maxExclusive.toLong() - min.toLong()).toUInt()).toLong()).toInt()
    /** Uniform in [0, 1), 24 random bits. */
    fun nextFloat(): Float = (nextU32() shr 8).toFloat() * (1f / 16777216f)
    fun nextFloat(min: Float, max: Float) = min + (max - min) * nextFloat()
    /** Uniform in [0, 1), 53 random bits. */
    fun nextDouble(): Double = (nextU64() shr 11).toDouble() * (1.0 / 9007199254740992.0)
    fun nextBool(probability: Float = 0.5f) = nextFloat() < probability

    /** Normally distributed (Marsaglia's polar method). */
    fun nextGaussian(mean: Float = 0f, standardDeviation: Float = 1f): Float {
        var u: Float
        var v: Float
        var s: Float
        do {
            u = nextFloat() * 2f - 1f
            v = nextFloat() * 2f - 1f
            s = u * u + v * v
        } while (s >= 1f || s == 0f)
        return mean + standardDeviation * u * sqrt(-2f * ln(s) / s)
    }

    fun insideUnitCircle(): Vec2 {
        while (true) {
            val p = Vec2(nextFloat() * 2f - 1f, nextFloat() * 2f - 1f)
            if (dot(p, p) <= 1f) return p
        }
    }
    fun insideUnitSphere(): Vec3 {
        while (true) {
            val p = Vec3(nextFloat() * 2f - 1f, nextFloat() * 2f - 1f, nextFloat() * 2f - 1f)
            if (dot(p, p) <= 1f) return p
        }
    }
    fun onUnitCircle(): Vec2 { val angle = nextFloat() * Tau; return Vec2(cos(angle), sin(angle)) }
    fun onUnitSphere(): Vec3 {
        val z = nextFloat() * 2f - 1f
        val angle = nextFloat() * Tau
        val r = sqrt(maxOf(0f, 1f - z * z))
        return Vec3(r * cos(angle), r * sin(angle), z)
    }
    /** A uniformly distributed rotation (Shoemake). */
    fun rotation(): Quat {
        val u1 = nextFloat()
        val u2 = nextFloat() * Tau
        val u3 = nextFloat() * Tau
        val a = sqrt(1f - u1)
        val b = sqrt(u1)
        return Quat(a * sin(u2), a * cos(u2), b * sin(u3), b * cos(u3))
    }
    fun insideBox(min: Vec3, max: Vec3) = Vec3(nextFloat(min.x, max.x), nextFloat(min.y, max.y), nextFloat(min.z, max.z))

    /** Fisher–Yates, in place. */
    fun <T> shuffle(items: MutableList<T>) {
        for (i in items.size downTo 2) {
            val j = nextU32(i.toUInt()).toInt()
            val t = items[i - 1]; items[i - 1] = items[j]; items[j] = t
        }
    }
    fun shuffle(items: IntArray) {
        for (i in items.size downTo 2) {
            val j = nextU32(i.toUInt()).toInt()
            val t = items[i - 1]; items[i - 1] = items[j]; items[j] = t
        }
    }
    fun <T> pick(items: List<T>): T = items[nextU32(items.size.toUInt()).toInt()]

    /** Skips [count] outputs in O(log count). */
    fun advance(count: ULong) {
        var c = count
        var accMult = 1uL
        var accPlus = 0uL
        var curMult = Multiplier
        var curPlus = increment
        while (c > 0uL) {
            if (c and 1uL != 0uL) { accMult *= curMult; accPlus = accPlus * curMult + curPlus }
            curPlus = (curMult + 1uL) * curPlus
            curMult *= curMult
            c = c shr 1
        }
        state = accMult * state + accPlus
    }

    /** The full state (state, increment), to save and restore a generator mid-sequence. */
    var fullState: Pair<ULong, ULong>
        get() = state to increment
        set(value) { state = value.first; increment = value.second or 1uL }

    companion object {
        const val DefaultSeed = 0x853c49e6748fea9buL
        const val DefaultStream = 0xda3e39cb94b95bdbuL
        private const val Multiplier = 6364136223846793005uL

        /** Seeded from the operating system's entropy: different every run. */
        fun fromEntropy(): Random {
            val r = java.security.SecureRandom()
            return Random(r.nextLong().toULong(), r.nextLong().toULong())
        }
        private val local = ThreadLocal.withInitial { fromEntropy() }
        /** This thread's own generator, seeded from entropy the first time it is used. */
        val threadLocal: Random get() = local.get()
    }
}

/** A well-mixed 32-bit hash of a 32-bit value. */
fun hash(v: UInt): UInt {
    val state = v * 747796405u + 2891336453u
    val word = ((state shr ((state shr 28) + 4u).toInt()) xor state) * 277803737u
    return (word shr 22) xor word
}
fun hashCombine(seed: UInt, v: UInt) = hash(seed xor (v + 0x9e3779b9u + (seed shl 6) + (seed shr 2)))
/** A uniform float in [0, 1) that depends only on [v]. */
fun hashToFloat(v: UInt) = (hash(v) shr 8).toFloat() * (1f / 16777216f)
