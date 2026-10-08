namespace Koral;

/// <summary>
/// kor::ClipSpace: the conventions a projection is built for (glm's RH_ZO, RH_NO, LH_ZO, LH_NO) — which way the
/// camera looks (right-handed: down -Z) and the clip depth range (0..1 as Vulkan, -1..1 as OpenGL).
/// </summary>
public enum ClipSpace : byte { RightHandedZeroToOne, RightHandedNegativeOneToOne, LeftHandedZeroToOne, LeftHandedNegativeOneToOne }

/// <summary>kor::EulerOrder: the axes, outermost first; the first six Tait-Bryan, the last six proper Euler.</summary>
public enum EulerOrder : byte { XYZ, XZY, YXZ, YZX, ZXY, ZYX, XYX, XZX, YXY, YZY, ZXZ, ZYZ }
