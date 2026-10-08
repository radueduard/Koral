package koral

import kotlin.math.cbrt
import kotlin.math.ln
import kotlin.math.pow

// kmath/color.h: colour as shaders see it, a Vec3/Vec4 of floats. What a person picked (hex codes, pickers,
// painted textures) is sRGB; what lighting adds and multiplies must be linear.

fun srgbToLinear(c: Float) = if (c <= 0.04045f) c / 12.92f else ((c + 0.055f) / 1.055f).pow(2.4f)
fun linearToSrgb(c: Float) = if (c <= 0.0031308f) c * 12.92f else 1.055f * c.pow(1f / 2.4f) - 0.055f
fun srgbToLinear(c: Vec3) = Vec3(srgbToLinear(c.x), srgbToLinear(c.y), srgbToLinear(c.z))
fun linearToSrgb(c: Vec3) = Vec3(linearToSrgb(c.x), linearToSrgb(c.y), linearToSrgb(c.z))
fun srgbToLinear(c: Vec4) = Vec4(srgbToLinear(c.xyz), c.w)
fun linearToSrgb(c: Vec4) = Vec4(linearToSrgb(c.xyz), c.w)

/** 0xRRGGBB as an opaque colour, channels in [0, 1]. */
fun colorFromHex(rgb: UInt) = colorFromHex(rgb.toInt())
fun colorFromHex(rgb: Int) = Vec4(((rgb shr 16) and 0xff) / 255f, ((rgb shr 8) and 0xff) / 255f, (rgb and 0xff) / 255f, 1f)
/** 0xRRGGBBAA. */
fun colorFromHexA(rgba: Int) = colorFromHex(rgba ushr 8).copy(w = (rgba and 0xff) / 255f)
/** Perceived brightness of a linear colour. */
fun luminance(linear: Vec3) = dot(linear, Vec3(0.2126f, 0.7152f, 0.0722f))

