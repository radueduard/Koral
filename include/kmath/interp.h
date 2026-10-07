#pragma once

// Getting from one value to another: easing curves for animation timing, and splines for paths.
// (Lerp, InverseLerp, Remap, SmoothStep, MoveTowards and SmoothDamp live with the scalars and vectors;
// Slerp and Nlerp with the quaternions.)

#include "vector.h"

#include <span>

#include "api.h"

namespace kor {
    /// Robert Penner's easing curves. Each maps t in [0, 1] to progress, 0 at 0 and 1 at 1 (Back and
    /// Elastic overshoot in between). In: slow start; Out: slow end; InOut: both.
    enum class Easing : u8 {
        eLinear,
        eInSine, eOutSine, eInOutSine,
        eInQuad, eOutQuad, eInOutQuad,
        eInCubic, eOutCubic, eInOutCubic,
        eInQuart, eOutQuart, eInOutQuart,
        eInQuint, eOutQuint, eInOutQuint,
        eInExpo, eOutExpo, eInOutExpo,
        eInCirc, eOutCirc, eInOutCirc,
        eInBack, eOutBack, eInOutBack,
        eInElastic, eOutElastic, eInOutElastic,
        eInBounce, eOutBounce, eInOutBounce,
    };

    /// The eased progress at `t` (clamped to [0, 1]).
    KORAL_API float Ease(Easing easing, float t);
    /// Lerp(a, b, Ease(easing, t)).
    template<class T> T Ease(Easing easing, const T& a, const T& b, float t) { return Lerp(a, b, Ease(easing, t)); }

    // ---- curves through control points ------------------------------------------------------------------
    // Templated over the point type: float, Vec2, Vec3, Vec4 all work.

    /// Quadratic Bézier: starts at p0, ends at p2, pulled toward p1.
    template<class P> constexpr P Bezier(const P& p0, const P& p1, const P& p2, float t) {
        const float u = 1.f - t;
        return p0 * (u * u) + p1 * (2.f * u * t) + p2 * (t * t);
    }
    /// Cubic Bézier: starts at p0 heading toward p1, ends at p3 arriving from p2.
    template<class P> constexpr P Bezier(const P& p0, const P& p1, const P& p2, const P& p3, float t) {
        const float u = 1.f - t;
        return p0 * (u * u * u) + p1 * (3.f * u * u * t) + p2 * (3.f * u * t * t) + p3 * (t * t * t);
    }
    /// The cubic Bézier's derivative (its direction of travel, times its speed).
    template<class P> constexpr P BezierTangent(const P& p0, const P& p1, const P& p2, const P& p3, float t) {
        const float u = 1.f - t;
        return (p1 - p0) * (3.f * u * u) + (p2 - p1) * (6.f * u * t) + (p3 - p2) * (3.f * t * t);
    }
    /// Cubic Hermite: from p0 with tangent m0 to p1 with tangent m1.
    template<class P> constexpr P Hermite(const P& p0, const P& m0, const P& p1, const P& m1, float t) {
        const float t2 = t * t, t3 = t2 * t;
        return p0 * (2.f * t3 - 3.f * t2 + 1.f) + m0 * (t3 - 2.f * t2 + t) + p1 * (-2.f * t3 + 3.f * t2) + m1 * (t3 - t2);
    }
    /// Uniform Catmull–Rom: passes through p1 (t = 0) and p2 (t = 1), shaped by its neighbours p0 and p3.
    template<class P> constexpr P CatmullRom(const P& p0, const P& p1, const P& p2, const P& p3, float t) {
        const float t2 = t * t, t3 = t2 * t;
        return (p1 * 2.f + (p2 - p0) * t + (p0 * 2.f - p1 * 5.f + p2 * 4.f - p3) * t2 + (p1 * 3.f - p0 - p2 * 3.f + p3) * t3) * 0.5f;
    }

    /**
     * A point on the Catmull–Rom path through all `points`: t = 0 at the first, t = points.size() - 1 at the
     * last (or back at the first, after points.size(), when `closed`). The ends repeat their point as the
     * missing neighbour. Empty points give a default P.
     */
    template<class P> P SamplePath(std::span<const P> points, float t, bool closed = false) {
        const int n = int(points.size());
        if (n == 0) return P{};
        if (n == 1) return points[0];
        const int segments = closed ? n : n - 1;
        t = closed ? Mod(t, float(segments)) : Clamp(t, 0.f, float(segments));
        const int i = Min(int(t), segments - 1);
        auto at = [&](int k) { return closed ? points[Mod(k, n)] : points[Clamp(k, 0, n - 1)]; };
        return CatmullRom(at(i - 1), at(i), at(i + 1), at(i + 2), t - float(i));
    }
}
