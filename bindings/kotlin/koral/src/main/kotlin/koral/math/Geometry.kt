package koral

import kotlin.math.abs as kabs
import kotlin.math.sqrt as ksqrt

// kmath/geometry.h: shapes and the questions asked of them. Hits are distances along the ray (in units of its
// direction's length); a ray starting inside a solid shape hits it at 0.

/** kor::Ray. */
data class Ray(val origin: Vec3 = Vec3.Zero, val direction: Vec3 = Vec3.Forward) {
    fun at(distance: Float) = origin + direction * distance
    companion object {
        /** From [from] toward [to], the direction normalised. */
        fun between(from: Vec3, to: Vec3) = Ray(from, normalize(to - from))
    }
}

/** kor::Plane: the points p with dot(normal, p) + distance == 0. */
data class Plane(val normal: Vec3 = Vec3.Up, val distance: Float = 0f) {
    fun signedDistance(p: Vec3) = dot(normal, p) + distance
    fun normalized(): Plane { val len = length(normal); return if (len > 0f) Plane(normal / len, distance / len) else this }
    companion object {
        fun fromPointNormal(point: Vec3, normal: Vec3): Plane { val n = normalize(normal); return Plane(n, -dot(n, point)) }
        /** Through three points; counter-clockwise a → b → c faces the normal. */
        fun fromPoints(a: Vec3, b: Vec3, c: Vec3) = fromPointNormal(a, cross(b - a, c - a))
    }
}

/** kor::Sphere. */
data class Sphere(val center: Vec3 = Vec3.Zero, val radius: Float = 0f) {
    fun contains(p: Vec3) = distance2(center, p) <= radius * radius
    companion object {
        /** A sphere around all the points (Ritter's). */
        fun fromPoints(points: List<Vec3>): Sphere {
            if (points.isEmpty()) return Sphere()
            fun farthest(from: Vec3): Vec3 {
                var best = points[0]
                var bestSq = -1f
                for (p in points) { val sq = distance2(from, p); if (sq > bestSq) { bestSq = sq; best = p } }
                return best
            }
            val a = farthest(points[0])
            val b = farthest(a)
            var center = (a + b) * 0.5f
            var radius = distance(a, b) * 0.5f
            for (p in points) {
                val d = distance(center, p)
                if (d > radius) {
                    val r = (radius + d) * 0.5f
                    center += (p - center) * ((r - radius) / d)
                    radius = r
                }
            }
            return Sphere(center, radius)
        }
    }
}

/** kor::Aabb. `Aabb()` is empty (min +inf, max -inf): what expand grows from. */
data class Aabb(val min: Vec3 = Vec3(Float.POSITIVE_INFINITY), val max: Vec3 = Vec3(Float.NEGATIVE_INFINITY)) {
    val valid: Boolean get() = min.x <= max.x && min.y <= max.y && min.z <= max.z
    val center: Vec3 get() = (min + max) * 0.5f
    val size: Vec3 get() = max - min
    val halfExtents: Vec3 get() = (max - min) * 0.5f
    val volume: Float get() = if (valid) compMul(size) else 0f
    val surfaceArea: Float get() = if (!valid) 0f else size.let { 2f * (it.x * it.y + it.y * it.z + it.z * it.x) }
    /** Corner [i] of 8: bit 0 picks max.x, bit 1 max.y, bit 2 max.z. */
    fun corner(i: Int) = Vec3(if (i and 1 != 0) max.x else min.x, if (i and 2 != 0) max.y else min.y, if (i and 4 != 0) max.z else min.z)
    fun contains(p: Vec3) = p.x >= min.x && p.y >= min.y && p.z >= min.z && p.x <= max.x && p.y <= max.y && p.z <= max.z
    fun contains(b: Aabb) = contains(b.min) && contains(b.max)
    /** This box grown to take in [p]. */
    fun expand(p: Vec3) = Aabb(min(min, p), max(max, p))
    fun expand(b: Aabb) = Aabb(min(min, b.min), max(max, b.max))
    fun inflated(amount: Float) = Aabb(min - Vec3(amount), max + Vec3(amount))
    /** The box around this box after [m] (Arvo's method). */
    fun transformed(m: Mat4): Aabb {
        if (!valid) return this
        var lo = Vec3(m[3])
        var hi = lo
        for (c in 0 until 3) {
            val col = Vec3(m[c])
            val a = col * min[c]
            val b = col * max[c]
            lo += min(a, b)
            hi += max(a, b)
        }
        return Aabb(lo, hi)
    }
    companion object {
        val Empty = Aabb()
        fun fromCenterExtents(center: Vec3, halfExtents: Vec3) = Aabb(center - halfExtents, center + halfExtents)
        fun fromPoints(points: List<Vec3>) = points.fold(Empty) { box, p -> box.expand(p) }
    }
}

