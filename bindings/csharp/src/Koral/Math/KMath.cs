namespace Koral;

/// <summary>
/// kmath's free functions — kor::Dot, kor::Normalize, kor::Perspective, ... — under the same names. With
/// <c>using static Koral.KMath;</c> they read exactly as in C++: <c>Normalize(Cross(a, b))</c>. The GLSL chapters
/// (trigonometric, exponential, common, geometric, relational, integer, packing) and the matrices are generated
/// (Generated/KMath.Glsl.g.cs, Generated/Matrices.g.cs) for every type C++ has them for; this file is the rest.
/// </summary>
/// <remarks>
/// Each does its arithmetic in the order the C++ one does, so what is only additions, multiplications,
/// divisions, square roots and Floor gives the same bits as Koral itself (the tests check it against the C
/// interface); what goes through MathF.Sin and friends agrees to within the last bit or two.
/// </remarks>
public static partial class KMath
{
    // ---- quaternions (kmath/quaternion.h) ------------------------------------------------------------

    public static float Dot(Quat a, Quat b) => a.X * b.X + a.Y * b.Y + a.Z * b.Z + a.W * b.W;
    public static float Length(Quat q) => MathF.Sqrt(Dot(q, q));
    public static Quat Normalize(Quat q) { float len = Length(q); return len > 0f ? q * (1f / len) : Quat.Identity; }
    public static Quat Conjugate(Quat q) => new(-q.X, -q.Y, -q.Z, q.W);
    public static Quat Inverse(Quat q) => Conjugate(q) * (1f / Dot(q, q));
    public static float Angle(Quat q) => 2f * MathF.Acos(Clamp(q.W, -1f, 1f));
    public static Vec3 Axis(Quat q)
    {
        float s2 = 1f - q.W * q.W;
        return s2 <= 0f ? Vec3.UnitZ : q.XYZ * (1f / MathF.Sqrt(s2));
    }
    /// <summary>(pitch, yaw, roll) in radians.</summary>
    public static Vec3 EulerAngles(Quat q)
    {
        float py = 2f * (q.Y * q.Z + q.W * q.X), px = q.W * q.W - q.X * q.X - q.Y * q.Y + q.Z * q.Z;
        float pitch = Abs(px) < 1e-7f && Abs(py) < 1e-7f ? 2f * MathF.Atan2(q.X, q.W) : MathF.Atan2(py, px);
        float yaw = MathF.Asin(Clamp(-2f * (q.X * q.Z - q.W * q.Y), -1f, 1f));
        float roll = MathF.Atan2(2f * (q.X * q.Y + q.W * q.Z), q.W * q.W + q.X * q.X - q.Y * q.Y - q.Z * q.Z);
        return new Vec3(pitch, yaw, roll);
    }
    public static Mat3 ToMat3(Quat q)
    {
        float xx = q.X * q.X, yy = q.Y * q.Y, zz = q.Z * q.Z, xz = q.X * q.Z, xy = q.X * q.Y, yz = q.Y * q.Z;
        float wx = q.W * q.X, wy = q.W * q.Y, wz = q.W * q.Z;
        return new Mat3(new Vec3(1f - 2f * (yy + zz), 2f * (xy + wz), 2f * (xz - wy)),
                        new Vec3(2f * (xy - wz), 1f - 2f * (xx + zz), 2f * (yz + wx)),
                        new Vec3(2f * (xz + wy), 2f * (yz - wx), 1f - 2f * (xx + yy)));
    }
    public static Mat4 ToMat4(Quat q) => new(ToMat3(q));
    public static Quat Nlerp(Quat a, Quat b, float t)
    {
        if (Dot(a, b) < 0f) b = -b;
        return Normalize(a + (b - a) * t);
    }
    public static Quat Slerp(Quat a, Quat b, float t)
    {
        float cosTheta = Dot(a, b);
        if (cosTheta < 0f) { b = -b; cosTheta = -cosTheta; }
        if (cosTheta > 1f - 1e-6f) return Nlerp(a, b, t);
        float angle = MathF.Acos(cosTheta);
        return (a * MathF.Sin((1f - t) * angle) + b * MathF.Sin(t * angle)) * (1f / MathF.Sin(angle));
    }
    public static Quat RotateTowards(Quat from, Quat to, float maxRadians)
    {
        float angle = 2f * MathF.Acos(Clamp(Abs(Dot(from, to)), 0f, 1f));
        return angle <= maxRadians || angle == 0f ? to : Slerp(from, to, maxRadians / angle);
    }
    /// <summary>Rotation about X, as EulerAngles(q).X.</summary>
    public static float Pitch(Quat q)
    {
        float py = 2f * (q.Y * q.Z + q.W * q.X), px = q.W * q.W - q.X * q.X - q.Y * q.Y + q.Z * q.Z;
        return Abs(px) < 1e-7f && Abs(py) < 1e-7f ? 2f * MathF.Atan2(q.X, q.W) : MathF.Atan2(py, px);
    }
    /// <summary>Rotation about Y.</summary>
    public static float Yaw(Quat q) => MathF.Asin(Clamp(-2f * (q.X * q.Z - q.W * q.Y), -1f, 1f));
    /// <summary>Rotation about Z.</summary>
    public static float Roll(Quat q) => MathF.Atan2(2f * (q.X * q.Y + q.W * q.Z), q.W * q.W + q.X * q.X - q.Y * q.Y - q.Z * q.Z);
    /// <summary>q followed by <paramref name="angle"/> radians about <paramref name="axis"/> in q's frame: q * AngleAxis(angle, axis).</summary>
    public static Quat Rotate(Quat q, float angle, Vec3 axis) => q * Quat.AngleAxis(angle, axis);
    /// <summary><paramref name="v"/> turned <paramref name="angle"/> radians about <paramref name="axis"/>.</summary>
    public static Vec3 Rotate(Vec3 v, float angle, Vec3 axis) => Quat.AngleAxis(angle, axis) * v;
    /// <summary>Component-wise, not normalised: Slerp or Nlerp for rotations.</summary>
    public static Quat Lerp(Quat a, Quat b, float t) => a + (b - a) * t;
    /// <summary>glm::mix of quaternions: Slerp.</summary>
    public static Quat Mix(Quat a, Quat b, float t) => Slerp(a, b, t);
    public static bool ApproxEqual(Quat a, Quat b, float epsilon = Epsilon) =>
        ApproxEqual(a.X, b.X, epsilon) && ApproxEqual(a.Y, b.Y, epsilon) && ApproxEqual(a.Z, b.Z, epsilon) && ApproxEqual(a.W, b.W, epsilon);
    /// <summary>The same rotation, whichever of q and -q was given.</summary>
    public static bool SameRotation(Quat a, Quat b, float epsilon = Epsilon) => ApproxEqual(a, b, epsilon) || ApproxEqual(a, -b, epsilon);

