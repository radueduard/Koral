#pragma once

// kor::Noise: coherent noise for terrain, clouds, textures and wobble. A Noise is a seed turned into a
// permutation table; the same seed gives the same field on every platform and binding (only additions,
// multiplications and Floor are involved, compiled without fused multiply-adds, so results agree bit for bit).
//
// Ranges: Perlin, Simplex and Value return roughly [-1, 1]; Cellular returns the distance to the nearest
// feature point, 0 at the point and about 1 at most. A feature size is one unit: scale the input to change it.

#include "vector.h"

#include <array>

#include "api.h"

namespace kor {
    /// How the octaves of a fractal are combined.
    enum class FractalType : u8 {
        eFbm,         ///< Fractional Brownian motion: octaves summed. Rolling hills, clouds.
        eRidged,      ///< 1 - |noise|, squared and weighted by the previous octave: sharp ridges, mountains.
        eTurbulence,  ///< |noise| summed: billowy, fire, marble veins.
    };

    struct FractalOptions {
        FractalType type = FractalType::eFbm;
        int octaves = 5;
        /// Frequency multiplier between octaves.
        float lacunarity = 2.f;
        /// Amplitude multiplier between octaves (also called persistence).
        float gain = 0.5f;
    };

    class KORAL_API Noise {
    public:
        enum class Kind : u8 { ePerlin, eSimplex, eValue, eCellular };

        explicit Noise(u32 seed = 0);
        [[nodiscard]] u32 Seed() const { return _seed; }

        /// Ken Perlin's improved gradient noise (2002).
        [[nodiscard]] float Perlin(float x, float y) const;
        [[nodiscard]] float Perlin(float x, float y, float z) const;
        /// Simplex noise (Gustavson's formulation): fewer directional artefacts than Perlin, cheaper in 3D.
        [[nodiscard]] float Simplex(float x, float y) const;
        [[nodiscard]] float Simplex(float x, float y, float z) const;
        /// Random values at the lattice points, smoothly interpolated: blockier than gradient noise.
        [[nodiscard]] float Value(float x, float y) const;
        [[nodiscard]] float Value(float x, float y, float z) const;
        /// Worley noise: the distance to the nearest of one jittered feature point per cell.
        [[nodiscard]] float Cellular(float x, float y) const;
        [[nodiscard]] float Cellular(float x, float y, float z) const;

        [[nodiscard]] float Perlin(const Vec2& p) const { return Perlin(p.x, p.y); }
        [[nodiscard]] float Perlin(const Vec3& p) const { return Perlin(p.x, p.y, p.z); }
        [[nodiscard]] float Simplex(const Vec2& p) const { return Simplex(p.x, p.y); }
        [[nodiscard]] float Simplex(const Vec3& p) const { return Simplex(p.x, p.y, p.z); }
        [[nodiscard]] float Value(const Vec2& p) const { return Value(p.x, p.y); }
        [[nodiscard]] float Value(const Vec3& p) const { return Value(p.x, p.y, p.z); }
        [[nodiscard]] float Cellular(const Vec2& p) const { return Cellular(p.x, p.y); }
        [[nodiscard]] float Cellular(const Vec3& p) const { return Cellular(p.x, p.y, p.z); }

        /// One of the above by kind.
        [[nodiscard]] float Sample(Kind kind, const Vec2& p) const;
        [[nodiscard]] float Sample(Kind kind, const Vec3& p) const;

        /// Octaves of `kind` combined per `options`, normalised back to the single octave's range
        /// (fBm: about [-1, 1]; ridged and turbulence: [0, 1]).
        [[nodiscard]] float Fractal(Kind kind, const Vec2& p, const FractalOptions& options = {}) const;
        [[nodiscard]] float Fractal(Kind kind, const Vec3& p, const FractalOptions& options = {}) const;

    private:
        [[nodiscard]] int Perm(int i) const { return _perm[i & 511]; }

        u32 _seed;
        std::array<u8, 512> _perm{};
    };
}
