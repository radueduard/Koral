#pragma once

// Euler angles as glm's gtx/euler_angle has them: rotation matrices built from angles about the axes in a
// given order, and the angles read back out of a matrix.
//
// EulerAngleXYZ(a, b, c) is EulerAngleX(a) * EulerAngleY(b) * EulerAngleZ(c): applied to a column vector,
// Z turns it first. Every order is also reachable through the EulerOrder enum, which is what the
// bindings use.

#include "matrix.h"
#include "transform.h"

namespace kor {

    /// The axes, outermost first: XYZ is X(a) * Y(b) * Z(c). The first six are Tait-Bryan (three
    /// different axes), the last six proper Euler (first axis repeated).
    enum class EulerOrder : u8 { eXYZ, eXZY, eYXZ, eYZX, eZXY, eZYX, eXYX, eXZX, eYXY, eYZY, eZXZ, eZYZ };

    namespace detail {
        /// The rotation of `angle` radians about axis 0, 1 or 2.
        template<std::floating_point T> Mat<T, 4, 4> AxisRotation(int axis, T angle) {
            const T c = std::cos(angle), s = std::sin(angle);
            Mat<T, 4, 4> m;
            const int u = (axis + 1) % 3, v = (axis + 2) % 3;
            m[u][u] = c;
            m[u][v] = s;
            m[v][u] = -s;
            m[v][v] = c;
            return m;
        }
        constexpr Vec<int, 3> Axes(EulerOrder order) {
            constexpr int table[12][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0},
                                          {0, 1, 0}, {0, 2, 0}, {1, 0, 1}, {1, 2, 1}, {2, 0, 2}, {2, 1, 2}};
            const auto& a = table[static_cast<int>(order)];
            return {a[0], a[1], a[2]};
        }
    }

    template<std::floating_point T> Mat<T, 4, 4> EulerAngleX(T angle) { return detail::AxisRotation(0, angle); }
    template<std::floating_point T> Mat<T, 4, 4> EulerAngleY(T angle) { return detail::AxisRotation(1, angle); }
    template<std::floating_point T> Mat<T, 4, 4> EulerAngleZ(T angle) { return detail::AxisRotation(2, angle); }

    /// The rotation `angles` = (first, second, third) radians describe in `order`.
    template<std::floating_point T> Mat<T, 4, 4> EulerAngles(EulerOrder order, const Vec<T, 3>& angles) {
        const Vec<int, 3> a = detail::Axes(order);
        return detail::AxisRotation(a.x, angles.x) * detail::AxisRotation(a.y, angles.y) * detail::AxisRotation(a.z, angles.z);
    }

    /**
     * The angles that rebuild `m`'s rotation in `order` (the inverse of EulerAngles). The middle angle comes
     * back in [-pi/2, pi/2] for Tait-Bryan orders and [0, pi] for proper ones; at gimbal lock the third is 0.
     */
    template<std::floating_point T> Vec<T, 3> ExtractEulerAngles(EulerOrder order, const Mat<T, 4, 4>& m) {
        const Vec<int, 3> a = detail::Axes(order);
        const auto at = [&](int row, int column) { return m[column][row]; };
        constexpr T lock = T(1e-6);
        if (a.x != a.z) {
            const int i = a.x, j = a.y, k = a.z;
            const T e = ((j - i + 3) % 3 == 1) ? T(1) : T(-1);  // even permutation (XYZ, YZX, ZXY)?
            const T cb = std::sqrt(at(i, i) * at(i, i) + at(i, j) * at(i, j));
            const T b = std::atan2(e * at(i, k), cb);
            if (cb < lock) return {std::atan2(e * at(k, j), at(j, j)), b, T(0)};
            return {std::atan2(-e * at(j, k), at(k, k)), b, std::atan2(-e * at(i, j), at(i, i))};
        }
        // Proper Euler, R = I(a) J(b) I(c), with K the remaining axis.
        const int i = a.x, j = a.y, k = 3 - i - j;
        const T e = ((j - i + 3) % 3 == 1) ? T(1) : T(-1);
        const T sb = std::sqrt(at(i, j) * at(i, j) + at(i, k) * at(i, k));
        const T b = std::atan2(sb, at(i, i));
        if (sb < lock) return {std::atan2(e * at(k, j), at(j, j)), b, T(0)};
        return {std::atan2(at(j, i), -e * at(k, i)), b, std::atan2(at(i, j), e * at(i, k))};
    }

#define KOR_EULER_2(A, B, I, J)                                                                                                    \
    template<std::floating_point T> Mat<T, 4, 4> EulerAngle##A##B(T first, T second) {                                             \
        return detail::AxisRotation(I, first) * detail::AxisRotation(J, second);                                                   \
    }
    KOR_EULER_2(X, Y, 0, 1)
    KOR_EULER_2(X, Z, 0, 2)
    KOR_EULER_2(Y, X, 1, 0)
    KOR_EULER_2(Y, Z, 1, 2)
    KOR_EULER_2(Z, X, 2, 0)
    KOR_EULER_2(Z, Y, 2, 1)
#undef KOR_EULER_2

#define KOR_EULER_3(Name)                                                                                                          \
    template<std::floating_point T> Mat<T, 4, 4> EulerAngle##Name(T first, T second, T third) {                                    \
        return EulerAngles(EulerOrder::e##Name, Vec<T, 3>(first, second, third));                                                  \
    }                                                                                                                              \
    template<std::floating_point T> Vec<T, 3> ExtractEulerAngle##Name(const Mat<T, 4, 4>& m) { return ExtractEulerAngles(EulerOrder::e##Name, m); }
    KOR_EULER_3(XYZ)
    KOR_EULER_3(XZY)
    KOR_EULER_3(YXZ)
    KOR_EULER_3(YZX)
    KOR_EULER_3(ZXY)
    KOR_EULER_3(ZYX)
    KOR_EULER_3(XYX)
    KOR_EULER_3(XZX)
    KOR_EULER_3(YXY)
    KOR_EULER_3(YZY)
    KOR_EULER_3(ZXZ)
    KOR_EULER_3(ZYZ)
#undef KOR_EULER_3

    /// glm::yawPitchRoll: yaw about Y, then pitch about X, then roll about Z, outermost first (Y * X * Z).
    template<std::floating_point T> Mat<T, 4, 4> YawPitchRoll(T yaw, T pitch, T roll) { return EulerAngleYXZ(yaw, pitch, roll); }

    /// The quaternion of the same rotation as EulerAngles(order, angles).
    template<std::floating_point T> QuatT<T> QuatFromEuler(EulerOrder order, const Vec<T, 3>& angles) {
        const Vec<int, 3> a = detail::Axes(order);
        const auto axis = [](int i) { Vec<T, 3> v(T(0)); v[i] = T(1); return v; };
        return QuatT<T>::AngleAxis(angles.x, axis(a.x)) * QuatT<T>::AngleAxis(angles.y, axis(a.y)) * QuatT<T>::AngleAxis(angles.z, axis(a.z));
    }

}
