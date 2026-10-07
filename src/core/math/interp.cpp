#include "kmath/interp.h"

namespace kor {
    namespace {
        float OutBounce(float t) {
            constexpr float n = 7.5625f, d = 2.75f;
            if (t < 1.f / d) return n * t * t;
            if (t < 2.f / d) { t -= 1.5f / d; return n * t * t + 0.75f; }
            if (t < 2.5f / d) { t -= 2.25f / d; return n * t * t + 0.9375f; }
            t -= 2.625f / d;
            return n * t * t + 0.984375f;
        }
    }

    float Ease(Easing easing, float t) {
        t = Saturate(t);
        constexpr float c1 = 1.70158f, c2 = c1 * 1.525f, c3 = c1 + 1.f;
        constexpr float c4 = Tau<float> / 3.f, c5 = Tau<float> / 4.5f;
        constexpr float pi = Pi<float>;
        switch (easing) {
            case Easing::eLinear: return t;
            case Easing::eInSine: return 1.f - std::cos(t * pi * 0.5f);
            case Easing::eOutSine: return std::sin(t * pi * 0.5f);
            case Easing::eInOutSine: return -(std::cos(pi * t) - 1.f) * 0.5f;
            case Easing::eInQuad: return t * t;
            case Easing::eOutQuad: return 1.f - (1.f - t) * (1.f - t);
            case Easing::eInOutQuad: return t < 0.5f ? 2.f * t * t : 1.f - std::pow(-2.f * t + 2.f, 2.f) * 0.5f;
            case Easing::eInCubic: return t * t * t;
            case Easing::eOutCubic: return 1.f - std::pow(1.f - t, 3.f);
            case Easing::eInOutCubic: return t < 0.5f ? 4.f * t * t * t : 1.f - std::pow(-2.f * t + 2.f, 3.f) * 0.5f;
            case Easing::eInQuart: return t * t * t * t;
            case Easing::eOutQuart: return 1.f - std::pow(1.f - t, 4.f);
            case Easing::eInOutQuart: return t < 0.5f ? 8.f * t * t * t * t : 1.f - std::pow(-2.f * t + 2.f, 4.f) * 0.5f;
            case Easing::eInQuint: return t * t * t * t * t;
            case Easing::eOutQuint: return 1.f - std::pow(1.f - t, 5.f);
            case Easing::eInOutQuint: return t < 0.5f ? 16.f * t * t * t * t * t : 1.f - std::pow(-2.f * t + 2.f, 5.f) * 0.5f;
            case Easing::eInExpo: return t == 0.f ? 0.f : std::pow(2.f, 10.f * t - 10.f);
            case Easing::eOutExpo: return t == 1.f ? 1.f : 1.f - std::pow(2.f, -10.f * t);
            case Easing::eInOutExpo:
                if (t == 0.f || t == 1.f) return t;
                return t < 0.5f ? std::pow(2.f, 20.f * t - 10.f) * 0.5f : (2.f - std::pow(2.f, -20.f * t + 10.f)) * 0.5f;
            case Easing::eInCirc: return 1.f - std::sqrt(1.f - t * t);
            case Easing::eOutCirc: return std::sqrt(1.f - (t - 1.f) * (t - 1.f));
            case Easing::eInOutCirc:
                return t < 0.5f ? (1.f - std::sqrt(1.f - 4.f * t * t)) * 0.5f
                                : (std::sqrt(1.f - std::pow(-2.f * t + 2.f, 2.f)) + 1.f) * 0.5f;
            case Easing::eInBack: return c3 * t * t * t - c1 * t * t;
            case Easing::eOutBack: return 1.f + c3 * std::pow(t - 1.f, 3.f) + c1 * std::pow(t - 1.f, 2.f);
            case Easing::eInOutBack:
                return t < 0.5f ? (std::pow(2.f * t, 2.f) * ((c2 + 1.f) * 2.f * t - c2)) * 0.5f
                                : (std::pow(2.f * t - 2.f, 2.f) * ((c2 + 1.f) * (t * 2.f - 2.f) + c2) + 2.f) * 0.5f;
            case Easing::eInElastic:
                if (t == 0.f || t == 1.f) return t;
                return -std::pow(2.f, 10.f * t - 10.f) * std::sin((t * 10.f - 10.75f) * c4);
            case Easing::eOutElastic:
                if (t == 0.f || t == 1.f) return t;
                return std::pow(2.f, -10.f * t) * std::sin((t * 10.f - 0.75f) * c4) + 1.f;
            case Easing::eInOutElastic:
                if (t == 0.f || t == 1.f) return t;
                return t < 0.5f ? -(std::pow(2.f, 20.f * t - 10.f) * std::sin((20.f * t - 11.125f) * c5)) * 0.5f
                                : (std::pow(2.f, -20.f * t + 10.f) * std::sin((20.f * t - 11.125f) * c5)) * 0.5f + 1.f;
            case Easing::eInBounce: return 1.f - OutBounce(1.f - t);
            case Easing::eOutBounce: return OutBounce(t);
            case Easing::eInOutBounce: return t < 0.5f ? (1.f - OutBounce(1.f - 2.f * t)) * 0.5f : (1.f + OutBounce(2.f * t - 1.f)) * 0.5f;
        }
        return t;
    }
}
