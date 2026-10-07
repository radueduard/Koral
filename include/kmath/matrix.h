#pragma once

// kor::Mat<T, C, R>: C columns of R-component vectors, column after column in memory — the layout GLSL,
// Slang's column_major default, glm and OpenGL all share, so a Mat4 uploads as is. m[c] is column c and
// m[c][r] the element in row r. Vectors are columns: `projection * view * model * point` applies model
// first. A square matrix defaults to the identity.

#include "vector.h"

namespace kor {
    template<Scalar T, int C, int R>
    struct Mat {
        using value_type = T;
        using Column = Vec<T, R>;
        static constexpr int Columns = C;
        static constexpr int Rows = R;

        Column columns[C];

        /// The identity for a square matrix (ones on the diagonal of any other).
        constexpr Mat() : Mat(T(1)) {}
        /// `diagonal` on the diagonal, zero elsewhere: Mat4(2) scales by two, Mat4(0) is all zero.
        constexpr explicit Mat(T diagonal) : columns{} {
            for (int i = 0; i < (C < R ? C : R); ++i) columns[i][i] = diagonal;
        }
        /// From its columns.
        constexpr Mat(const Column& c0, const Column& c1) requires (C == 2) : columns{c0, c1} {}
        constexpr Mat(const Column& c0, const Column& c1, const Column& c2) requires (C == 3) : columns{c0, c1, c2} {}
        constexpr Mat(const Column& c0, const Column& c1, const Column& c2, const Column& c3) requires (C == 4) : columns{c0, c1, c2, c3} {}
        /// From C * R scalars, column after column (as glm's and GLSL's constructors take them).
        template<class... Ts> requires (sizeof...(Ts) == C * R && sizeof...(Ts) > 1 && (std::is_arithmetic_v<Ts> && ...))
        constexpr Mat(Ts... values) : columns{} {
            const T flat[] = {T(values)...};
            for (int c = 0; c < C; ++c)
                for (int r = 0; r < R; ++r) columns[c][r] = flat[c * R + r];
        }
        /// From a matrix of another size or type: the overlap is copied, the rest is the identity's.
        template<Scalar U, int C2, int R2> requires (C2 != C || R2 != R || !std::is_same_v<U, T>)
        constexpr explicit Mat(const Mat<U, C2, R2>& m) : Mat(T(1)) {
            for (int c = 0; c < (C < C2 ? C : C2); ++c)
                for (int r = 0; r < (R < R2 ? R : R2); ++r) columns[c][r] = T(m[c][r]);
        }

        constexpr Column& operator[](int c) { return columns[c]; }
        constexpr const Column& operator[](int c) const { return columns[c]; }
        constexpr Vec<T, C> Row(int r) const {
            Vec<T, C> row;
            for (int c = 0; c < C; ++c) row[c] = columns[c][r];
            return row;
        }
        constexpr T* data() { return columns[0].data(); }
        constexpr const T* data() const { return columns[0].data(); }

        static constexpr Mat Identity() requires (C == R) { return Mat(T(1)); }
        static constexpr Mat Zero() { return Mat(T(0)); }
    };

    using Mat2 = Mat<float, 2, 2>;
    using Mat3 = Mat<float, 3, 3>;
    using Mat4 = Mat<float, 4, 4>;
    /// An affine transform without its constant last row: 3 rows, 4 columns (Vulkan's VkTransformMatrixKHR is its transpose).
    using Mat4x3 = Mat<float, 4, 3>;
    using DMat3 = Mat<double, 3, 3>;
    using DMat4 = Mat<double, 4, 4>;

    static_assert(sizeof(Mat4) == 64 && sizeof(Mat3) == 36);
    static_assert(std::is_trivially_copyable_v<Mat4>);

    template<Scalar T, int C, int R>
    constexpr bool operator==(const Mat<T, C, R>& a, const Mat<T, C, R>& b) {
        for (int c = 0; c < C; ++c)
            if (!(a[c] == b[c])) return false;
        return true;
    }

    template<Scalar T, int C, int R> constexpr Mat<T, C, R> operator+(const Mat<T, C, R>& a, const Mat<T, C, R>& b) {
        Mat<T, C, R> m(T(0));
        for (int c = 0; c < C; ++c) m[c] = a[c] + b[c];
        return m;
    }
    template<Scalar T, int C, int R> constexpr Mat<T, C, R> operator-(const Mat<T, C, R>& a, const Mat<T, C, R>& b) {
        Mat<T, C, R> m(T(0));
        for (int c = 0; c < C; ++c) m[c] = a[c] - b[c];
        return m;
    }
    template<Scalar T, int C, int R> constexpr Mat<T, C, R> operator-(const Mat<T, C, R>& a) {
        Mat<T, C, R> m(T(0));
        for (int c = 0; c < C; ++c) m[c] = -a[c];
        return m;
    }
    template<Scalar T, int C, int R> constexpr Mat<T, C, R> operator*(const Mat<T, C, R>& a, std::type_identity_t<T> s) {
        Mat<T, C, R> m(T(0));
        for (int c = 0; c < C; ++c) m[c] = a[c] * s;
        return m;
    }
    template<Scalar T, int C, int R> constexpr Mat<T, C, R> operator*(std::type_identity_t<T> s, const Mat<T, C, R>& a) { return a * s; }