    // ---- transforms and cameras (kmath/transform.h) --------------------------------------------------

    public static Mat4 Translation(Vec3 by) { var m = Mat4.Identity; m.C3 = new Vec4(by, 1f); return m; }
    public static Mat4 Scaling(Vec3 by) { var m = Mat4.Identity; m[0, 0] = by.X; m[1, 1] = by.Y; m[2, 2] = by.Z; return m; }
    public static Mat4 Rotation(float angle, Vec3 axis) => ToMat4(Quat.AngleAxis(angle, axis));
    public static Mat4 Rotation(Quat q) => ToMat4(q);
    /// <summary>Translation * Rotation * Scale: scale first, then rotate, then move.</summary>
    public static Mat4 Compose(Vec3 translation, Quat rotation, Vec3 scale)
    {
        Mat3 r = ToMat3(rotation);
        return new Mat4(new Vec4(r.C0 * scale.X, 0f), new Vec4(r.C1 * scale.Y, 0f), new Vec4(r.C2 * scale.Z, 0f), new Vec4(translation, 1f));
    }
    /// <summary>Splits an affine matrix back into what Compose took; false for a degenerate one.</summary>
    public static bool Decompose(Mat4 m, out Vec3 translation, out Quat rotation, out Vec3 scale)
    {
        translation = (Vec3)m.C3;
        Vec3 c0 = (Vec3)m.C0, c1 = (Vec3)m.C1, c2 = (Vec3)m.C2;
        scale = new Vec3(Length(c0), Length(c1), Length(c2));
        rotation = Quat.Identity;
        if (scale.X == 0f || scale.Y == 0f || scale.Z == 0f) return false;
        if (Dot(c0, Cross(c1, c2)) < 0f) scale.X = -scale.X;
        rotation = Normalize(Quat.FromMatrix(new Mat3(c0 / scale.X, c1 / scale.Y, c2 / scale.Z)));
        return true;
    }
    /// <summary>m * Translation(by).</summary>
    public static Mat4 Translate(Mat4 m, Vec3 by) { var r = m; r.C3 = m.C0 * by.X + m.C1 * by.Y + m.C2 * by.Z + m.C3; return r; }
    /// <summary>m * Rotation(angle, axis).</summary>
    public static Mat4 Rotate(Mat4 m, float angle, Vec3 axis) => m * Rotation(angle, axis);
    /// <summary>m * Scaling(by).</summary>
    public static Mat4 Scale(Mat4 m, Vec3 by) => new(m.C0 * by.X, m.C1 * by.Y, m.C2 * by.Z, m.C3);

