#pragma once

// Building transform matrices, and kor::Transform — position, rotation, scale — for things placed in a world.
//
// Koral's conventions, which every function here follows: right-handed, +Y up, a camera looks down -Z, and
// clip-space depth runs 0 (near) to 1 (far) as Vulkan has it. The projections here do NOT flip Y: Vulkan's
// clip space points Y down, so a camera negates [1][1] (the camera module's do) or flips the viewport.

#include "quaternion.h"

namespace kor {
    // ---- pure matrices ---------------------------------------------------------------------------------

    template<std::floating_point T> constexpr Mat<T, 4, 4> Translation(const Vec<T, 3>& by) {
        Mat<T, 4, 4> m;
        m[3] = Vec<T, 4>(by, T(1));
        return m;
    }
    template<std::floating_point T> constexpr Mat<T, 4, 4> Scaling(const Vec<T, 3>& by) {
        Mat<T, 4, 4> m;
        m[0][0] = by.x; m[1][1] = by.y; m[2][2] = by.z;
        return m;
    }
    /// `angle` radians counter-clockwise about `axis` (normalised for you).
    template<std::floating_point T> Mat<T, 4, 4> Rotation(T angle, const Vec<T, 3>& axis) {
        return ToMat4(QuatT<T>::AngleAxis(angle, axis));
    }
    template<std::floating_point T> constexpr Mat<T, 4, 4> Rotation(const QuatT<T>& q) { return ToMat4(q); }
    /// Translation * Rotation * Scale: scale first, then rotate, then move.
    template<std::floating_point T> constexpr Mat<T, 4, 4> Compose(const Vec<T, 3>& translation, const QuatT<T>& rotation, const Vec<T, 3>& scale) {
        const Mat<T, 3, 3> r = ToMat3(rotation);
        return {Vec<T, 4>(r[0] * scale.x, T(0)), Vec<T, 4>(r[1] * scale.y, T(0)), Vec<T, 4>(r[2] * scale.z, T(0)), Vec<T, 4>(translation, T(1))};
    }
    /**
     * Splits an affine matrix back into what Compose took. A negative determinant (a mirror) is put in the
     * X scale. Shear and projection are not representable and are lost. Returns false for a degenerate matrix.
     */
    template<std::floating_point T>
    bool Decompose(const Mat<T, 4, 4>& m, Vec<T, 3>& translation, QuatT<T>& rotation, Vec<T, 3>& scale) {
        translation = Vec<T, 3>(m[3]);
        Vec<T, 3> c0(m[0]), c1(m[1]), c2(m[2]);
        scale = {Length(c0), Length(c1), Length(c2)};
        if (scale.x == T(0) || scale.y == T(0) || scale.z == T(0)) return false;
        if (Dot(c0, Cross(c1, c2)) < T(0)) scale.x = -scale.x;
        rotation = Normalize(QuatT<T>::FromMatrix(Mat<T, 3, 3>(c0 / scale.x, c1 / scale.y, c2 / scale.z)));
        return true;
    }

    // ---- glm-style: post-multiplied onto an existing matrix --------------------------------------------

    /// m * Translation(by).
    template<std::floating_point T> constexpr Mat<T, 4, 4> Translate(const Mat<T, 4, 4>& m, const Vec<T, 3>& by) {
        Mat<T, 4, 4> r = m;
        r[3] = m[0] * by.x + m[1] * by.y + m[2] * by.z + m[3];
        return r;
    }
    /// m * Rotation(angle, axis).
    template<std::floating_point T> Mat<T, 4, 4> Rotate(const Mat<T, 4, 4>& m, T angle, const Vec<T, 3>& axis) { return m * Rotation(angle, axis); }
    /// m * Scaling(by).
    template<std::floating_point T> constexpr Mat<T, 4, 4> Scale(const Mat<T, 4, 4>& m, const Vec<T, 3>& by) {
        return {m[0] * by.x, m[1] * by.y, m[2] * by.z, m[3]};
    }