/** kor::Obb: a box of halfExtents around center, turned by rotation. */
data class Obb(val center: Vec3 = Vec3.Zero, val halfExtents: Vec3 = Vec3(0.5f), val rotation: Quat = Quat.Identity) {
    fun bounds(): Aabb {
        val r = toMat3(rotation)
        val e = Vec3(kabs(r[0, 0]) * halfExtents.x + kabs(r[1, 0]) * halfExtents.y + kabs(r[2, 0]) * halfExtents.z,
                     kabs(r[0, 1]) * halfExtents.x + kabs(r[1, 1]) * halfExtents.y + kabs(r[2, 1]) * halfExtents.z,
                     kabs(r[0, 2]) * halfExtents.x + kabs(r[1, 2]) * halfExtents.y + kabs(r[2, 2]) * halfExtents.z)
        return Aabb(center - e, center + e)
    }
    fun contains(p: Vec3): Boolean {
        val local = abs(conjugate(rotation) * (p - center))
        return local.x <= halfExtents.x && local.y <= halfExtents.y && local.z <= halfExtents.z
    }
    fun corner(i: Int) = center + rotation * Vec3(if (i and 1 != 0) halfExtents.x else -halfExtents.x,
        if (i and 2 != 0) halfExtents.y else -halfExtents.y, if (i and 4 != 0) halfExtents.z else -halfExtents.z)
    companion object {
        /** The box [local] placed by [m] (scale into the extents, shear lost). */
        fun fromAabb(local: Aabb, m: Mat4): Obb {
            val t = decompose(m) ?: return Obb(transformPoint(m, local.center), Vec3.Zero, Quat.Identity)
            return Obb(transformPoint(m, local.center), abs(local.halfExtents * t.scale), t.rotation)
        }
    }
}

/** kor::Triangle. */
data class Triangle(val a: Vec3, val b: Vec3, val c: Vec3) {
    val normal: Vec3 get() = normalize(cross(b - a, c - a))
    val area: Float get() = 0.5f * length(cross(b - a, c - a))
    val centroid: Vec3 get() = (a + b + c) * (1f / 3f)
    /** (u, v, w) with p = u*a + v*b + w*c. */
    fun barycentric(p: Vec3): Vec3 {
        val v0 = b - a; val v1 = c - a; val v2 = p - a
        val d00 = dot(v0, v0); val d01 = dot(v0, v1); val d11 = dot(v1, v1)
        val d20 = dot(v2, v0); val d21 = dot(v2, v1)
        val denom = d00 * d11 - d01 * d01
        if (denom == 0f) return Vec3(1f, 0f, 0f)
        val v = (d11 * d20 - d01 * d21) / denom
        val w = (d00 * d21 - d01 * d20) / denom
        return Vec3(1f - v - w, v, w)
    }
}

/** kor::Aabb2: Aabb in 2D, an axis-aligned rectangle by its corners. `Aabb2()` is empty. */
data class Aabb2(val min: Vec2 = Vec2(Float.POSITIVE_INFINITY), val max: Vec2 = Vec2(Float.NEGATIVE_INFINITY)) {
    val valid: Boolean get() = min.x <= max.x && min.y <= max.y
    val size: Vec2 get() = max - min
    val center: Vec2 get() = (min + max) * 0.5f
    val area: Float get() = if (valid) size.x * size.y else 0f
    fun contains(p: Vec2) = p.x >= min.x && p.y >= min.y && p.x <= max.x && p.y <= max.y
    fun expand(p: Vec2) = Aabb2(min(min, p), max(max, p))
    fun inflated(amount: Float) = Aabb2(min - Vec2(amount), max + Vec2(amount))
    companion object {
        val Empty = Aabb2()
        fun fromPositionSize(position: Vec2, size: Vec2) = Aabb2(position, position + size)
    }
}

/** kor::Containment. */
enum class Containment { Outside, Intersects, Inside }

/** kor::TriangleHit: u and v are the barycentric weights of b and c (a's is 1 - u - v). */
data class TriangleHit(val distance: Float, val u: Float, val v: Float)

/** kor::Frustum: six inward-facing planes — left, right, bottom, top, near, far. */
data class Frustum(val planes: List<Plane>) {
    init { require(planes.size == 6) { "a frustum has 6 planes" } }

    fun contains(p: Vec3) = planes.all { it.signedDistance(p) >= 0f }
    fun classify(box: Aabb): Containment {
        if (!box.valid) return Containment.Outside
        val center = box.center
        val extent = box.halfExtents
        var result = Containment.Inside
        for (plane in planes) {
            val d = plane.signedDistance(center)
            val r = dot(extent, abs(plane.normal))
            if (d < -r) return Containment.Outside
            if (d < r) result = Containment.Intersects
        }
        return result
    }
    fun classify(sphere: Sphere): Containment {
        var result = Containment.Inside
        for (plane in planes) {
            val d = plane.signedDistance(sphere.center)
            if (d < -sphere.radius) return Containment.Outside
            if (d < sphere.radius) result = Containment.Intersects
        }
        return result
    }

