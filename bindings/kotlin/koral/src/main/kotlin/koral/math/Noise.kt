package koral

import kotlin.math.floor
import kotlin.math.sqrt

/** kor::FractalType: how a fractal's octaves combine. */
enum class FractalType { Fbm, Ridged, Turbulence }

/** kor::FractalOptions. */
data class FractalOptions(val type: FractalType = FractalType.Fbm, val octaves: Int = 5, val lacunarity: Float = 2f, val gain: Float = 0.5f)

/**
 * kor::Noise: coherent noise from a seed. The same seed gives the same field, bit for bit, here, in C++, C and
 * C#. perlin, simplex and value return roughly [-1, 1]; cellular the distance to the nearest feature point.
 */
class Noise(val seed: UInt = 0u) {
    enum class Kind { Perlin, Simplex, Value, Cellular }

    private val perm = IntArray(512)

    init {
        val p = IntArray(256) { it }
        Random(seed.toULong()).shuffle(p)
        for (i in 0 until 512) perm[i] = p[i and 255]
    }

    private fun perm(i: Int) = perm[i and 511]

    fun perlin(x: Float, y: Float): Float {
        val xi = floorToInt(x); val yi = floorToInt(y)
        val bx = xi and 255; val by = yi and 255
        val fx = x - xi.toFloat(); val fy = y - yi.toFloat()
        val u = fade(fx); val v = fade(fy)
        val a = perm(bx) + by; val b = perm(bx + 1) + by
        val n = mix(mix(grad(perm(a), fx, fy), grad(perm(b), fx - 1f, fy), u),
                    mix(grad(perm(a + 1), fx, fy - 1f), grad(perm(b + 1), fx - 1f, fy - 1f), u), v)
        return n * 0.5f
    }

    fun perlin(x: Float, y: Float, z: Float): Float {
        val xi = floorToInt(x); val yi = floorToInt(y); val zi = floorToInt(z)
        val bx = xi and 255; val by = yi and 255; val bz = zi and 255
        val fx = x - xi.toFloat(); val fy = y - yi.toFloat(); val fz = z - zi.toFloat()
        val u = fade(fx); val v = fade(fy); val w = fade(fz)
        val a = perm(bx) + by; val aa = perm(a) + bz; val ab = perm(a + 1) + bz
        val b = perm(bx + 1) + by; val ba = perm(b) + bz; val bb = perm(b + 1) + bz
        return mix(mix(mix(grad(perm(aa), fx, fy, fz), grad(perm(ba), fx - 1f, fy, fz), u),
                       mix(grad(perm(ab), fx, fy - 1f, fz), grad(perm(bb), fx - 1f, fy - 1f, fz), u), v),
                   mix(mix(grad(perm(aa + 1), fx, fy, fz - 1f), grad(perm(ba + 1), fx - 1f, fy, fz - 1f), u),
                       mix(grad(perm(ab + 1), fx, fy - 1f, fz - 1f), grad(perm(bb + 1), fx - 1f, fy - 1f, fz - 1f), u), v), w)
    }

    fun simplex(xin: Float, yin: Float): Float {
        val s = (xin + yin) * F2
        val i = floorToInt(xin + s); val j = floorToInt(yin + s)
        val t = (i + j).toFloat() * G2
        val x0 = xin - (i.toFloat() - t); val y0 = yin - (j.toFloat() - t)
        val i1 = if (x0 > y0) 1 else 0; val j1 = if (x0 > y0) 0 else 1
        val x1 = x0 - i1.toFloat() + G2; val y1 = y0 - j1.toFloat() + G2
        val x2 = x0 - 1f + 2f * G2; val y2 = y0 - 1f + 2f * G2
        val ii = i and 255; val jj = j and 255
        val g0 = perm(ii + perm(jj)) % 12; val g1 = perm(ii + i1 + perm(jj + j1)) % 12; val g2 = perm(ii + 1 + perm(jj + 1)) % 12
        return 70f * (corner2(g0, x0, y0) + corner2(g1, x1, y1) + corner2(g2, x2, y2))
    }