    /// <summary>A right-handed view matrix: the camera at <paramref name="eye"/> looking at <paramref name="target"/> (down -Z).</summary>
    public static Mat4 LookAt(Vec3 eye, Vec3 target, Vec3? up = null)
    {
        Vec3 f = Normalize(target - eye), s = Normalize(Cross(f, up ?? Vec3.Up)), u = Cross(s, f);
        return new Mat4(new Vec4(s.X, u.X, -f.X, 0f), new Vec4(s.Y, u.Y, -f.Y, 0f), new Vec4(s.Z, u.Z, -f.Z, 0f),
                        new Vec4(-Dot(s, eye), -Dot(u, eye), Dot(f, eye), 1f));
    }
    /// <summary>LookAt by its right-handed name.</summary>
    public static Mat4 LookAtRH(Vec3 eye, Vec3 target, Vec3? up = null) => LookAt(eye, target, up);
    /// <summary>A left-handed view matrix: the camera looks down +Z.</summary>
    public static Mat4 LookAtLH(Vec3 eye, Vec3 target, Vec3? up = null)
    {
        Vec3 f = Normalize(target - eye), s = Normalize(Cross(up ?? Vec3.Up, f)), u = Cross(f, s);
        return new Mat4(new Vec4(s.X, u.X, f.X, 0f), new Vec4(s.Y, u.Y, f.Y, 0f), new Vec4(s.Z, u.Z, f.Z, 0f),
                        new Vec4(-Dot(s, eye), -Dot(u, eye), -Dot(f, eye), 1f));
    }