    /// Matrix times column vector.
    template<Scalar T, int C, int R> constexpr Vec<T, R> operator*(const Mat<T, C, R>& m, const Vec<T, C>& v) {
        Vec<T, R> out = m[0] * v[0];
        for (int c = 1; c < C; ++c) out += m[c] * v[c];
        return out;
    }
    /// Row vector times matrix: the same as Transpose(m) * v.
    template<Scalar T, int C, int R> constexpr Vec<T, C> operator*(const Vec<T, R>& v, const Mat<T, C, R>& m) {
        Vec<T, C> out;
        for (int c = 0; c < C; ++c) out[c] = Dot(v, m[c]);
        return out;
    }
    /// a * b: b applied first.
    template<Scalar T, int K, int R, int C>
    constexpr Mat<T, C, R> operator*(const Mat<T, K, R>& a, const Mat<T, C, K>& b) {
        Mat<T, C, R> m(T(0));
        for (int c = 0; c < C; ++c) m[c] = a * b[c];
        return m;
    }
    template<Scalar T, int N> constexpr Mat<T, N, N>& operator*=(Mat<T, N, N>& a, const Mat<T, N, N>& b) { return a = a * b; }

    template<Scalar T, int C, int R>
    constexpr Mat<T, R, C> Transpose(const Mat<T, C, R>& m) {
        Mat<T, R, C> t(T(0));
        for (int c = 0; c < C; ++c)
            for (int r = 0; r < R; ++r) t[r][c] = m[c][r];
        return t;
    }

    template<Scalar T> constexpr T Determinant(const Mat<T, 2, 2>& m) { return m[0][0] * m[1][1] - m[1][0] * m[0][1]; }
    template<Scalar T> constexpr T Determinant(const Mat<T, 3, 3>& m) { return Dot(m[0], Cross(m[1], m[2])); }
    template<Scalar T> constexpr T Determinant(const Mat<T, 4, 4>& m) {
        const T s0 = m[2][2] * m[3][3] - m[3][2] * m[2][3], s1 = m[2][1] * m[3][3] - m[3][1] * m[2][3];
        const T s2 = m[2][1] * m[3][2] - m[3][1] * m[2][2], s3 = m[2][0] * m[3][3] - m[3][0] * m[2][3];
        const T s4 = m[2][0] * m[3][2] - m[3][0] * m[2][2], s5 = m[2][0] * m[3][1] - m[3][0] * m[2][1];
        const T c0 = +(m[1][1] * s0 - m[1][2] * s1 + m[1][3] * s2);
        const T c1 = -(m[1][0] * s0 - m[1][2] * s3 + m[1][3] * s4);
        const T c2 = +(m[1][0] * s1 - m[1][1] * s3 + m[1][3] * s5);
        const T c3 = -(m[1][0] * s2 - m[1][1] * s4 + m[1][2] * s5);
        return m[0][0] * c0 + m[0][1] * c1 + m[0][2] * c2 + m[0][3] * c3;
    }

