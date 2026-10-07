#pragma once

// kor::Random: a small, fast, seedable generator (PCG32, O'Neill 2014) — the same seed gives the same
// sequence on every platform and in every binding (C, C#, Kotlin), so it can drive procedural content,
// replays and lockstep simulations. Not for cryptography.
//
// Exactly reproducible everywhere: NextU32/NextU64/NextU32(bound)/NextInt/NextFloat/NextDouble/NextBool,
// the rejection-sampled InsideUnitCircle/InsideUnitSphere, Shuffle and Pick. The rest (NextGaussian,
// OnUnitSphere, Rotation, ...) go through sqrt/log/sin, which language runtimes may round differently in
// the last bit.

#include "quaternion.h"

#include <span>
#include <utility>

#include "api.h"

namespace kor {
    class KORAL_API Random {
    public:
        static constexpr u64 DefaultSeed = 0x853c49e6748fea9bull;
        static constexpr u64 DefaultStream = 0xda3e39cb94b95bdbull;

        /// Seeded with `seed`; generators with different `stream`s give independent sequences for the same seed.
        constexpr explicit Random(u64 seed = DefaultSeed, u64 stream = DefaultStream) { Seed(seed, stream); }

        /// A generator seeded from the operating system's entropy: different every run.
        static Random FromEntropy();
        /// This thread's own generator, seeded from entropy the first time it is used.
        static Random& ThreadLocal();

        constexpr void Seed(u64 seed, u64 stream = DefaultStream) {
            _state = 0;
            _increment = (stream << 1u) | 1u;
            NextU32();
            _state += seed;
            NextU32();
        }

        constexpr u32 NextU32() {
            const u64 old = _state;
            _state = old * 6364136223846793005ull + _increment;
            const u32 xorShifted = u32(((old >> 18u) ^ old) >> 27u);
            const u32 rot = u32(old >> 59u);
            return (xorShifted >> rot) | (xorShifted << ((0u - rot) & 31u));
        }
        /// Two NextU32s, the first in the high half.
        constexpr u64 NextU64() {
            const u64 hi = NextU32();
            return (hi << 32u) | NextU32();
        }
        /// Uniform in [0, bound), without modulo bias. 0 for a bound of 0.
        constexpr u32 NextU32(u32 bound) {
            if (bound == 0) return 0;
            const u32 threshold = (0u - bound) % bound;
            for (;;)
                if (const u32 r = NextU32(); r >= threshold) return r % bound;
        }
        /// Uniform in [min, maxExclusive). `min` when the range is empty.
        constexpr i32 NextInt(i32 min, i32 maxExclusive) {
            if (maxExclusive <= min) return min;
            return i32(i64(min) + i64(NextU32(u32(i64(maxExclusive) - i64(min)))));
        }
        /// Uniform in [0, 1), 24 random bits.
        constexpr float NextFloat() { return float(NextU32() >> 8) * 0x1p-24f; }
        /// Uniform in [min, max).
        constexpr float NextFloat(float min, float max) { return min + (max - min) * NextFloat(); }
        /// Uniform in [0, 1), 53 random bits.
        constexpr double NextDouble() { return double(NextU64() >> 11) * 0x1p-53; }
        /// True with the given probability.
        constexpr bool NextBool(float probability = 0.5f) { return NextFloat() < probability; }

        /// Normally distributed (Marsaglia's polar method).
        float NextGaussian(float mean = 0.f, float standardDeviation = 1.f);

        /// Uniform inside the unit disc.
        constexpr Vec2 InsideUnitCircle() {
            for (;;) {
                const Vec2 p{NextFloat() * 2.f - 1.f, NextFloat() * 2.f - 1.f};
                if (Dot(p, p) <= 1.f) return p;
            }
        }
        /// Uniform inside the unit ball.
        constexpr Vec3 InsideUnitSphere() {
            for (;;) {
                const Vec3 p{NextFloat() * 2.f - 1.f, NextFloat() * 2.f - 1.f, NextFloat() * 2.f - 1.f};
                if (Dot(p, p) <= 1.f) return p;
            }
        }
        /// Uniform on the unit circle.
        Vec2 OnUnitCircle();
        /// Uniform on the unit sphere.
        Vec3 OnUnitSphere();
        /// A uniformly distributed rotation (Shoemake).
        Quat Rotation();
        /// A uniform point in the box.
        constexpr Vec3 InsideBox(const Vec3& min, const Vec3& max) { return {NextFloat(min.x, max.x), NextFloat(min.y, max.y), NextFloat(min.z, max.z)}; }

        /// Fisher–Yates, in place.
        template<class T>
        constexpr void Shuffle(std::span<T> items) {
            for (std::size_t i = items.size(); i > 1; --i) {
                const std::size_t j = NextU32(u32(i));
                std::swap(items[i - 1], items[j]);
            }
        }
        /// A uniformly chosen element (the span must not be empty).
        template<class T>
        constexpr T& Pick(std::span<T> items) { return items[NextU32(u32(items.size()))]; }

        /// Skips `count` outputs in O(log count) (jump-ahead): splits one sequence among workers without overlap.
        constexpr void Advance(u64 count) {
            u64 accMult = 1, accPlus = 0, curMult = 6364136223846793005ull, curPlus = _increment;
            while (count > 0) {
                if (count & 1u) { accMult *= curMult; accPlus = accPlus * curMult + curPlus; }
                curPlus = (curMult + 1) * curPlus;
                curMult *= curMult;
                count >>= 1u;
            }
            _state = accMult * _state + accPlus;
        }

        /// The full state, to save and restore a generator mid-sequence.
        constexpr std::pair<u64, u64> State() const { return {_state, _increment}; }
        constexpr void SetState(u64 state, u64 increment) { _state = state; _increment = increment | 1u; }

    private:
        u64 _state = 0, _increment = 0;
    };

    // ---- stateless hashing, for when a value must depend only on its inputs ------------------------------

    /// A well-mixed 32-bit hash of a 32-bit value (PCG's output permutation on one LCG step).
    constexpr u32 Hash(u32 v) {
        const u32 state = v * 747796405u + 2891336453u;
        const u32 word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
        return (word >> 22u) ^ word;
    }
    /// Mixes `v` into `seed` (order matters): Hash(Combine(Combine(s, x), y)).
    constexpr u32 HashCombine(u32 seed, u32 v) { return Hash(seed ^ (v + 0x9e3779b9u + (seed << 6u) + (seed >> 2u))); }
    /// A uniform float in [0, 1) that depends only on the inputs.
    constexpr float HashToFloat(u32 v) { return float(Hash(v) >> 8) * 0x1p-24f; }
}
