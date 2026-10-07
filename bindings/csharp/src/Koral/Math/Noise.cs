namespace Koral;

/// <summary>kor::FractalType: how a fractal's octaves combine.</summary>
public enum FractalType : byte { Fbm, Ridged, Turbulence }

/// <summary>kor::FractalOptions.</summary>
public record struct FractalOptions(FractalType Type = FractalType.Fbm, int Octaves = 5, float Lacunarity = 2f, float Gain = 0.5f)
{
    public FractalOptions() : this(FractalType.Fbm) { }
}

/// <summary>
/// kor::Noise: coherent noise from a seed. The same seed gives the same field, bit for bit, here, in C++, C
/// and Kotlin. Perlin, Simplex and Value return roughly [-1, 1]; Cellular the distance to the nearest
/// feature point.
/// </summary>
public sealed class Noise
{
    public enum Kind : byte { Perlin, Simplex, Value, Cellular }

    private readonly byte[] _perm = new byte[512];
    public uint Seed { get; }

    public Noise(uint seed = 0)
    {
        Seed = seed;
        var p = new byte[256];
        for (int i = 0; i < 256; ++i) p[i] = (byte)i;
        new Random(seed).Shuffle<byte>(p);
        for (int i = 0; i < 512; ++i) _perm[i] = p[i & 255];
    }

