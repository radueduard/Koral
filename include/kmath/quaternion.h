#pragma once

// kor::Quat: a rotation, stored x, y, z, w (the order of its memory and of its constructor, as in Slang,
// HLSL and System.Numerics; glm's constructor took w first). `a * b` rotates by b, then by a, like the
// matrices they stand for, and `q * v` rotates a vector. Euler angles are radians, (pitch, yaw, roll)
// about (x, y, z), with the same composition glm's quat(vec3) and eulerAngles used.

#include "matrix.h"

namespace kor {
    template<std::floating_point T>
    struct QuatT {
        using value_type = T;
        T x{}, y{}, z{}, w{1};

        /// The identity: no rotation.
        constexpr QuatT() = default;
        constexpr QuatT(T x, T y, T z, T w) : x(x), y(y), z(z), w(w) {}
        constexpr QuatT(const Vec<T, 3>& xyz, T w) : x(xyz.x), y(xyz.y), z(xyz.z), w(w) {}
        template<std::floating_point U>
        constexpr explicit QuatT(const QuatT<U>& q) : x(T(q.x)), y(T(q.y)), z(T(q.z)), w(T(q.w)) {}

        constexpr Vec<T, 3> XYZ() const { return {x, y, z}; }
        constexpr T& operator[](int i) { return i == 0 ? x : i == 1 ? y : i == 2 ? z : w; }
        constexpr const T& operator[](int i) const { return i == 0 ? x : i == 1 ? y : i == 2 ? z : w; }
        constexpr T* data() { return &x; }
        constexpr const T* data() const { return &x; }

        static constexpr QuatT Identity() { return {}; }

        /// `angle` radians counter-clockwise about `axis` (which need not be normalised).
        static QuatT AngleAxis(T angle, const Vec<T, 3>& axis) {
            const T s = std::sin(angle * T(0.5));
            return QuatT(Normalize(axis) * s, std::cos(angle * T(0.5)));
        }
        /// From Euler angles (pitch, yaw, roll) in radians, as glm::quat(vec3) built them.
        static QuatT FromEuler(const Vec<T, 3>& euler) {
            const Vec<T, 3> c = Cos(euler * T(0.5)), s = Sin(euler * T(0.5));
            return {s.x * c.y * c.z - c.x * s.y * s.z,
                    c.x * s.y * c.z + s.x * c.y * s.z,
                    c.x * c.y * s.z - s.x * s.y * c.z,
                    c.x * c.y * c.z + s.x * s.y * s.z};
        }
        /// The rotation of a rotation matrix (whose columns must be orthonormal).
        static QuatT FromMatrix(const Mat<T, 3, 3>& m) {
            const T fourXSq = m[0][0] - m[1][1] - m[2][2], fourYSq = m[1][1] - m[0][0] - m[2][2];
            const T fourZSq = m[2][2] - m[0][0] - m[1][1], fourWSq = m[0][0] + m[1][1] + m[2][2];
            int biggest = 0;
            T fourBiggestSq = fourWSq;
            if (fourXSq > fourBiggestSq) { fourBiggestSq = fourXSq; biggest = 1; }
            if (fourYSq > fourBiggestSq) { fourBiggestSq = fourYSq; biggest = 2; }
            if (fourZSq > fourBiggestSq) { fourBiggestSq = fourZSq; biggest = 3; }
            const T big = std::sqrt(fourBiggestSq + T(1)) * T(0.5);
            const T mult = T(0.25) / big;
            switch (biggest) {
                case 0: return {(m[1][2] - m[2][1]) * mult, (m[2][0] - m[0][2]) * mult, (m[0][1] - m[1][0]) * mult, big};
                case 1: return {big, (m[0][1] + m[1][0]) * mult, (m[2][0] + m[0][2]) * mult, (m[1][2] - m[2][1]) * mult};
                case 2: return {(m[0][1] + m[1][0]) * mult, big, (m[1][2] + m[2][1]) * mult, (m[2][0] - m[0][2]) * mult};
                default: return {(m[2][0] + m[0][2]) * mult, (m[1][2] + m[2][1]) * mult, big, (m[0][1] - m[1][0]) * mult};
            }
        }
        static QuatT FromMatrix(const Mat<T, 4, 4>& m) { return FromMatrix(Mat<T, 3, 3>(m)); }
        /// The rotation that turns -Z (forward) toward `direction` with +Y toward `up` (glm::quatLookAt).
        static QuatT LookRotation(const Vec<T, 3>& direction, const Vec<T, 3>& up = Vec<T, 3>::Up()) {
            Mat<T, 3, 3> m;
            m[2] = -Normalize(direction);
            const Vec<T, 3> right = Cross(up, m[2]);
            m[0] = right * (T(1) / std::sqrt(Max(T(1e-5), Dot(right, right))));
            m[1] = Cross(m[2], m[0]);
            return FromMatrix(m);
        }
        /// The shortest rotation that turns direction `from` onto direction `to`.
        static QuatT FromTo(const Vec<T, 3>& from, const Vec<T, 3>& to) {
            const Vec<T, 3> f = Normalize(from), t = Normalize(to);
            const T d = Dot(f, t);
            if (d >= T(1) - T(1e-6)) return {};
            if (d <= T(-1) + T(1e-6)) return QuatT(AnyPerpendicular(f), T(0));   // half a turn about anything perpendicular
            const T s = std::sqrt((T(1) + d) * T(2));
            return QuatT(Cross(f, t) * (T(1) / s), s * T(0.5));
        }
    };