/** RGB to hue (a full turn is 1), saturation, value. */
fun rgbToHsv(c: Vec3): Vec3 {
    val max = compMax(c)
    val min = compMin(c)
    val d = max - min
    var h = 0f
    if (d > 0f) {
        h = if (max == c.x) (c.y - c.z) / d + (if (c.y < c.z) 6f else 0f) else if (max == c.y) (c.z - c.x) / d + 2f else (c.x - c.y) / d + 4f
        h /= 6f
    }
    return Vec3(h, if (max > 0f) d / max else 0f, max)
}
fun hsvToRgb(hsv: Vec3): Vec3 {
    val h = fract(hsv.x) * 6f
    val s = hsv.y
    val v = hsv.z
    val i = h.toInt()
    val f = h - i.toFloat()
    val p = v * (1f - s); val q = v * (1f - s * f); val t = v * (1f - s * (1f - f))
    return when (i % 6) { 0 -> Vec3(v, t, p); 1 -> Vec3(q, v, p); 2 -> Vec3(p, v, t); 3 -> Vec3(p, q, v); 4 -> Vec3(t, p, v); else -> Vec3(v, p, q) }
}
fun rgbToHsl(c: Vec3): Vec3 {
    val max = compMax(c)
    val min = compMin(c)
    val l = (max + min) * 0.5f
    val d = max - min
    if (d == 0f) return Vec3(0f, 0f, l)
    val s = if (l > 0.5f) d / (2f - max - min) else d / (max + min)
    val h = if (max == c.x) (c.y - c.z) / d + (if (c.y < c.z) 6f else 0f) else if (max == c.y) (c.z - c.x) / d + 2f else (c.x - c.y) / d + 4f
    return Vec3(h / 6f, s, l)
}
fun hslToRgb(hsl: Vec3): Vec3 {
    val h = fract(hsl.x)
    val s = hsl.y
    val l = hsl.z
    if (s == 0f) return Vec3(l)
    val q = if (l < 0.5f) l * (1f + s) else l + s - l * s
    val p = 2f * l - q
    fun channel(t0: Float): Float {
        val t = fract(t0)
        if (t < 1f / 6f) return p + (q - p) * 6f * t
        if (t < 0.5f) return q
        if (t < 2f / 3f) return p + (q - p) * (2f / 3f - t) * 6f
        return p
    }
    return Vec3(channel(h + 1f / 3f), channel(h), channel(h - 1f / 3f))
}
/** Linear sRGB to Oklab: perceptually even, for gradients and blends. */
fun linearToOklab(c: Vec3): Vec3 {
    val l = cbrt(0.4122214708f * c.x + 0.5363325363f * c.y + 0.0514459929f * c.z)
    val m = cbrt(0.2119034982f * c.x + 0.6806995451f * c.y + 0.1073969566f * c.z)
    val s = cbrt(0.0883024619f * c.x + 0.2817188376f * c.y + 0.6299787005f * c.z)
    return Vec3(0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s,
                1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s,
                0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s)
}
fun oklabToLinear(lab: Vec3): Vec3 {
    val l = lab.x + 0.3963377774f * lab.y + 0.2158037573f * lab.z
    val m = lab.x - 0.1055613458f * lab.y - 0.0638541728f * lab.z
    val s = lab.x - 0.0894841775f * lab.y - 1.2914855480f * lab.z
    val l3 = l * l * l; val m3 = m * m * m; val s3 = s * s * s
    return Vec3(4.0767416621f * l3 - 3.3077115913f * m3 + 0.2309699292f * s3,
                -1.2684380046f * l3 + 2.6097574011f * m3 - 0.3413193965f * s3,
                -0.0041960863f * l3 - 0.7034186147f * m3 + 1.7076147010f * s3)
}
fun mixOklab(a: Vec3, b: Vec3, t: Float) = oklabToLinear(lerp(linearToOklab(a), linearToOklab(b), t))
/** The linear colour of a black body at [kelvin], brightest channel 1. */
fun colorTemperature(kelvin: Float): Vec3 {
    val t = clamp(kelvin, 1000f, 40000f) / 100f
    val r: Float
    val g: Float
    val b: Float
    if (t <= 66f) {
        r = 255f
        g = 99.4708025861f * ln(t) - 161.1195681661f
        b = if (t <= 19f) 0f else 138.5177312231f * ln(t - 10f) - 305.0447927307f
    } else {
        r = 329.698727446f * (t - 60f).pow(-0.1332047592f)
        g = 288.1221695283f * (t - 60f).pow(-0.0755148492f)
        b = 255f
    }
    val linear = srgbToLinear(clamp(Vec3(r, g, b) / 255f, 0f, 1f))
    return linear / maxOf(compMax(linear), 1e-6f)
}

/** A unit normal in two snorm16s (octahedral mapping). */
fun packOctahedral(n: Vec3): Int {
    var p = Vec2(n.x, n.y) * (1f / (kotlin.math.abs(n.x) + kotlin.math.abs(n.y) + kotlin.math.abs(n.z)))
    if (n.z < 0f) p = Vec2((1f - kotlin.math.abs(p.y)) * (if (p.x >= 0f) 1f else -1f), (1f - kotlin.math.abs(p.x)) * (if (p.y >= 0f) 1f else -1f))
    fun snorm(v: Float) = (clamp(v, -1f, 1f) * 32767f + (if (v >= 0f) 0.5f else -0.5f)).toInt().toShort().toInt() and 0xffff
    return snorm(p.x) or (snorm(p.y) shl 16)
}
fun unpackOctahedral(packed: Int): Vec3 {
    val px = maxOf(packed.toShort() / 32767f, -1f)
    val py = maxOf((packed ushr 16).toShort() / 32767f, -1f)
    val nz = 1f - kotlin.math.abs(px) - kotlin.math.abs(py)
    val t = maxOf(-nz, 0f)
    return normalize(Vec3(px + (if (px >= 0f) -t else t), py + (if (py >= 0f) -t else t), nz))
}