    private int Perm(int i) => _perm[i & 511];
    private static float Fade(float t) => t * t * t * (t * (t * 6f - 15f) + 10f);
    private static float Mix(float a, float b, float t) => a + t * (b - a);
    private static int FloorToInt(float v) => (int)MathF.Floor(v);
    private static float Grad(int hash, float x, float y, float z)
    {
        int h = hash & 15;
        float u = h < 8 ? x : y;
        float v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
        return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
    }
    private static float Grad(int hash, float x, float y)
    {
        int h = hash & 7;
        float u = h < 4 ? x : y;
        float v = h < 4 ? y : x;
        return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? 2f * v : -2f * v);
    }
    private static readonly float[] Grad3 = [1, 1, 0, -1, 1, 0, 1, -1, 0, -1, -1, 0, 1, 0, 1, -1, 0, 1,
                                             1, 0, -1, -1, 0, -1, 0, 1, 1, 0, -1, 1, 0, 1, -1, 0, -1, -1];

    public float Perlin(float x, float y)
    {
        int xi = FloorToInt(x), yi = FloorToInt(y);
        int X = xi & 255, Y = yi & 255;
        x -= xi;
        y -= yi;
        float u = Fade(x), v = Fade(y);
        int A = Perm(X) + Y, B = Perm(X + 1) + Y;
        float n = Mix(Mix(Grad(Perm(A), x, y), Grad(Perm(B), x - 1f, y), u),
                      Mix(Grad(Perm(A + 1), x, y - 1f), Grad(Perm(B + 1), x - 1f, y - 1f), u), v);
        return n * 0.5f;
    }

    public float Perlin(float x, float y, float z)
    {
        int xi = FloorToInt(x), yi = FloorToInt(y), zi = FloorToInt(z);
        int X = xi & 255, Y = yi & 255, Z = zi & 255;
        x -= xi;
        y -= yi;
        z -= zi;
        float u = Fade(x), v = Fade(y), w = Fade(z);
        int A = Perm(X) + Y, AA = Perm(A) + Z, AB = Perm(A + 1) + Z;
        int B = Perm(X + 1) + Y, BA = Perm(B) + Z, BB = Perm(B + 1) + Z;
        return Mix(Mix(Mix(Grad(Perm(AA), x, y, z), Grad(Perm(BA), x - 1f, y, z), u),
                       Mix(Grad(Perm(AB), x, y - 1f, z), Grad(Perm(BB), x - 1f, y - 1f, z), u), v),
                   Mix(Mix(Grad(Perm(AA + 1), x, y, z - 1f), Grad(Perm(BA + 1), x - 1f, y, z - 1f), u),
                       Mix(Grad(Perm(AB + 1), x, y - 1f, z - 1f), Grad(Perm(BB + 1), x - 1f, y - 1f, z - 1f), u), v), w);
    }

    public float Simplex(float xin, float yin)
    {
        const float F2 = 0.36602540378443865f, G2 = 0.21132486540518713f;
        float s = (xin + yin) * F2;
        int i = FloorToInt(xin + s), j = FloorToInt(yin + s);
        float t = (i + j) * G2;
        float x0 = xin - (i - t), y0 = yin - (j - t);
        int i1 = x0 > y0 ? 1 : 0, j1 = x0 > y0 ? 0 : 1;
        float x1 = x0 - i1 + G2, y1 = y0 - j1 + G2;
        float x2 = x0 - 1f + 2f * G2, y2 = y0 - 1f + 2f * G2;
        int ii = i & 255, jj = j & 255;
        int g0 = Perm(ii + Perm(jj)) % 12, g1 = Perm(ii + i1 + Perm(jj + j1)) % 12, g2 = Perm(ii + 1 + Perm(jj + 1)) % 12;
        return 70f * (Corner(g0, x0, y0) + Corner(g1, x1, y1) + Corner(g2, x2, y2));

        static float Corner(int g, float x, float y)
        {
            float t = 0.5f - x * x - y * y;
            if (t < 0f) return 0f;
            t *= t;
            return t * t * (Grad3[g * 3] * x + Grad3[g * 3 + 1] * y);
        }
    }

    public float Simplex(float xin, float yin, float zin)
    {
        const float F3 = 1f / 3f, G3 = 1f / 6f;
        float s = (xin + yin + zin) * F3;
        int i = FloorToInt(xin + s), j = FloorToInt(yin + s), k = FloorToInt(zin + s);
        float t = (i + j + k) * G3;
        float x0 = xin - (i - t), y0 = yin - (j - t), z0 = zin - (k - t);
        int i1, j1, k1, i2, j2, k2;
        if (x0 >= y0)
        {
            if (y0 >= z0) { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }
            else if (x0 >= z0) { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 0; k2 = 1; }
            else { i1 = 0; j1 = 0; k1 = 1; i2 = 1; j2 = 0; k2 = 1; }
        }
        else
        {
            if (y0 < z0) { i1 = 0; j1 = 0; k1 = 1; i2 = 0; j2 = 1; k2 = 1; }
            else if (x0 < z0) { i1 = 0; j1 = 1; k1 = 0; i2 = 0; j2 = 1; k2 = 1; }
            else { i1 = 0; j1 = 1; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }
        }
        float x1 = x0 - i1 + G3, y1 = y0 - j1 + G3, z1 = z0 - k1 + G3;
        float x2 = x0 - i2 + 2f * G3, y2 = y0 - j2 + 2f * G3, z2 = z0 - k2 + 2f * G3;
        float x3 = x0 - 1f + 3f * G3, y3 = y0 - 1f + 3f * G3, z3 = z0 - 1f + 3f * G3;
        int ii = i & 255, jj = j & 255, kk = k & 255;
        int g0 = Perm(ii + Perm(jj + Perm(kk))) % 12;
        int g1 = Perm(ii + i1 + Perm(jj + j1 + Perm(kk + k1))) % 12;
        int g2 = Perm(ii + i2 + Perm(jj + j2 + Perm(kk + k2))) % 12;
        int g3 = Perm(ii + 1 + Perm(jj + 1 + Perm(kk + 1))) % 12;
        return 32f * (Corner(g0, x0, y0, z0) + Corner(g1, x1, y1, z1) + Corner(g2, x2, y2, z2) + Corner(g3, x3, y3, z3));

        static float Corner(int g, float x, float y, float z)
        {
            float t = 0.6f - x * x - y * y - z * z;
            if (t < 0f) return 0f;
            t *= t;
            return t * t * (Grad3[g * 3] * x + Grad3[g * 3 + 1] * y + Grad3[g * 3 + 2] * z);
        }
    }

    public float Value(float x, float y)
    {
        int xi = FloorToInt(x), yi = FloorToInt(y);
        int X = xi & 255, Y = yi & 255;
        float u = Fade(x - xi), v = Fade(y - yi);
        float At(int dx, int dy) => Perm(Perm(X + dx) + Y + dy) * (2f / 255f) - 1f;
        return Mix(Mix(At(0, 0), At(1, 0), u), Mix(At(0, 1), At(1, 1), u), v);
    }

    public float Value(float x, float y, float z)
    {
        int xi = FloorToInt(x), yi = FloorToInt(y), zi = FloorToInt(z);
        int X = xi & 255, Y = yi & 255, Z = zi & 255;
        float u = Fade(x - xi), v = Fade(y - yi), w = Fade(z - zi);
        float At(int dx, int dy, int dz) => Perm(Perm(Perm(X + dx) + Y + dy) + Z + dz) * (2f / 255f) - 1f;
        return Mix(Mix(Mix(At(0, 0, 0), At(1, 0, 0), u), Mix(At(0, 1, 0), At(1, 1, 0), u), v),
                   Mix(Mix(At(0, 0, 1), At(1, 0, 1), u), Mix(At(0, 1, 1), At(1, 1, 1), u), v), w);
    }

    public float Cellular(float x, float y)
    {
        int xi = FloorToInt(x), yi = FloorToInt(y);
        float best = 8f;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
            {
                int h = Perm(Perm((xi + dx) & 255) + ((yi + dy) & 255));
                float fx = (xi + dx) + Perm(h) * (1f / 255f) - x;
                float fy = (yi + dy) + Perm(h + 1) * (1f / 255f) - y;
                best = KMath.Min(best, fx * fx + fy * fy);
            }
        return MathF.Sqrt(best);
    }

    public float Cellular(float x, float y, float z)
    {
        int xi = FloorToInt(x), yi = FloorToInt(y), zi = FloorToInt(z);
        float best = 8f;
        for (int dz = -1; dz <= 1; ++dz)
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                {
                    int h = Perm(Perm(Perm((xi + dx) & 255) + ((yi + dy) & 255)) + ((zi + dz) & 255));
                    float fx = (xi + dx) + Perm(h) * (1f / 255f) - x;
                    float fy = (yi + dy) + Perm(h + 1) * (1f / 255f) - y;
                    float fz = (zi + dz) + Perm(h + 2) * (1f / 255f) - z;
                    best = KMath.Min(best, fx * fx + fy * fy + fz * fz);
                }
        return MathF.Sqrt(best);
    }

    public float Perlin(Vec2 p) => Perlin(p.X, p.Y);
    public float Perlin(Vec3 p) => Perlin(p.X, p.Y, p.Z);
    public float Simplex(Vec2 p) => Simplex(p.X, p.Y);
    public float Simplex(Vec3 p) => Simplex(p.X, p.Y, p.Z);
    public float Value(Vec2 p) => Value(p.X, p.Y);
    public float Value(Vec3 p) => Value(p.X, p.Y, p.Z);
    public float Cellular(Vec2 p) => Cellular(p.X, p.Y);
    public float Cellular(Vec3 p) => Cellular(p.X, p.Y, p.Z);

    public float Sample(Kind kind, Vec2 p) => kind switch
    {
        Kind.Perlin => Perlin(p), Kind.Simplex => Simplex(p), Kind.Value => Value(p), Kind.Cellular => Cellular(p), _ => 0f,
    };
    public float Sample(Kind kind, Vec3 p) => kind switch
    {
        Kind.Perlin => Perlin(p), Kind.Simplex => Simplex(p), Kind.Value => Value(p), Kind.Cellular => Cellular(p), _ => 0f,
    };

    /// <summary>Octaves of <paramref name="kind"/> combined per <paramref name="options"/>, normalised.</summary>
    public float Fractal(Kind kind, Vec2 p, FractalOptions? options = null) => Accumulate(options ?? new(), f => Sample(kind, p * f));
    public float Fractal(Kind kind, Vec3 p, FractalOptions? options = null) => Accumulate(options ?? new(), f => Sample(kind, p * f));

    private static float Accumulate(FractalOptions options, Func<float, float> sampleAt)
    {
        float sum = 0f, norm = 0f, amplitude = 1f, frequency = 1f;
        for (int o = 0; o < Math.Max(1, options.Octaves); ++o)
        {
            float n = sampleAt(frequency);
            switch (options.Type)
            {
                case FractalType.Ridged: n = 1f - KMath.Abs(n); n *= n; break;
                case FractalType.Turbulence: n = KMath.Abs(n); break;
            }
            sum += n * amplitude;
            norm += amplitude;
            amplitude *= options.Gain;
            frequency *= options.Lacunarity;
        }
        return sum / norm;
    }
}