    static bool LeftHanded(ClipSpace c) => c is ClipSpace.LeftHandedZeroToOne or ClipSpace.LeftHandedNegativeOneToOne;
    static bool ZeroToOne(ClipSpace c) => c is ClipSpace.RightHandedZeroToOne or ClipSpace.LeftHandedZeroToOne;
    static void PerspectiveDepth(ref Mat4 m, float near, float far, ClipSpace clip)
    {
        m[2, 3] = LeftHanded(clip) ? 1f : -1f;
        if (ZeroToOne(clip))
        {
            m[2, 2] = LeftHanded(clip) ? far / (far - near) : far / (near - far);
            m[3, 2] = -(far * near) / (far - near);
        }
        else
        {
            m[2, 2] = LeftHanded(clip) ? (far + near) / (far - near) : -(far + near) / (far - near);
            m[3, 2] = -(2f * far * near) / (far - near);
        }
    }
    /// <summary>Perspective projection, vertical field of view in radians; by default depth 0 at near to 1 at far. Y is not flipped.</summary>
    public static Mat4 Perspective(float fovY, float aspect, float near, float far, ClipSpace clip = ClipSpace.RightHandedZeroToOne)
    {
        float f = 1f / MathF.Tan(fovY * 0.5f);
        var m = Mat4.Zero;
        m[0, 0] = f / aspect;
        m[1, 1] = f;
        PerspectiveDepth(ref m, near, far, clip);
        return m;
    }
    /// <summary>Perspective from a field of view and the viewport's size in pixels.</summary>
    public static Mat4 PerspectiveFov(float fov, float width, float height, float near, float far, ClipSpace clip = ClipSpace.RightHandedZeroToOne)
    {
        float h = MathF.Cos(0.5f * fov) / MathF.Sin(0.5f * fov);
        var m = Mat4.Zero;
        m[0, 0] = h * height / width;
        m[1, 1] = h;
        PerspectiveDepth(ref m, near, far, clip);
        return m;
    }
    /// <summary>Perspective with no far plane.</summary>
    public static Mat4 InfinitePerspective(float fovY, float aspect, float near, ClipSpace clip = ClipSpace.RightHandedZeroToOne)
    {
        float range = MathF.Tan(fovY * 0.5f) * near;
        var m = Mat4.Zero;
        m[0, 0] = (2f * near) / (range * aspect * 2f);
        m[1, 1] = (2f * near) / (range * 2f);
        m[2, 2] = m[2, 3] = LeftHanded(clip) ? 1f : -1f;
        m[3, 2] = ZeroToOne(clip) ? -near : -2f * near;
        return m;
    }
    /// <summary>An off-centre perspective (glm::frustum): the view volume of the near-plane rectangle.</summary>
    public static Mat4 FrustumProjection(float left, float right, float bottom, float top, float near, float far, ClipSpace clip = ClipSpace.RightHandedZeroToOne)
    {
        var m = Mat4.Zero;
        m[0, 0] = (2f * near) / (right - left);
        m[1, 1] = (2f * near) / (top - bottom);
        float side = LeftHanded(clip) ? -1f : 1f;
        m[2, 0] = side * (right + left) / (right - left);
        m[2, 1] = side * (top + bottom) / (top - bottom);
        PerspectiveDepth(ref m, near, far, clip);
        return m;
    }
    /// <summary>Reversed depth (1 at near, 0 at far); an infinite <paramref name="far"/> for no far plane.</summary>
    public static Mat4 PerspectiveReversedZ(float fovY, float aspect, float near, float far = float.PositiveInfinity)
    {
        float f = 1f / MathF.Tan(fovY * 0.5f);
        var m = Mat4.Zero;
        m[0, 0] = f / aspect;
        m[1, 1] = f;
        m[2, 3] = -1f;
        if (float.IsInfinity(far))
        {
            m[3, 2] = near;
        }
        else
        {
            m[2, 2] = near / (far - near);
            m[3, 2] = far * near / (far - near);
        }
        return m;
    }
    /// <summary>Orthographic projection of the box [left, right] x [bottom, top] x [near, far] in front of the camera.</summary>
    public static Mat4 Orthographic(float left, float right, float bottom, float top, float near, float far, ClipSpace clip = ClipSpace.RightHandedZeroToOne)
    {
        var m = Mat4.Identity;
        m[0, 0] = 2f / (right - left);
        m[1, 1] = 2f / (top - bottom);
        m[3, 0] = -(right + left) / (right - left);
        m[3, 1] = -(top + bottom) / (top - bottom);
        float sign = LeftHanded(clip) ? 1f : -1f;
        if (ZeroToOne(clip))
        {
            m[2, 2] = sign / (far - near);
            m[3, 2] = -near / (far - near);
        }
        else
        {
            m[2, 2] = sign * 2f / (far - near);
            m[3, 2] = -(far + near) / (far - near);
        }
        return m;
    }
    /// <summary>A 2D orthographic projection: x and y only (glm's four-argument ortho).</summary>
    public static Mat4 Orthographic(float left, float right, float bottom, float top)
    {
        var m = Mat4.Identity;
        m[0, 0] = 2f / (right - left);
        m[1, 1] = 2f / (top - bottom);
        m[2, 2] = -1f;
        m[3, 0] = -(right + left) / (right - left);
        m[3, 1] = -(top + bottom) / (top - bottom);
        return m;
    }
    /// <summary>Where <paramref name="obj"/> lands in window coordinates; <paramref name="viewport"/> is (x, y, width, height).</summary>
    public static Vec3 Project(Vec3 obj, Mat4 model, Mat4 projection, Vec4 viewport, ClipSpace clip = ClipSpace.RightHandedZeroToOne)
    {
        Vec4 v = projection * (model * new Vec4(obj, 1f));
        v /= v.W;
        if (ZeroToOne(clip))
        {
            v.X = v.X * 0.5f + 0.5f;
            v.Y = v.Y * 0.5f + 0.5f;
        }
        else
        {
            v = v * 0.5f + 0.5f;
        }
        return new Vec3(v.X * viewport.Z + viewport.X, v.Y * viewport.W + viewport.Y, v.Z);
    }
    /// <summary>The inverse of Project: the point in object space under window coordinates <paramref name="window"/>.</summary>
    public static Vec3 UnProject(Vec3 window, Mat4 model, Mat4 projection, Vec4 viewport, ClipSpace clip = ClipSpace.RightHandedZeroToOne)
    {
        Mat4 inverse = Inverse(projection * model);
        var v = new Vec4(window, 1f);
        v.X = (v.X - viewport.X) / viewport.Z;
        v.Y = (v.Y - viewport.Y) / viewport.W;
        if (ZeroToOne(clip))
        {
            v.X = v.X * 2f - 1f;
            v.Y = v.Y * 2f - 1f;
        }
        else
        {
            v = v * 2f - 1f;
        }
        Vec4 o = inverse * v;
        o /= o.W;
        return (Vec3)o;
    }
    /// <summary>A matrix that narrows a projection to the <paramref name="size"/>-pixel region around <paramref name="center"/>: for picking.</summary>
    public static Mat4 PickMatrix(Vec2 center, Vec2 size, Vec4 viewport)
    {
        var m = Mat4.Identity;
        if (!(size.X > 0f && size.Y > 0f)) return m;
        var t = new Vec3((viewport.Z - 2f * (center.X - viewport.X)) / size.X, (viewport.W - 2f * (center.Y - viewport.Y)) / size.Y, 0f);
        return Scale(Translate(m, t), new Vec3(viewport.Z / size.X, viewport.W / size.Y, 1f));
    }

