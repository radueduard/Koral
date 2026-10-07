#include "kmath/random.h"

#include <random>

namespace kor {
    Random Random::FromEntropy() {
        std::random_device device;
        const u64 seed = (u64(device()) << 32u) | device();
        const u64 stream = (u64(device()) << 32u) | device();
        return Random(seed, stream);
    }

    Random& Random::ThreadLocal() {
        thread_local Random random = FromEntropy();
        return random;
    }

    float Random::NextGaussian(float mean, float standardDeviation) {
        float u, v, s;
        do {
            u = NextFloat() * 2.f - 1.f;
            v = NextFloat() * 2.f - 1.f;
            s = u * u + v * v;
        } while (s >= 1.f || s == 0.f);
        return mean + standardDeviation * u * std::sqrt(-2.f * std::log(s) / s);
    }

    Vec2 Random::OnUnitCircle() {
        const float angle = NextFloat() * Tau<float>;
        return {std::cos(angle), std::sin(angle)};
    }

    Vec3 Random::OnUnitSphere() {
        const float z = NextFloat() * 2.f - 1.f;
        const float angle = NextFloat() * Tau<float>;
        const float r = std::sqrt(Max(0.f, 1.f - z * z));
        return {r * std::cos(angle), r * std::sin(angle), z};
    }

    Quat Random::Rotation() {
        const float u1 = NextFloat(), u2 = NextFloat() * Tau<float>, u3 = NextFloat() * Tau<float>;
        const float a = std::sqrt(1.f - u1), b = std::sqrt(u1);
        return {a * std::sin(u2), a * std::cos(u2), b * std::sin(u3), b * std::cos(u3)};
    }
}
