namespace Koral;

/// <summary>
/// kor::Random: PCG32, seedable — the same seed gives the same sequence here, in C++, C and Kotlin. Not for
/// cryptography. Exactly reproducible across them: the integer draws, NextFloat/NextDouble/NextBool, the
/// rejection-sampled InsideUnitCircle/InsideUnitSphere, Shuffle and Pick; the rest go through MathF and may
/// differ in the last bit.
/// </summary>
/// <remarks>
/// With ImplicitUsings on, System.Random is in scope too: write <c>Koral.Random</c>, or add
/// <c>using Random = Koral.Random;</c> to a file that uses it.
/// </remarks>
public sealed class Random
{
    public const ulong DefaultSeed = 0x853c49e6748fea9bUL;
    public const ulong DefaultStream = 0xda3e39cb94b95bdbUL;
    private const ulong Multiplier = 6364136223846793005UL;

    private ulong _state, _increment;

    public Random(ulong seed = DefaultSeed, ulong stream = DefaultStream) => Seed(seed, stream);

    /// <summary>Seeded from the operating system's entropy: different every run.</summary>
    public static Random FromEntropy()
    {
        Span<ulong> words = stackalloc ulong[2];
        System.Security.Cryptography.RandomNumberGenerator.Fill(System.Runtime.InteropServices.MemoryMarshal.AsBytes(words));
        return new Random(words[0], words[1]);
    }

    [ThreadStatic] private static Random? _threadLocal;
    /// <summary>This thread's own generator, seeded from entropy the first time it is used.</summary>
    public static Random ThreadLocal => _threadLocal ??= FromEntropy();

    public void Seed(ulong seed, ulong stream = DefaultStream)
    {
        _state = 0;
        _increment = (stream << 1) | 1;
        NextU32();
        _state += seed;
        NextU32();
    }

    public uint NextU32()
    {
        ulong old = _state;
        _state = old * Multiplier + _increment;
        uint xorShifted = (uint)(((old >> 18) ^ old) >> 27);
        int rot = (int)(old >> 59);
        return (xorShifted >> rot) | (xorShifted << ((-rot) & 31));
    }
    /// <summary>Two NextU32s, the first in the high half.</summary>
    public ulong NextU64() { ulong hi = NextU32(); return (hi << 32) | NextU32(); }
    /// <summary>Uniform in [0, bound), without modulo bias.</summary>
    public uint NextU32(uint bound)
    {
        if (bound == 0) return 0;
        uint threshold = (0u - bound) % bound;
        for (;;)
        {
            uint r = NextU32();
            if (r >= threshold) return r % bound;
        }
    }
    /// <summary>Uniform in [min, maxExclusive).</summary>
    public int NextInt(int min, int maxExclusive) => maxExclusive <= min ? min : (int)(min + (long)NextU32((uint)((long)maxExclusive - min)));
    /// <summary>Uniform in [0, 1), 24 random bits.</summary>
    public float NextFloat() => (NextU32() >> 8) * (1f / 16777216f);
    public float NextFloat(float min, float max) => min + (max - min) * NextFloat();
    /// <summary>Uniform in [0, 1), 53 random bits.</summary>
    public double NextDouble() => (NextU64() >> 11) * (1.0 / 9007199254740992.0);
    public bool NextBool(float probability = 0.5f) => NextFloat() < probability;

    /// <summary>Normally distributed (Marsaglia's polar method).</summary>
    public float NextGaussian(float mean = 0f, float standardDeviation = 1f)
    {
        float u, v, s;
        do
        {
            u = NextFloat() * 2f - 1f;
            v = NextFloat() * 2f - 1f;
            s = u * u + v * v;
        } while (s >= 1f || s == 0f);
        return mean + standardDeviation * u * MathF.Sqrt(-2f * MathF.Log(s) / s);
    }

    public Vec2 InsideUnitCircle()
    {
        for (;;)
        {
            var p = new Vec2(NextFloat() * 2f - 1f, NextFloat() * 2f - 1f);
            if (KMath.Dot(p, p) <= 1f) return p;
        }
    }
    public Vec3 InsideUnitSphere()
    {
        for (;;)
        {
            var p = new Vec3(NextFloat() * 2f - 1f, NextFloat() * 2f - 1f, NextFloat() * 2f - 1f);
            if (KMath.Dot(p, p) <= 1f) return p;
        }
    }
    public Vec2 OnUnitCircle()
    {
        float angle = NextFloat() * KMath.Tau;
        return new Vec2(MathF.Cos(angle), MathF.Sin(angle));
    }
    public Vec3 OnUnitSphere()
    {
        float z = NextFloat() * 2f - 1f;
        float angle = NextFloat() * KMath.Tau;
        float r = MathF.Sqrt(KMath.Max(0f, 1f - z * z));
        return new Vec3(r * MathF.Cos(angle), r * MathF.Sin(angle), z);
    }
    /// <summary>A uniformly distributed rotation (Shoemake).</summary>
    public Quat Rotation()
    {
        float u1 = NextFloat(), u2 = NextFloat() * KMath.Tau, u3 = NextFloat() * KMath.Tau;
        float a = MathF.Sqrt(1f - u1), b = MathF.Sqrt(u1);
        return new Quat(a * MathF.Sin(u2), a * MathF.Cos(u2), b * MathF.Sin(u3), b * MathF.Cos(u3));
    }
    public Vec3 InsideBox(Vec3 min, Vec3 max) => new(NextFloat(min.X, max.X), NextFloat(min.Y, max.Y), NextFloat(min.Z, max.Z));

    /// <summary>Fisher–Yates, in place.</summary>
    public void Shuffle<T>(Span<T> items)
    {
        for (int i = items.Length; i > 1; --i)
        {
            int j = (int)NextU32((uint)i);
            (items[i - 1], items[j]) = (items[j], items[i - 1]);
        }
    }
    public T Pick<T>(ReadOnlySpan<T> items) => items[(int)NextU32((uint)items.Length)];

    /// <summary>Skips <paramref name="count"/> outputs in O(log count).</summary>
    public void Advance(ulong count)
    {
        ulong accMult = 1, accPlus = 0, curMult = Multiplier, curPlus = _increment;
        while (count > 0)
        {
            if ((count & 1) != 0) { accMult *= curMult; accPlus = accPlus * curMult + curPlus; }
            curPlus = (curMult + 1) * curPlus;
            curMult *= curMult;
            count >>= 1;
        }
        _state = accMult * _state + accPlus;
    }

    /// <summary>The full state, to save and restore a generator mid-sequence.</summary>
    public (ulong State, ulong Increment) State
    {
        get => (_state, _increment);
        set { _state = value.State; _increment = value.Increment | 1; }
    }
}

public static partial class KMath
{
    /// <summary>A well-mixed 32-bit hash of a 32-bit value.</summary>
    public static uint Hash(uint v)
    {
        uint state = v * 747796405u + 2891336453u;
        uint word = ((state >> (int)((state >> 28) + 4u)) ^ state) * 277803737u;
        return (word >> 22) ^ word;
    }
    public static uint HashCombine(uint seed, uint v) => Hash(seed ^ (v + 0x9e3779b9u + (seed << 6) + (seed >> 2)));
    /// <summary>A uniform float in [0, 1) that depends only on <paramref name="v"/>.</summary>
    public static float HashToFloat(uint v) => (Hash(v) >> 8) * (1f / 16777216f);
}
