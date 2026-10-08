namespace Koral;

// kmath/color.h: colour as shaders see it, a Vec3/Vec4 of floats. What a person picked (hex codes, pickers,
// painted textures) is sRGB; what lighting adds and multiplies must be linear.
public static partial class KMath
{
    public static float SrgbToLinear(float c) => c <= 0.04045f ? c / 12.92f : MathF.Pow((c + 0.055f) / 1.055f, 2.4f);
    public static float LinearToSrgb(float c) => c <= 0.0031308f ? c * 12.92f : 1.055f * MathF.Pow(c, 1f / 2.4f) - 0.055f;
    public static Vec3 SrgbToLinear(Vec3 c) => new(SrgbToLinear(c.X), SrgbToLinear(c.Y), SrgbToLinear(c.Z));
    public static Vec3 LinearToSrgb(Vec3 c) => new(LinearToSrgb(c.X), LinearToSrgb(c.Y), LinearToSrgb(c.Z));
    public static Vec4 SrgbToLinear(Vec4 c) => new(SrgbToLinear((Vec3)c), c.W);
    public static Vec4 LinearToSrgb(Vec4 c) => new(LinearToSrgb((Vec3)c), c.W);

    /// <summary>0xRRGGBB as an opaque colour, channels in [0, 1].</summary>
    public static Vec4 ColorFromHex(uint rgb) => new(((rgb >> 16) & 0xff) / 255f, ((rgb >> 8) & 0xff) / 255f, (rgb & 0xff) / 255f, 1f);
    /// <summary>0xRRGGBBAA.</summary>
    public static Vec4 ColorFromHexA(uint rgba) { var c = ColorFromHex(rgba >> 8); c.W = (rgba & 0xff) / 255f; return c; }
    /// <summary>Perceived brightness of a linear colour.</summary>
    public static float Luminance(Vec3 linear) => Dot(linear, new Vec3(0.2126f, 0.7152f, 0.0722f));

