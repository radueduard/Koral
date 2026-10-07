package koral

/**
 * kor::Transform: where something is — position, rotation and per-axis scale, applied scale first.
 * `parent * child` places a child inside its parent (exact unless the parent's scale is non-uniform and the child
 * is rotated against it). `Transform()` is the identity.
 */
data class Transform(val position: Vec3 = Vec3.Zero, val rotation: Quat = Quat.Identity, val scale: Vec3 = Vec3.One) {
    val matrix: Mat4 get() = compose(position, rotation, scale)
    val inverseMatrix: Mat4 get() = inverse(matrix)
    val forward: Vec3 get() = rotation * Vec3.Forward
    val right: Vec3 get() = rotation * Vec3.Right
    val up: Vec3 get() = rotation * Vec3.Up

    fun transformPoint(p: Vec3) = position + rotation * (p * scale)
    fun transformDirection(d: Vec3) = rotation * (d * scale)
    fun inverseTransformPoint(p: Vec3) = (conjugate(rotation) * (p - position)) / scale
    fun inverseTransformDirection(d: Vec3) = (conjugate(rotation) * d) / scale

    /** This transform turned to look at [target]. */
    fun lookAt(target: Vec3, up: Vec3 = Vec3.Up) = copy(rotation = Quat.lookRotation(target - position, up))
    fun translate(by: Vec3) = copy(position = position + by)
    /** Rotated by [q] in world space. */
    fun rotate(q: Quat) = copy(rotation = normalize(q * rotation))
    /** The transform that undoes this one (exact for uniform scale). */
    fun inverse(): Transform {
        val inv = conjugate(rotation)
        val invScale = Vec3.One / scale
        return Transform(inv * (-position * invScale), inv, invScale)
    }

    operator fun times(child: Transform) = Transform(transformPoint(child.position), rotation * child.rotation, scale * child.scale)

    companion object {
        val Identity = Transform()
        /** From an affine matrix (shear is lost; a degenerate one gives the identity). */
        fun fromMatrix(m: Mat4) = decompose(m) ?: Identity
    }
}

/** Lerps position and scale, slerps rotation. */
fun lerp(a: Transform, b: Transform, t: Float) = Transform(lerp(a.position, b.position, t), slerp(a.rotation, b.rotation, t), lerp(a.scale, b.scale, t))