    // ---- Euler angles (kmath/euler.h, glm's gtx/euler_angle) ---------------------------------------

    static Mat4 AxisRotation(int axis, float angle)
    {
        float c = MathF.Cos(angle), s = MathF.Sin(angle);
        var m = Mat4.Identity;
        int u = (axis + 1) % 3, v = (axis + 2) % 3;
        m[u, u] = c;
        m[u, v] = s;
        m[v, u] = -s;
        m[v, v] = c;
        return m;
    }
    static readonly int[][] EulerAxes = [[0, 1, 2], [0, 2, 1], [1, 0, 2], [1, 2, 0], [2, 0, 1], [2, 1, 0],
                                         [0, 1, 0], [0, 2, 0], [1, 0, 1], [1, 2, 1], [2, 0, 2], [2, 1, 2]];
    public static Mat4 EulerAngleX(float angle) => AxisRotation(0, angle);
    public static Mat4 EulerAngleY(float angle) => AxisRotation(1, angle);
    public static Mat4 EulerAngleZ(float angle) => AxisRotation(2, angle);
    /// <summary>The rotation <paramref name="angles"/> = (first, second, third) describe in <paramref name="order"/>, outermost first.</summary>
    public static Mat4 EulerAngles(EulerOrder order, Vec3 angles)
    {
        int[] a = EulerAxes[(int)order];
        return AxisRotation(a[0], angles.X) * AxisRotation(a[1], angles.Y) * AxisRotation(a[2], angles.Z);
    }
    /// <summary>The angles that rebuild <paramref name="m"/>'s rotation in <paramref name="order"/>; at gimbal lock the third is 0.</summary>
    public static Vec3 ExtractEulerAngles(EulerOrder order, Mat4 m)
    {
        int[] a = EulerAxes[(int)order];
        float At(int row, int column) => m[column, row];
        const float lockLimit = 1e-6f;
        if (a[0] != a[2])
        {
            int i = a[0], j = a[1], k = a[2];
            float e = (j - i + 3) % 3 == 1 ? 1f : -1f;
            float cb = MathF.Sqrt(At(i, i) * At(i, i) + At(i, j) * At(i, j));
            float b = MathF.Atan2(e * At(i, k), cb);
            if (cb < lockLimit) return new Vec3(MathF.Atan2(e * At(k, j), At(j, j)), b, 0f);
            return new Vec3(MathF.Atan2(-e * At(j, k), At(k, k)), b, MathF.Atan2(-e * At(i, j), At(i, i)));
        }
        {
            int i = a[0], j = a[1], k = 3 - i - j;
            float e = (j - i + 3) % 3 == 1 ? 1f : -1f;
            float sb = MathF.Sqrt(At(i, j) * At(i, j) + At(i, k) * At(i, k));
            float b = MathF.Atan2(sb, At(i, i));
            if (sb < lockLimit) return new Vec3(MathF.Atan2(e * At(k, j), At(j, j)), b, 0f);
            return new Vec3(MathF.Atan2(At(j, i), -e * At(k, i)), b, MathF.Atan2(At(i, j), e * At(i, k)));
        }
    }
    /// <summary>glm::yawPitchRoll: Y(yaw) * X(pitch) * Z(roll).</summary>
    public static Mat4 YawPitchRoll(float yaw, float pitch, float roll) => EulerAngles(EulerOrder.YXZ, new Vec3(yaw, pitch, roll));
    /// <summary>The quaternion of EulerAngles(order, angles).</summary>
    public static Quat QuatFromEuler(EulerOrder order, Vec3 angles)
    {
        int[] a = EulerAxes[(int)order];
        static Vec3 Axis(int i) => i == 0 ? Vec3.UnitX : i == 1 ? Vec3.UnitY : Vec3.UnitZ;
        return Quat.AngleAxis(angles.X, Axis(a[0])) * Quat.AngleAxis(angles.Y, Axis(a[1])) * Quat.AngleAxis(angles.Z, Axis(a[2]));
    }

    /// <summary>m * (p, 1), without the projective divide.</summary>
    public static Vec3 TransformPoint(Mat4 m, Vec3 p) => (Vec3)m.C0 * p.X + (Vec3)m.C1 * p.Y + (Vec3)m.C2 * p.Z + (Vec3)m.C3;
    public static Vec3 TransformPointProjective(Mat4 m, Vec3 p) { Vec4 h = m * new Vec4(p, 1f); return (Vec3)h / h.W; }
    public static Vec3 TransformDirection(Mat4 m, Vec3 d) => (Vec3)m.C0 * d.X + (Vec3)m.C1 * d.Y + (Vec3)m.C2 * d.Z;
}