    fun simplex(xin: Float, yin: Float, zin: Float): Float {
        val s = (xin + yin + zin) * F3
        val i = floorToInt(xin + s); val j = floorToInt(yin + s); val k = floorToInt(zin + s)
        val t = (i + j + k).toFloat() * G3
        val x0 = xin - (i.toFloat() - t); val y0 = yin - (j.toFloat() - t); val z0 = zin - (k.toFloat() - t)
        val o: IntArray = if (x0 >= y0) {
            if (y0 >= z0) intArrayOf(1, 0, 0, 1, 1, 0) else if (x0 >= z0) intArrayOf(1, 0, 0, 1, 0, 1) else intArrayOf(0, 0, 1, 1, 0, 1)
        } else {
            if (y0 < z0) intArrayOf(0, 0, 1, 0, 1, 1) else if (x0 < z0) intArrayOf(0, 1, 0, 0, 1, 1) else intArrayOf(0, 1, 0, 1, 1, 0)
        }
        val x1 = x0 - o[0].toFloat() + G3; val y1 = y0 - o[1].toFloat() + G3; val z1 = z0 - o[2].toFloat() + G3
        val x2 = x0 - o[3].toFloat() + 2f * G3; val y2 = y0 - o[4].toFloat() + 2f * G3; val z2 = z0 - o[5].toFloat() + 2f * G3
        val x3 = x0 - 1f + 3f * G3; val y3 = y0 - 1f + 3f * G3; val z3 = z0 - 1f + 3f * G3
        val ii = i and 255; val jj = j and 255; val kk = k and 255
        val g0 = perm(ii + perm(jj + perm(kk))) % 12
        val g1 = perm(ii + o[0] + perm(jj + o[1] + perm(kk + o[2]))) % 12
        val g2 = perm(ii + o[3] + perm(jj + o[4] + perm(kk + o[5]))) % 12
        val g3 = perm(ii + 1 + perm(jj + 1 + perm(kk + 1))) % 12
        return 32f * (corner3(g0, x0, y0, z0) + corner3(g1, x1, y1, z1) + corner3(g2, x2, y2, z2) + corner3(g3, x3, y3, z3))
    }

    fun value(x: Float, y: Float): Float {
        val xi = floorToInt(x); val yi = floorToInt(y)
        val bx = xi and 255; val by = yi and 255
        val u = fade(x - xi.toFloat()); val v = fade(y - yi.toFloat())
        fun at(dx: Int, dy: Int) = perm(perm(bx + dx) + by + dy).toFloat() * (2f / 255f) - 1f
        return mix(mix(at(0, 0), at(1, 0), u), mix(at(0, 1), at(1, 1), u), v)
    }

    fun value(x: Float, y: Float, z: Float): Float {
        val xi = floorToInt(x); val yi = floorToInt(y); val zi = floorToInt(z)
        val bx = xi and 255; val by = yi and 255; val bz = zi and 255
        val u = fade(x - xi.toFloat()); val v = fade(y - yi.toFloat()); val w = fade(z - zi.toFloat())
        fun at(dx: Int, dy: Int, dz: Int) = perm(perm(perm(bx + dx) + by + dy) + bz + dz).toFloat() * (2f / 255f) - 1f
        return mix(mix(mix(at(0, 0, 0), at(1, 0, 0), u), mix(at(0, 1, 0), at(1, 1, 0), u), v),
                   mix(mix(at(0, 0, 1), at(1, 0, 1), u), mix(at(0, 1, 1), at(1, 1, 1), u), v), w)
    }

    fun cellular(x: Float, y: Float): Float {
        val xi = floorToInt(x); val yi = floorToInt(y)
        var best = 8f
        for (dy in -1..1) for (dx in -1..1) {
            val h = perm(perm((xi + dx) and 255) + ((yi + dy) and 255))
            val fx = (xi + dx).toFloat() + perm(h).toFloat() * (1f / 255f) - x
            val fy = (yi + dy).toFloat() + perm(h + 1).toFloat() * (1f / 255f) - y
            val d = fx * fx + fy * fy
            if (d < best) best = d
        }
        return sqrt(best)
    }

    fun cellular(x: Float, y: Float, z: Float): Float {
        val xi = floorToInt(x); val yi = floorToInt(y); val zi = floorToInt(z)
        var best = 8f
        for (dz in -1..1) for (dy in -1..1) for (dx in -1..1) {
            val h = perm(perm(perm((xi + dx) and 255) + ((yi + dy) and 255)) + ((zi + dz) and 255))
            val fx = (xi + dx).toFloat() + perm(h).toFloat() * (1f / 255f) - x
            val fy = (yi + dy).toFloat() + perm(h + 1).toFloat() * (1f / 255f) - y
            val fz = (zi + dz).toFloat() + perm(h + 2).toFloat() * (1f / 255f) - z
            val d = fx * fx + fy * fy + fz * fz
            if (d < best) best = d
        }
        return sqrt(best)
    }