    companion object {
        /** From a projection * view matrix with Vulkan's 0..1 depth. */
        fun fromMatrix(m: Mat4): Frustum {
            val r0 = m.row(0); val r1 = m.row(1); val r2 = m.row(2); val r3 = m.row(3)
            return Frustum(listOf(r3 + r0, r3 - r0, r3 + r1, r3 - r1, r2, r3 - r2).map { Plane(it.xyz, it.w).normalized() })
        }
        /** The 8 world-space corners (Aabb.corner's numbering, z near/far). */
        fun corners(viewProjection: Mat4): List<Vec3> {
            val inv = inverse(viewProjection)
            return (0 until 8).map { i ->
                transformPointProjective(inv, Vec3(if (i and 1 != 0) 1f else -1f, if (i and 2 != 0) 1f else -1f, if (i and 4 != 0) 1f else 0f))
            }
        }
    }
}

fun union(a: Aabb, b: Aabb) = Aabb(min(a.min, b.min), max(a.max, b.max))
fun intersection(a: Aabb, b: Aabb) = Aabb(max(a.min, b.min), min(a.max, b.max))

fun raycast(ray: Ray, plane: Plane, maxDistance: Float = Float.POSITIVE_INFINITY): Float? {
    val denom = dot(plane.normal, ray.direction)
    if (kabs(denom) < 1e-8f) return null
    val t = -plane.signedDistance(ray.origin) / denom
    return if (t < 0f || t > maxDistance) null else t
}
fun raycast(ray: Ray, sphere: Sphere, maxDistance: Float = Float.POSITIVE_INFINITY): Float? {
    val m = ray.origin - sphere.center
    val a = dot(ray.direction, ray.direction)
    val b = dot(m, ray.direction)
    val c = dot(m, m) - sphere.radius * sphere.radius
    if (c <= 0f) return 0f
    if (b > 0f || a == 0f) return null
    val disc = b * b - a * c
    if (disc < 0f) return null
    val t = (-b - ksqrt(disc)) / a
    return if (t > maxDistance) null else t
}
fun raycast(ray: Ray, box: Aabb, maxDistance: Float = Float.POSITIVE_INFINITY): Float? {
    var tMin = 0f
    var tMax = maxDistance
    for (i in 0 until 3) {
        if (kabs(ray.direction[i]) < 1e-12f) {
            if (ray.origin[i] < box.min[i] || ray.origin[i] > box.max[i]) return null
            continue
        }
        val inv = 1f / ray.direction[i]
        var t0 = (box.min[i] - ray.origin[i]) * inv
        var t1 = (box.max[i] - ray.origin[i]) * inv
        if (t0 > t1) { val s = t0; t0 = t1; t1 = s }
        tMin = if (tMin < t0) t0 else tMin
        tMax = if (t1 < tMax) t1 else tMax
        if (tMin > tMax) return null
    }
    return tMin
}
fun raycast(ray: Ray, box: Obb, maxDistance: Float = Float.POSITIVE_INFINITY): Float? {
    val inv = conjugate(box.rotation)
    return raycast(Ray(inv * (ray.origin - box.center), inv * ray.direction), Aabb(-box.halfExtents, box.halfExtents), maxDistance)
}
/** Möller–Trumbore; both faces unless [cullBackFaces]. */
fun raycast(ray: Ray, tri: Triangle, maxDistance: Float = Float.POSITIVE_INFINITY, cullBackFaces: Boolean = false): TriangleHit? {
    val eps = 1e-8f
    val e1 = tri.b - tri.a
    val e2 = tri.c - tri.a
    val p = cross(ray.direction, e2)
    val det = dot(e1, p)
    if (if (cullBackFaces) det < eps else kabs(det) < eps) return null
    val invDet = 1f / det
    val s = ray.origin - tri.a
    val u = dot(s, p) * invDet
    if (u < 0f || u > 1f) return null
    val q = cross(s, e1)
    val v = dot(ray.direction, q) * invDet
    if (v < 0f || u + v > 1f) return null
    val t = dot(e2, q) * invDet
    return if (t < 0f || t > maxDistance) null else TriangleHit(t, u, v)
}