    // ---- cameras ----------------------------------------------------------------------------------------

    /// A right-handed view matrix: the camera at `eye` looking at `target`.
    template<std::floating_point T> Mat<T, 4, 4> LookAt(const Vec<T, 3>& eye, const Vec<T, 3>& target, const Vec<T, 3>& up = Vec<T, 3>::Up()) {
        const Vec<T, 3> f = Normalize(target - eye), s = Normalize(Cross(f, up)), u = Cross(s, f);
        Mat<T, 4, 4> m;
        m[0] = {s.x, u.x, -f.x, T(0)};
        m[1] = {s.y, u.y, -f.y, T(0)};
        m[2] = {s.z, u.z, -f.z, T(0)};
        m[3] = {-Dot(s, eye), -Dot(u, eye), Dot(f, eye), T(1)};
        return m;
    }
    /// Perspective projection, vertical field of view in radians, depth 0 at `near` to 1 at `far`.
    template<std::floating_point T> Mat<T, 4, 4> Perspective(T fovY, T aspect, T near, T far) {
        const T f = T(1) / std::tan(fovY * T(0.5));
        Mat<T, 4, 4> m(T(0));
        m[0][0] = f / aspect;
        m[1][1] = f;
        m[2][2] = far / (near - far);
        m[2][3] = T(-1);
        m[3][2] = -(far * near) / (far - near);
        return m;
    }
    /// Perspective with reversed depth (1 at near, 0 at far) — far better depth precision with a float
    /// depth buffer; compare with eGreater and clear depth to 0. Pass an infinite `far` for no far plane.
    template<std::floating_point T> Mat<T, 4, 4> PerspectiveReversedZ(T fovY, T aspect, T near, T far = std::numeric_limits<T>::infinity()) {
        const T f = T(1) / std::tan(fovY * T(0.5));
        Mat<T, 4, 4> m(T(0));
        m[0][0] = f / aspect;
        m[1][1] = f;
        m[2][3] = T(-1);
        if (std::isinf(far)) {
            m[3][2] = near;
        } else {
            m[2][2] = near / (far - near);
            m[3][2] = far * near / (far - near);
        }
        return m;
    }
    /// Orthographic projection of the box [left, right] x [bottom, top] x [-near, -far], depth 0 to 1.
    template<std::floating_point T> constexpr Mat<T, 4, 4> Orthographic(T left, T right, T bottom, T top, T near, T far) {
        Mat<T, 4, 4> m;
        m[0][0] = T(2) / (right - left);
        m[1][1] = T(2) / (top - bottom);
        m[2][2] = T(-1) / (far - near);
        m[3][0] = -(right + left) / (right - left);
        m[3][1] = -(top + bottom) / (top - bottom);
        m[3][2] = -near / (far - near);
        return m;
    }

    // ---- applying a matrix ------------------------------------------------------------------------------

    /// m * (p, 1), without the projective divide (for affine m).
    template<std::floating_point T> constexpr Vec<T, 3> TransformPoint(const Mat<T, 4, 4>& m, const Vec<T, 3>& p) {
        return Vec<T, 3>(m[0]) * p.x + Vec<T, 3>(m[1]) * p.y + Vec<T, 3>(m[2]) * p.z + Vec<T, 3>(m[3]);
    }
    /// m * (p, 1) divided by its w: for projection matrices.
    template<std::floating_point T> constexpr Vec<T, 3> TransformPointProjective(const Mat<T, 4, 4>& m, const Vec<T, 3>& p) {
        const Vec<T, 4> h = m * Vec<T, 4>(p, T(1));
        return Vec<T, 3>(h) / h.w;
    }
    /// m * (d, 0): a direction, unaffected by translation (not normalised; use NormalMatrix for normals).
    template<std::floating_point T> constexpr Vec<T, 3> TransformDirection(const Mat<T, 4, 4>& m, const Vec<T, 3>& d) {
        return Vec<T, 3>(m[0]) * d.x + Vec<T, 3>(m[1]) * d.y + Vec<T, 3>(m[2]) * d.z;
    }