    /// The inverse; a singular matrix gives non-finite elements (check Determinant first if that can happen).
    template<std::floating_point T> constexpr Mat<T, 2, 2> Inverse(const Mat<T, 2, 2>& m) {
        const T inv = T(1) / Determinant(m);
        return {Vec<T, 2>(m[1][1] * inv, -m[0][1] * inv), Vec<T, 2>(-m[1][0] * inv, m[0][0] * inv)};
    }
    template<std::floating_point T> constexpr Mat<T, 3, 3> Inverse(const Mat<T, 3, 3>& m) {
        const Vec<T, 3> r0 = Cross(m[1], m[2]), r1 = Cross(m[2], m[0]), r2 = Cross(m[0], m[1]);
        const T inv = T(1) / Dot(m[0], r0);
        return Transpose(Mat<T, 3, 3>(r0 * inv, r1 * inv, r2 * inv));
    }
    template<std::floating_point T> constexpr Mat<T, 4, 4> Inverse(const Mat<T, 4, 4>& m) {
        // Cofactors by 2x2 sub-determinants (the layout of glm's and MESA's gluInvertMatrix).
        const T a2323 = m[2][2] * m[3][3] - m[2][3] * m[3][2], a1323 = m[2][1] * m[3][3] - m[2][3] * m[3][1];
        const T a1223 = m[2][1] * m[3][2] - m[2][2] * m[3][1], a0323 = m[2][0] * m[3][3] - m[2][3] * m[3][0];
        const T a0223 = m[2][0] * m[3][2] - m[2][2] * m[3][0], a0123 = m[2][0] * m[3][1] - m[2][1] * m[3][0];
        const T a2313 = m[1][2] * m[3][3] - m[1][3] * m[3][2], a1313 = m[1][1] * m[3][3] - m[1][3] * m[3][1];
        const T a1213 = m[1][1] * m[3][2] - m[1][2] * m[3][1], a2312 = m[1][2] * m[2][3] - m[1][3] * m[2][2];
        const T a1312 = m[1][1] * m[2][3] - m[1][3] * m[2][1], a1212 = m[1][1] * m[2][2] - m[1][2] * m[2][1];
        const T a0313 = m[1][0] * m[3][3] - m[1][3] * m[3][0], a0213 = m[1][0] * m[3][2] - m[1][2] * m[3][0];
        const T a0312 = m[1][0] * m[2][3] - m[1][3] * m[2][0], a0212 = m[1][0] * m[2][2] - m[1][2] * m[2][0];
        const T a0113 = m[1][0] * m[3][1] - m[1][1] * m[3][0], a0112 = m[1][0] * m[2][1] - m[1][1] * m[2][0];

        const T det = m[0][0] * (m[1][1] * a2323 - m[1][2] * a1323 + m[1][3] * a1223)
                    - m[0][1] * (m[1][0] * a2323 - m[1][2] * a0323 + m[1][3] * a0223)
                    + m[0][2] * (m[1][0] * a1323 - m[1][1] * a0323 + m[1][3] * a0123)
                    - m[0][3] * (m[1][0] * a1223 - m[1][1] * a0223 + m[1][2] * a0123);
        const T inv = T(1) / det;

        Mat<T, 4, 4> r(T(0));
        r[0][0] = inv * (m[1][1] * a2323 - m[1][2] * a1323 + m[1][3] * a1223);
        r[0][1] = inv * -(m[0][1] * a2323 - m[0][2] * a1323 + m[0][3] * a1223);
        r[0][2] = inv * (m[0][1] * a2313 - m[0][2] * a1313 + m[0][3] * a1213);
        r[0][3] = inv * -(m[0][1] * a2312 - m[0][2] * a1312 + m[0][3] * a1212);
        r[1][0] = inv * -(m[1][0] * a2323 - m[1][2] * a0323 + m[1][3] * a0223);
        r[1][1] = inv * (m[0][0] * a2323 - m[0][2] * a0323 + m[0][3] * a0223);
        r[1][2] = inv * -(m[0][0] * a2313 - m[0][2] * a0313 + m[0][3] * a0213);
        r[1][3] = inv * (m[0][0] * a2312 - m[0][2] * a0312 + m[0][3] * a0212);
        r[2][0] = inv * (m[1][0] * a1323 - m[1][1] * a0323 + m[1][3] * a0123);
        r[2][1] = inv * -(m[0][0] * a1323 - m[0][1] * a0323 + m[0][3] * a0123);
        r[2][2] = inv * (m[0][0] * a1313 - m[0][1] * a0313 + m[0][3] * a0113);
        r[2][3] = inv * -(m[0][0] * a1312 - m[0][1] * a0312 + m[0][3] * a0112);
        r[3][0] = inv * -(m[1][0] * a1223 - m[1][1] * a0223 + m[1][2] * a0123);
        r[3][1] = inv * (m[0][0] * a1223 - m[0][1] * a0223 + m[0][2] * a0123);
        r[3][2] = inv * -(m[0][0] * a1213 - m[0][1] * a0213 + m[0][2] * a0113);
        r[3][3] = inv * (m[0][0] * a1212 - m[0][1] * a0212 + m[0][2] * a0112);
        return r;
    }

    /// The matrix that transforms normals for `model`: the inverse transpose of its upper 3x3.
    template<std::floating_point T> constexpr Mat<T, 3, 3> NormalMatrix(const Mat<T, 4, 4>& model) {
        return Transpose(Inverse(Mat<T, 3, 3>(model)));
    }

    template<std::floating_point T, int C, int R>
    constexpr bool ApproxEqual(const Mat<T, C, R>& a, const Mat<T, C, R>& b, T epsilon = Epsilon<T>) {
        for (int c = 0; c < C; ++c)
            if (!ApproxEqual(a[c], b[c], epsilon)) return false;
        return true;
    }

    template<Scalar T, int C, int R>
    std::ostream& operator<<(std::ostream& os, const Mat<T, C, R>& m) {
        os << '[';
        for (int r = 0; r < R; ++r) os << (r ? "; " : "") << m.Row(r);
        return os << ']';
    }
}

/// `{}` formats a matrix row by row: [(r0) ; (r1) ...]; a format spec applies to every element.
template<kor::Scalar T, int C, int R, class Char>
struct std::formatter<kor::Mat<T, C, R>, Char> : std::formatter<kor::Vec<T, C>, Char> {
    template<class Context>
    auto format(const kor::Mat<T, C, R>& m, Context& ctx) const {
        auto out = ctx.out();
        *out++ = Char('[');
        for (int r = 0; r < R; ++r) {
            if (r) { *out++ = Char(';'); *out++ = Char(' '); }
            ctx.advance_to(out);
            out = std::formatter<kor::Vec<T, C>, Char>::format(m.Row(r), ctx);
        }
        *out++ = Char(']');
        return out;
    }
};