    fun perlin(p: Vec2) = perlin(p.x, p.y)
    fun perlin(p: Vec3) = perlin(p.x, p.y, p.z)
    fun simplex(p: Vec2) = simplex(p.x, p.y)
    fun simplex(p: Vec3) = simplex(p.x, p.y, p.z)
    fun value(p: Vec2) = value(p.x, p.y)
    fun value(p: Vec3) = value(p.x, p.y, p.z)
    fun cellular(p: Vec2) = cellular(p.x, p.y)
    fun cellular(p: Vec3) = cellular(p.x, p.y, p.z)

    fun sample(kind: Kind, p: Vec2) = when (kind) { Kind.Perlin -> perlin(p); Kind.Simplex -> simplex(p); Kind.Value -> value(p); Kind.Cellular -> cellular(p) }
    fun sample(kind: Kind, p: Vec3) = when (kind) { Kind.Perlin -> perlin(p); Kind.Simplex -> simplex(p); Kind.Value -> value(p); Kind.Cellular -> cellular(p) }

    /** Octaves of [kind] combined per [options], normalised. */
    fun fractal(kind: Kind, p: Vec2, options: FractalOptions = FractalOptions()) = accumulate(options) { f -> sample(kind, p * f) }
    fun fractal(kind: Kind, p: Vec3, options: FractalOptions = FractalOptions()) = accumulate(options) { f -> sample(kind, p * f) }

    private inline fun accumulate(options: FractalOptions, sampleAt: (Float) -> Float): Float {
        var sum = 0f
        var norm = 0f
        var amplitude = 1f
        var frequency = 1f
        repeat(maxOf(1, options.octaves)) {
            var n = sampleAt(frequency)
            when (options.type) {
                FractalType.Fbm -> {}
                FractalType.Ridged -> { n = 1f - kotlin.math.abs(n); n *= n }
                FractalType.Turbulence -> n = kotlin.math.abs(n)
            }
            sum += n * amplitude
            norm += amplitude
            amplitude *= options.gain
            frequency *= options.lacunarity
        }
        return sum / norm
    }

    private companion object {
        const val F2 = 0.36602540378443865f
        const val G2 = 0.21132486540518713f
        const val F3 = 1f / 3f
        const val G3 = 1f / 6f
        val GRAD3 = floatArrayOf(1f, 1f, 0f, -1f, 1f, 0f, 1f, -1f, 0f, -1f, -1f, 0f, 1f, 0f, 1f, -1f, 0f, 1f,
                                 1f, 0f, -1f, -1f, 0f, -1f, 0f, 1f, 1f, 0f, -1f, 1f, 0f, 1f, -1f, 0f, -1f, -1f)

        fun floorToInt(v: Float) = floor(v).toInt()
        fun fade(t: Float) = t * t * t * (t * (t * 6f - 15f) + 10f)
        fun mix(a: Float, b: Float, t: Float) = a + t * (b - a)
        fun grad(hash: Int, x: Float, y: Float, z: Float): Float {
            val h = hash and 15
            val u = if (h < 8) x else y
            val v = if (h < 4) y else if (h == 12 || h == 14) x else z
            return (if (h and 1 == 0) u else -u) + (if (h and 2 == 0) v else -v)
        }
        fun grad(hash: Int, x: Float, y: Float): Float {
            val h = hash and 7
            val u = if (h < 4) x else y
            val v = if (h < 4) y else x
            return (if (h and 1 == 0) u else -u) + (if (h and 2 == 0) 2f * v else -2f * v)
        }
        fun corner2(g: Int, x: Float, y: Float): Float {
            var t = 0.5f - x * x - y * y
            if (t < 0f) return 0f
            t *= t
            return t * t * (GRAD3[g * 3] * x + GRAD3[g * 3 + 1] * y)
        }
        fun corner3(g: Int, x: Float, y: Float, z: Float): Float {
            var t = 0.6f - x * x - y * y - z * z
            if (t < 0f) return 0f
            t *= t
            return t * t * (GRAD3[g * 3] * x + GRAD3[g * 3 + 1] * y + GRAD3[g * 3 + 2] * z)
        }
    }
}