    /// <summary>RGB to hue (a full turn is 1), saturation, value.</summary>
    public static Vec3 RgbToHsv(Vec3 c)
    {
        float max = CompMax(c), min = CompMin(c), d = max - min, h = 0f;
        if (d > 0f)
        {
            if (max == c.X) h = (c.Y - c.Z) / d + (c.Y < c.Z ? 6f : 0f);
            else if (max == c.Y) h = (c.Z - c.X) / d + 2f;
            else h = (c.X - c.Y) / d + 4f;
            h /= 6f;
        }
        return new Vec3(h, max > 0f ? d / max : 0f, max);
    }
    public static Vec3 HsvToRgb(Vec3 hsv)
    {
        float h = Fract(hsv.X) * 6f, s = hsv.Y, v = hsv.Z;
        int i = (int)h;
        float f = h - i, p = v * (1f - s), q = v * (1f - s * f), t = v * (1f - s * (1f - f));
        return (i % 6) switch
        {
            0 => new Vec3(v, t, p), 1 => new Vec3(q, v, p), 2 => new Vec3(p, v, t),
            3 => new Vec3(p, q, v), 4 => new Vec3(t, p, v), _ => new Vec3(v, p, q),
        };
    }
    public static Vec3 RgbToHsl(Vec3 c)
    {
        float max = CompMax(c), min = CompMin(c), l = (max + min) * 0.5f, d = max - min;
        if (d == 0f) return new Vec3(0f, 0f, l);
        float s = l > 0.5f ? d / (2f - max - min) : d / (max + min);
        float h;
        if (max == c.X) h = (c.Y - c.Z) / d + (c.Y < c.Z ? 6f : 0f);
        else if (max == c.Y) h = (c.Z - c.X) / d + 2f;
        else h = (c.X - c.Y) / d + 4f;
        return new Vec3(h / 6f, s, l);
    }
    public static Vec3 HslToRgb(Vec3 hsl)
    {
        float h = Fract(hsl.X), s = hsl.Y, l = hsl.Z;
        if (s == 0f) return new Vec3(l);
        float q = l < 0.5f ? l * (1f + s) : l + s - l * s, p = 2f * l - q;
        float Channel(float t)
        {
            t = Fract(t);
            if (t < 1f / 6f) return p + (q - p) * 6f * t;
            if (t < 0.5f) return q;
            if (t < 2f / 3f) return p + (q - p) * (2f / 3f - t) * 6f;
            return p;
        }
        return new Vec3(Channel(h + 1f / 3f), Channel(h), Channel(h - 1f / 3f));
    }
    /// <summary>Linear sRGB to Oklab: perceptually even, for gradients and blends.</summary>
    public static Vec3 LinearToOklab(Vec3 c)
    {
        float l = MathF.Cbrt(0.4122214708f * c.X + 0.5363325363f * c.Y + 0.0514459929f * c.Z);
        float m = MathF.Cbrt(0.2119034982f * c.X + 0.6806995451f * c.Y + 0.1073969566f * c.Z);
        float s = MathF.Cbrt(0.0883024619f * c.X + 0.2817188376f * c.Y + 0.6299787005f * c.Z);
        return new Vec3(0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s,
                        1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s,
                        0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s);
    }
    public static Vec3 OklabToLinear(Vec3 lab)
    {
        float l = lab.X + 0.3963377774f * lab.Y + 0.2158037573f * lab.Z;
        float m = lab.X - 0.1055613458f * lab.Y - 0.0638541728f * lab.Z;
        float s = lab.X - 0.0894841775f * lab.Y - 1.2914855480f * lab.Z;
        float l3 = l * l * l, m3 = m * m * m, s3 = s * s * s;
        return new Vec3(4.0767416621f * l3 - 3.3077115913f * m3 + 0.2309699292f * s3,
                        -1.2684380046f * l3 + 2.6097574011f * m3 - 0.3413193965f * s3,
                        -0.0041960863f * l3 - 0.7034186147f * m3 + 1.7076147010f * s3);
    }
    public static Vec3 MixOklab(Vec3 a, Vec3 b, float t) => OklabToLinear(Lerp(LinearToOklab(a), LinearToOklab(b), t));
    /// <summary>The linear colour of a black body at <paramref name="kelvin"/>, brightest channel 1.</summary>
    public static Vec3 ColorTemperature(float kelvin)
    {
        float t = Clamp(kelvin, 1000f, 40000f) / 100f, r, g, b;
        if (t <= 66f)
        {
            r = 255f;
            g = 99.4708025861f * MathF.Log(t) - 161.1195681661f;
            b = t <= 19f ? 0f : 138.5177312231f * MathF.Log(t - 10f) - 305.0447927307f;
        }
        else
        {
            r = 329.698727446f * MathF.Pow(t - 60f, -0.1332047592f);
            g = 288.1221695283f * MathF.Pow(t - 60f, -0.0755148492f);
            b = 255f;
        }
        Vec3 linear = SrgbToLinear(Clamp(new Vec3(r, g, b) / 255f, 0f, 1f));
        return linear / Max(CompMax(linear), 1e-6f);
    }

    /// <summary>A unit normal in two snorm16s (octahedral mapping).</summary>
    public static uint PackOctahedral(Vec3 n)
    {
        Vec2 p = new Vec2(n.X, n.Y) * (1f / (Abs(n.X) + Abs(n.Y) + Abs(n.Z)));
        if (n.Z < 0f) p = new Vec2((1f - Abs(p.Y)) * (p.X >= 0f ? 1f : -1f), (1f - Abs(p.X)) * (p.Y >= 0f ? 1f : -1f));
        static uint Snorm(float v) => (ushort)(short)(Clamp(v, -1f, 1f) * 32767f + (v >= 0f ? 0.5f : -0.5f));
        return Snorm(p.X) | (Snorm(p.Y) << 16);
    }
    public static Vec3 UnpackOctahedral(uint packed)
    {
        var p = new Vec2(Max((short)(ushort)packed / 32767f, -1f), Max((short)(ushort)(packed >> 16) / 32767f, -1f));
        var n = new Vec3(p.X, p.Y, 1f - Abs(p.X) - Abs(p.Y));
        float t = Max(-n.Z, 0f);
        n.X += n.X >= 0f ? -t : t;
        n.Y += n.Y >= 0f ? -t : t;
        return Normalize(n);
    }
}