    using Quat = QuatT<float>;
    using DQuat = QuatT<double>;
    static_assert(sizeof(Quat) == 16);

    template<std::floating_point T> constexpr bool operator==(const QuatT<T>& a, const QuatT<T>& b) {
        return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
    }
    template<std::floating_point T> constexpr QuatT<T> operator+(const QuatT<T>& a, const QuatT<T>& b) { return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w}; }
    template<std::floating_point T> constexpr QuatT<T> operator-(const QuatT<T>& a, const QuatT<T>& b) { return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w}; }
    template<std::floating_point T> constexpr QuatT<T> operator-(const QuatT<T>& q) { return {-q.x, -q.y, -q.z, -q.w}; }
    template<std::floating_point T> constexpr QuatT<T> operator*(const QuatT<T>& q, std::type_identity_t<T> s) { return {q.x * s, q.y * s, q.z * s, q.w * s}; }
    template<std::floating_point T> constexpr QuatT<T> operator*(std::type_identity_t<T> s, const QuatT<T>& q) { return q * s; }
    /// Rotation by b, then by a.
    template<std::floating_point T> constexpr QuatT<T> operator*(const QuatT<T>& p, const QuatT<T>& q) {
        return {p.w * q.x + p.x * q.w + p.y * q.z - p.z * q.y,
                p.w * q.y + p.y * q.w + p.z * q.x - p.x * q.z,
                p.w * q.z + p.z * q.w + p.x * q.y - p.y * q.x,
                p.w * q.w - p.x * q.x - p.y * q.y - p.z * q.z};
    }
    template<std::floating_point T> constexpr QuatT<T>& operator*=(QuatT<T>& p, const QuatT<T>& q) { return p = p * q; }
    /// `v` rotated by `q` (which must be unit length).
    template<std::floating_point T> constexpr Vec<T, 3> operator*(const QuatT<T>& q, const Vec<T, 3>& v) {
        const Vec<T, 3> u = q.XYZ();
        const Vec<T, 3> uv = Cross(u, v), uuv = Cross(u, uv);
        return v + (uv * q.w + uuv) * T(2);
    }

    template<std::floating_point T> constexpr T Dot(const QuatT<T>& a, const QuatT<T>& b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
    template<std::floating_point T> T Length(const QuatT<T>& q) { return std::sqrt(Dot(q, q)); }
    template<std::floating_point T> QuatT<T> Normalize(const QuatT<T>& q) {
        const T len = Length(q);
        return len > T(0) ? q * (T(1) / len) : QuatT<T>();
    }
    /// The inverse of a unit quaternion.
    template<std::floating_point T> constexpr QuatT<T> Conjugate(const QuatT<T>& q) { return {-q.x, -q.y, -q.z, q.w}; }
    template<std::floating_point T> constexpr QuatT<T> Inverse(const QuatT<T>& q) { return Conjugate(q) * (T(1) / Dot(q, q)); }
    /// The rotation angle, in radians [0, 2Pi).
    template<std::floating_point T> T Angle(const QuatT<T>& q) { return T(2) * std::acos(Clamp(q.w, T(-1), T(1))); }
    /// The rotation axis (+Z for the identity, which has none).
    template<std::floating_point T> Vec<T, 3> Axis(const QuatT<T>& q) {
        const T s2 = T(1) - q.w * q.w;
        return s2 <= T(0) ? Vec<T, 3>::UnitZ() : q.XYZ() * (T(1) / std::sqrt(s2));
    }
    /// Rotation about X, from EulerAngles.
    template<std::floating_point T> T Pitch(const QuatT<T>& q) {
        const T py = T(2) * (q.y * q.z + q.w * q.x), px = q.w * q.w - q.x * q.x - q.y * q.y + q.z * q.z;
        return (Abs(px) < T(1e-7) && Abs(py) < T(1e-7)) ? T(2) * std::atan2(q.x, q.w) : std::atan2(py, px);
    }
    /// Rotation about Y.
    template<std::floating_point T> T Yaw(const QuatT<T>& q) { return std::asin(Clamp(T(-2) * (q.x * q.z - q.w * q.y), T(-1), T(1))); }
    /// Rotation about Z.
    template<std::floating_point T> T Roll(const QuatT<T>& q) {
        return std::atan2(T(2) * (q.x * q.y + q.w * q.z), q.w * q.w + q.x * q.x - q.y * q.y - q.z * q.z);
    }
    /// (pitch, yaw, roll) in radians, as glm::eulerAngles returned them.
    template<std::floating_point T> Vec<T, 3> EulerAngles(const QuatT<T>& q) {
        const T py = T(2) * (q.y * q.z + q.w * q.x), px = q.w * q.w - q.x * q.x - q.y * q.y + q.z * q.z;
        const T pitch = (Abs(px) < T(1e-7) && Abs(py) < T(1e-7)) ? T(2) * std::atan2(q.x, q.w) : std::atan2(py, px);
        const T yaw = std::asin(Clamp(T(-2) * (q.x * q.z - q.w * q.y), T(-1), T(1)));
        const T roll = std::atan2(T(2) * (q.x * q.y + q.w * q.z), q.w * q.w + q.x * q.x - q.y * q.y - q.z * q.z);
        return {pitch, yaw, roll};
    }
    template<std::floating_point T> constexpr Mat<T, 3, 3> ToMat3(const QuatT<T>& q) {
        const T xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z, xz = q.x * q.z, xy = q.x * q.y, yz = q.y * q.z;
        const T wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
        return {Vec<T, 3>(T(1) - T(2) * (yy + zz), T(2) * (xy + wz), T(2) * (xz - wy)),
                Vec<T, 3>(T(2) * (xy - wz), T(1) - T(2) * (xx + zz), T(2) * (yz + wx)),
                Vec<T, 3>(T(2) * (xz + wy), T(2) * (yz - wx), T(1) - T(2) * (xx + yy))};
    }
    template<std::floating_point T> constexpr Mat<T, 4, 4> ToMat4(const QuatT<T>& q) { return Mat<T, 4, 4>(ToMat3(q)); }

    /// Normalised linear interpolation along the shorter arc: cheaper than Slerp, not constant speed.
    template<std::floating_point T> QuatT<T> Nlerp(const QuatT<T>& a, QuatT<T> b, T t) {
        if (Dot(a, b) < T(0)) b = -b;
        return Normalize(a + (b - a) * t);
    }
    /// Spherical interpolation along the shorter arc, at constant angular speed.
    template<std::floating_point T> QuatT<T> Slerp(const QuatT<T>& a, QuatT<T> b, T t) {
        T cosTheta = Dot(a, b);
        if (cosTheta < T(0)) { b = -b; cosTheta = -cosTheta; }
        if (cosTheta > T(1) - T(1e-6)) return Nlerp(a, b, t);
        const T angle = std::acos(cosTheta);
        return (a * std::sin((T(1) - t) * angle) + b * std::sin(t * angle)) * (T(1) / std::sin(angle));
    }
    /// q followed by `angle` radians about `axis` in q's frame: glm::rotate(q, angle, axis), q * AngleAxis(angle, axis).
    template<std::floating_point T> QuatT<T> Rotate(const QuatT<T>& q, T angle, const Vec<T, 3>& axis) { return q * QuatT<T>::AngleAxis(angle, axis); }
    /// v turned `angle` radians about `axis` (glm's rotate_vector).
    template<std::floating_point T> Vec<T, 3> Rotate(const Vec<T, 3>& v, T angle, const Vec<T, 3>& axis) { return QuatT<T>::AngleAxis(angle, axis) * v; }
    /// Component-wise interpolation, not normalised (glm::lerp of quaternions): Slerp or Nlerp for rotations.
    template<std::floating_point T> constexpr QuatT<T> Lerp(const QuatT<T>& a, const QuatT<T>& b, T t) { return a + (b - a) * t; }
    /// glm::mix of quaternions: spherical, along the shorter arc.
    template<std::floating_point T> QuatT<T> Mix(const QuatT<T>& a, const QuatT<T>& b, T t) { return Slerp(a, b, t); }
    /// Rotates `from` toward `to` by at most `maxRadians`.
    template<std::floating_point T> QuatT<T> RotateTowards(const QuatT<T>& from, const QuatT<T>& to, T maxRadians) {
        const T angle = T(2) * std::acos(Clamp(Abs(Dot(from, to)), T(0), T(1)));
        return angle <= maxRadians || angle == T(0) ? to : Slerp(from, to, maxRadians / angle);
    }
    template<std::floating_point T> constexpr bool ApproxEqual(const QuatT<T>& a, const QuatT<T>& b, T epsilon = Epsilon<T>) {
        return ApproxEqual(a.x, b.x, epsilon) && ApproxEqual(a.y, b.y, epsilon) && ApproxEqual(a.z, b.z, epsilon) && ApproxEqual(a.w, b.w, epsilon);
    }
    /// The same rotation, whichever of q and -q (both represent it) was given.
    template<std::floating_point T> constexpr bool SameRotation(const QuatT<T>& a, const QuatT<T>& b, T epsilon = Epsilon<T>) {
        return ApproxEqual(a, b, epsilon) || ApproxEqual(a, -b, epsilon);
    }

    template<std::floating_point T>
    std::ostream& operator<<(std::ostream& os, const QuatT<T>& q) { return os << "Quat(" << q.x << ", " << q.y << ", " << q.z << ", " << q.w << ')'; }
}

template<std::floating_point T, class Char>
struct std::formatter<kor::QuatT<T>, Char> : std::formatter<kor::Vec<T, 4>, Char> {
    template<class Context>
    auto format(const kor::QuatT<T>& q, Context& ctx) const {
        return std::formatter<kor::Vec<T, 4>, Char>::format(kor::Vec<T, 4>(q.x, q.y, q.z, q.w), ctx);
    }
};