    // ---- kor::Transform --------------------------------------------------------------------------------

    /**
     * Where something is: a position, a rotation and a per-axis scale, applied scale first. Unlike a matrix it
     * can be edited per part and interpolated. Combining two (`parent * child`) is exact unless the parent's
     * scale is non-uniform *and* the child is rotated relative to it — a shear no Transform can hold; use the
     * matrices when that matters.
     */
    template<std::floating_point T>
    struct TransformT {
        Vec<T, 3> position{};
        QuatT<T> rotation{};
        Vec<T, 3> scale{T(1)};

        constexpr TransformT() = default;
        constexpr TransformT(const Vec<T, 3>& position, const QuatT<T>& rotation = {}, const Vec<T, 3>& scale = Vec<T, 3>(T(1)))
            : position(position), rotation(rotation), scale(scale) {}
        /// From an affine matrix (see Decompose for what is lost).
        explicit TransformT(const Mat<T, 4, 4>& m) { Decompose(m, position, rotation, scale); }

        static constexpr TransformT Identity() { return {}; }

        constexpr Mat<T, 4, 4> Matrix() const { return Compose(position, rotation, scale); }
        /// The matrix that undoes this one: world to local.
        Mat<T, 4, 4> InverseMatrix() const { return Inverse(Matrix()); }

        constexpr Vec<T, 3> Forward() const { return rotation * Vec<T, 3>::Forward(); }
        constexpr Vec<T, 3> Right() const { return rotation * Vec<T, 3>::Right(); }
        constexpr Vec<T, 3> Up() const { return rotation * Vec<T, 3>::Up(); }

        /// A local point, in the space this transform is in.
        constexpr Vec<T, 3> TransformPoint(const Vec<T, 3>& p) const { return position + rotation * (p * scale); }
        /// A local direction (rotated and scaled, not moved).
        constexpr Vec<T, 3> TransformDirection(const Vec<T, 3>& d) const { return rotation * (d * scale); }
        Vec<T, 3> InverseTransformPoint(const Vec<T, 3>& p) const { return (Conjugate(rotation) * (p - position)) / scale; }
        Vec<T, 3> InverseTransformDirection(const Vec<T, 3>& d) const { return (Conjugate(rotation) * d) / scale; }

        /// Turns to look at `target` with +Y toward `up`.
        TransformT& LookAt(const Vec<T, 3>& target, const Vec<T, 3>& up = Vec<T, 3>::Up()) {
            rotation = QuatT<T>::LookRotation(target - position, up);
            return *this;
        }
        TransformT& Translate(const Vec<T, 3>& by) { position += by; return *this; }
        /// Rotates by `q` in world space (q applied after the current rotation).
        TransformT& Rotate(const QuatT<T>& q) { rotation = Normalize(q * rotation); return *this; }

        /// The transform that undoes this one (exact for uniform scale).
        TransformT Inverse() const {
            const QuatT<T> inv = Conjugate(rotation);
            const Vec<T, 3> invScale = Vec<T, 3>(T(1)) / scale;
            return {inv * (-position * invScale), inv, invScale};
        }
    };

    /// `child` placed inside `parent`.
    template<std::floating_point T>
    constexpr TransformT<T> operator*(const TransformT<T>& parent, const TransformT<T>& child) {
        return {parent.TransformPoint(child.position), parent.rotation * child.rotation, parent.scale * child.scale};
    }
    template<std::floating_point T>
    constexpr bool operator==(const TransformT<T>& a, const TransformT<T>& b) {
        return a.position == b.position && a.rotation == b.rotation && a.scale == b.scale;
    }
    /// Lerps position and scale, slerps rotation.
    template<std::floating_point T>
    TransformT<T> Lerp(const TransformT<T>& a, const TransformT<T>& b, T t) {
        return {Lerp(a.position, b.position, t), Slerp(a.rotation, b.rotation, t), Lerp(a.scale, b.scale, t)};
    }

    using Transform = TransformT<float>;
}