fun overlaps(a: Aabb, b: Aabb) = a.min.x <= b.max.x && a.min.y <= b.max.y && a.min.z <= b.max.z && b.min.x <= a.max.x && b.min.y <= a.max.y && b.min.z <= a.max.z
fun overlaps(a: Aabb2, b: Aabb2) = a.min.x <= b.max.x && b.min.x <= a.max.x && a.min.y <= b.max.y && b.min.y <= a.max.y
fun overlaps(a: Sphere, b: Sphere): Boolean { val r = a.radius + b.radius; return distance2(a.center, b.center) <= r * r }
fun overlaps(box: Aabb, sphere: Sphere) = distance2(closestPoint(box, sphere.center), sphere.center) <= sphere.radius * sphere.radius
fun overlaps(sphere: Sphere, box: Aabb) = overlaps(box, sphere)
fun overlaps(frustum: Frustum, box: Aabb) = frustum.classify(box) != Containment.Outside
fun overlaps(frustum: Frustum, sphere: Sphere) = frustum.classify(sphere) != Containment.Outside
/** Separating-axis test over the 15 candidate axes. */
fun overlaps(a: Obb, b: Obb): Boolean {
    val ra = toMat3(a.rotation)
    val rb = toMat3(b.rotation)
    val r = FloatArray(9)
    val absR = FloatArray(9)
    for (i in 0 until 3) for (j in 0 until 3) {
        r[i * 3 + j] = dot(ra[i], rb[j])
        absR[i * 3 + j] = kabs(r[i * 3 + j]) + 1e-6f
    }
    val d = b.center - a.center
    val t = floatArrayOf(dot(d, ra[0]), dot(d, ra[1]), dot(d, ra[2]))
    val ea = a.halfExtents.toArray()
    val eb = b.halfExtents.toArray()
    for (i in 0 until 3)
        if (kabs(t[i]) > ea[i] + eb[0] * absR[i * 3] + eb[1] * absR[i * 3 + 1] + eb[2] * absR[i * 3 + 2]) return false
    for (j in 0 until 3)
        if (kabs(t[0] * r[j] + t[1] * r[3 + j] + t[2] * r[6 + j]) > ea[0] * absR[j] + ea[1] * absR[3 + j] + ea[2] * absR[6 + j] + eb[j]) return false
    for (i in 0 until 3) {
        val i1 = (i + 1) % 3
        val i2 = (i + 2) % 3
        for (j in 0 until 3) {
            val j1 = (j + 1) % 3
            val j2 = (j + 2) % 3
            val rA = ea[i1] * absR[i2 * 3 + j] + ea[i2] * absR[i1 * 3 + j]
            val rB = eb[j1] * absR[i * 3 + j2] + eb[j2] * absR[i * 3 + j1]
            if (kabs(t[i2] * r[i1 * 3 + j] - t[i1] * r[i2 * 3 + j]) > rA + rB) return false
        }
    }
    return true
}

fun closestPoint(box: Aabb, p: Vec3) = clamp(p, box.min, box.max)
fun closestPoint(plane: Plane, p: Vec3) = p - plane.normal * plane.signedDistance(p)
fun closestPoint(sphere: Sphere, p: Vec3): Vec3 {
    val d = p - sphere.center
    val sq = dot(d, d)
    return if (sq <= sphere.radius * sphere.radius) p else sphere.center + d * (sphere.radius / ksqrt(sq))
}
fun closestPoint(box: Obb, p: Vec3): Vec3 {
    val local = conjugate(box.rotation) * (p - box.center)
    return box.center + box.rotation * clamp(local, -box.halfExtents, box.halfExtents)
}
fun closestPoint(tri: Triangle, p: Vec3): Vec3 {
    val a = tri.a; val b = tri.b; val c = tri.c
    val ab = b - a; val ac = c - a; val ap = p - a
    val d1 = dot(ab, ap); val d2 = dot(ac, ap)
    if (d1 <= 0f && d2 <= 0f) return a
    val bp = p - b
    val d3 = dot(ab, bp); val d4 = dot(ac, bp)
    if (d3 >= 0f && d4 <= d3) return b
    val vc = d1 * d4 - d3 * d2
    if (vc <= 0f && d1 >= 0f && d3 <= 0f) return a + ab * (d1 / (d1 - d3))
    val cp = p - c
    val d5 = dot(ab, cp); val d6 = dot(ac, cp)
    if (d6 >= 0f && d5 <= d6) return c
    val vb = d5 * d2 - d1 * d6
    if (vb <= 0f && d2 >= 0f && d6 <= 0f) return a + ac * (d2 / (d2 - d6))
    val va = d3 * d6 - d5 * d4
    if (va <= 0f && d4 - d3 >= 0f && d5 - d6 >= 0f) return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)))
    val denom = 1f / (va + vb + vc)
    return a + ab * (vb * denom) + ac * (vc * denom)
}
fun closestPointOnSegment(a: Vec3, b: Vec3, p: Vec3): Vec3 {
    val ab = b - a
    val sq = dot(ab, ab)
    return if (sq == 0f) a else a + ab * saturate(dot(p - a, ab) / sq)
}
fun distance(box: Aabb, p: Vec3) = distance(closestPoint(box, p), p)
