// Compiled with -ffp-contract=off (see CMakeLists.txt): the C# and Kotlin ports of this file reproduce it
// bit for bit, which a fused multiply-add here — and not there — would break. Keep every expression's
// order of operations identical to theirs when changing anything.

#include "kmath/noise.h"
#include "kmath/random.h"

namespace kor {
    namespace {
        float Fade(float t) { return t * t * t * (t * (t * 6.f - 15.f) + 10.f); }
        float Mix(float a, float b, float t) { return a + t * (b - a); }
        int FloorToInt(float v) { return int(std::floor(v)); }

        float Grad(int hash, float x, float y, float z) {
            const int h = hash & 15;
            const float u = h < 8 ? x : y;
            const float v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
            return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
        }
        float Grad(int hash, float x, float y) {
            const int h = hash & 7;
            const float u = h < 4 ? x : y;
            const float v = h < 4 ? y : x;
            return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? 2.f * v : -2.f * v);
        }

        constexpr float Grad3[12][3] = {{1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 0, 1}, {-1, 0, 1},
                                        {1, 0, -1}, {-1, 0, -1}, {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1}};
    }

    Noise::Noise(u32 seed) : _seed(seed) {
        std::array<u8, 256> p{};
        for (int i = 0; i < 256; ++i) p[i] = u8(i);
        Random random(seed);
        random.Shuffle(std::span<u8>(p));
        for (int i = 0; i < 512; ++i) _perm[i] = p[i & 255];
    }

    float Noise::Perlin(float x, float y) const {
        const int xi = FloorToInt(x), yi = FloorToInt(y);
        const int X = xi & 255, Y = yi & 255;
        x -= float(xi);
        y -= float(yi);
        const float u = Fade(x), v = Fade(y);
        const int A = Perm(X) + Y, B = Perm(X + 1) + Y;
        const float n = Mix(Mix(Grad(Perm(A), x, y), Grad(Perm(B), x - 1.f, y), u),
                            Mix(Grad(Perm(A + 1), x, y - 1.f), Grad(Perm(B + 1), x - 1.f, y - 1.f), u), v);
        return n * 0.5f;
    }

    float Noise::Perlin(float x, float y, float z) const {
        const int xi = FloorToInt(x), yi = FloorToInt(y), zi = FloorToInt(z);
        const int X = xi & 255, Y = yi & 255, Z = zi & 255;
        x -= float(xi);
        y -= float(yi);
        z -= float(zi);
        const float u = Fade(x), v = Fade(y), w = Fade(z);
        const int A = Perm(X) + Y, AA = Perm(A) + Z, AB = Perm(A + 1) + Z;
        const int B = Perm(X + 1) + Y, BA = Perm(B) + Z, BB = Perm(B + 1) + Z;
        return Mix(Mix(Mix(Grad(Perm(AA), x, y, z), Grad(Perm(BA), x - 1.f, y, z), u),
                       Mix(Grad(Perm(AB), x, y - 1.f, z), Grad(Perm(BB), x - 1.f, y - 1.f, z), u), v),
                   Mix(Mix(Grad(Perm(AA + 1), x, y, z - 1.f), Grad(Perm(BA + 1), x - 1.f, y, z - 1.f), u),
                       Mix(Grad(Perm(AB + 1), x, y - 1.f, z - 1.f), Grad(Perm(BB + 1), x - 1.f, y - 1.f, z - 1.f), u), v), w);
    }

    float Noise::Simplex(float xin, float yin) const {
        constexpr float F2 = 0.36602540378443865f;   // (sqrt(3) - 1) / 2
        constexpr float G2 = 0.21132486540518713f;   // (3 - sqrt(3)) / 6
        const float s = (xin + yin) * F2;
        const int i = FloorToInt(xin + s), j = FloorToInt(yin + s);
        const float t = float(i + j) * G2;
        const float x0 = xin - (float(i) - t), y0 = yin - (float(j) - t);
        const int i1 = x0 > y0 ? 1 : 0, j1 = x0 > y0 ? 0 : 1;
        const float x1 = x0 - float(i1) + G2, y1 = y0 - float(j1) + G2;
        const float x2 = x0 - 1.f + 2.f * G2, y2 = y0 - 1.f + 2.f * G2;
        const int ii = i & 255, jj = j & 255;
        const int g0 = Perm(ii + Perm(jj)) % 12, g1 = Perm(ii + i1 + Perm(jj + j1)) % 12, g2 = Perm(ii + 1 + Perm(jj + 1)) % 12;

        auto corner = [](int g, float x, float y) {
            float t = 0.5f - x * x - y * y;
            if (t < 0.f) return 0.f;
            t *= t;
            return t * t * (Grad3[g][0] * x + Grad3[g][1] * y);
        };
        return 70.f * (corner(g0, x0, y0) + corner(g1, x1, y1) + corner(g2, x2, y2));
    }

    float Noise::Simplex(float xin, float yin, float zin) const {
        constexpr float F3 = 1.f / 3.f, G3 = 1.f / 6.f;
        const float s = (xin + yin + zin) * F3;
        const int i = FloorToInt(xin + s), j = FloorToInt(yin + s), k = FloorToInt(zin + s);
        const float t = float(i + j + k) * G3;
        const float x0 = xin - (float(i) - t), y0 = yin - (float(j) - t), z0 = zin - (float(k) - t);
        int i1, j1, k1, i2, j2, k2;
        if (x0 >= y0) {
            if (y0 >= z0) { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }
            else if (x0 >= z0) { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 0; k2 = 1; }
            else { i1 = 0; j1 = 0; k1 = 1; i2 = 1; j2 = 0; k2 = 1; }
        } else {
            if (y0 < z0) { i1 = 0; j1 = 0; k1 = 1; i2 = 0; j2 = 1; k2 = 1; }
            else if (x0 < z0) { i1 = 0; j1 = 1; k1 = 0; i2 = 0; j2 = 1; k2 = 1; }
            else { i1 = 0; j1 = 1; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }
        }
        const float x1 = x0 - float(i1) + G3, y1 = y0 - float(j1) + G3, z1 = z0 - float(k1) + G3;
        const float x2 = x0 - float(i2) + 2.f * G3, y2 = y0 - float(j2) + 2.f * G3, z2 = z0 - float(k2) + 2.f * G3;
        const float x3 = x0 - 1.f + 3.f * G3, y3 = y0 - 1.f + 3.f * G3, z3 = z0 - 1.f + 3.f * G3;
        const int ii = i & 255, jj = j & 255, kk = k & 255;
        const int g0 = Perm(ii + Perm(jj + Perm(kk))) % 12;
        const int g1 = Perm(ii + i1 + Perm(jj + j1 + Perm(kk + k1))) % 12;
        const int g2 = Perm(ii + i2 + Perm(jj + j2 + Perm(kk + k2))) % 12;
        const int g3 = Perm(ii + 1 + Perm(jj + 1 + Perm(kk + 1))) % 12;

        auto corner = [](int g, float x, float y, float z) {
            float t = 0.6f - x * x - y * y - z * z;
            if (t < 0.f) return 0.f;
            t *= t;
            return t * t * (Grad3[g][0] * x + Grad3[g][1] * y + Grad3[g][2] * z);
        };
        return 32.f * (corner(g0, x0, y0, z0) + corner(g1, x1, y1, z1) + corner(g2, x2, y2, z2) + corner(g3, x3, y3, z3));
    }

    float Noise::Value(float x, float y) const {
        const int xi = FloorToInt(x), yi = FloorToInt(y);
        const int X = xi & 255, Y = yi & 255;
        const float u = Fade(x - float(xi)), v = Fade(y - float(yi));
        auto at = [&](int dx, int dy) { return float(Perm(Perm(X + dx) + Y + dy)) * (2.f / 255.f) - 1.f; };
        return Mix(Mix(at(0, 0), at(1, 0), u), Mix(at(0, 1), at(1, 1), u), v);
    }

    float Noise::Value(float x, float y, float z) const {
        const int xi = FloorToInt(x), yi = FloorToInt(y), zi = FloorToInt(z);
        const int X = xi & 255, Y = yi & 255, Z = zi & 255;
        const float u = Fade(x - float(xi)), v = Fade(y - float(yi)), w = Fade(z - float(zi));
        auto at = [&](int dx, int dy, int dz) { return float(Perm(Perm(Perm(X + dx) + Y + dy) + Z + dz)) * (2.f / 255.f) - 1.f; };
        return Mix(Mix(Mix(at(0, 0, 0), at(1, 0, 0), u), Mix(at(0, 1, 0), at(1, 1, 0), u), v),
                   Mix(Mix(at(0, 0, 1), at(1, 0, 1), u), Mix(at(0, 1, 1), at(1, 1, 1), u), v), w);
    }

    float Noise::Cellular(float x, float y) const {
        const int xi = FloorToInt(x), yi = FloorToInt(y);
        float best = 8.f;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const int h = Perm(Perm((xi + dx) & 255) + ((yi + dy) & 255));
                const float fx = float(xi + dx) + float(Perm(h)) * (1.f / 255.f) - x;
                const float fy = float(yi + dy) + float(Perm(h + 1)) * (1.f / 255.f) - y;
                best = Min(best, fx * fx + fy * fy);
            }
        return std::sqrt(best);
    }

    float Noise::Cellular(float x, float y, float z) const {
        const int xi = FloorToInt(x), yi = FloorToInt(y), zi = FloorToInt(z);
        float best = 8.f;
        for (int dz = -1; dz <= 1; ++dz)
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int h = Perm(Perm(Perm((xi + dx) & 255) + ((yi + dy) & 255)) + ((zi + dz) & 255));
                    const float fx = float(xi + dx) + float(Perm(h)) * (1.f / 255.f) - x;
                    const float fy = float(yi + dy) + float(Perm(h + 1)) * (1.f / 255.f) - y;
                    const float fz = float(zi + dz) + float(Perm(h + 2)) * (1.f / 255.f) - z;
                    best = Min(best, fx * fx + fy * fy + fz * fz);
                }
        return std::sqrt(best);
    }

    float Noise::Sample(Kind kind, const Vec2& p) const {
        switch (kind) {
            case Kind::ePerlin: return Perlin(p);
            case Kind::eSimplex: return Simplex(p);
            case Kind::eValue: return Value(p);
            case Kind::eCellular: return Cellular(p);
        }
        return 0.f;
    }

    float Noise::Sample(Kind kind, const Vec3& p) const {
        switch (kind) {
            case Kind::ePerlin: return Perlin(p);
            case Kind::eSimplex: return Simplex(p);
            case Kind::eValue: return Value(p);
            case Kind::eCellular: return Cellular(p);
        }
        return 0.f;
    }

    namespace {
        template<class V, class SampleFn>
        float Accumulate(const V& p, const FractalOptions& options, SampleFn&& sample) {
            float sum = 0.f, norm = 0.f, amplitude = 1.f, frequency = 1.f;
            for (int o = 0; o < Max(1, options.octaves); ++o) {
                float n = sample(p * frequency);
                switch (options.type) {
                    case FractalType::eFbm: break;
                    case FractalType::eRidged: n = 1.f - Abs(n); n = n * n; break;
                    case FractalType::eTurbulence: n = Abs(n); break;
                }
                sum += n * amplitude;
                norm += amplitude;
                amplitude *= options.gain;
                frequency *= options.lacunarity;
            }
            return sum / norm;
        }
    }

    float Noise::Fractal(Kind kind, const Vec2& p, const FractalOptions& options) const {
        return Accumulate(p, options, [&](const Vec2& q) { return Sample(kind, q); });
    }

    float Noise::Fractal(Kind kind, const Vec3& p, const FractalOptions& options) const {
        return Accumulate(p, options, [&](const Vec3& q) { return Sample(kind, q); });
    }
}
