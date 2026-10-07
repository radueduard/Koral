# Mathematics

`#include <kmath.h>` — or one of `kmath/*.h` for less. Everything is in `kor::`, and the same library is in
C (`koral_math_c.h`), C# (`Koral`, with the free functions in `Koral.KMath`) and Kotlin (package `koral`,
the free functions top-level), under the same names in each language's own casing.

| Header | What |
|---|---|
| `kmath/scalar.h` | `u8`…`u64`, `i8`…`i64`, `f32`, `f64`; `Pi`, `Tau`; `Radians`, `Clamp`, `Lerp`, `InverseLerp`, `Remap`, `SmoothStep`, `MoveTowards`, `SmoothDamp`, `DeltaAngle`, `AlignUp`, … |
| `kmath/vector.h` | `Vec2/3/4`, `IVec*`, `UVec*`, `DVec*`, `U8Vec4`, `BVec*`; `Dot`, `Cross`, `Length`, `Normalize`, `Reflect`, `Project`, `Angle`, `SignedAngle`, component-wise everything |
| `kmath/matrix.h` | `Mat2/3/4`, `Mat4x3`; `Transpose`, `Determinant`, `Inverse`, `NormalMatrix` |
| `kmath/quaternion.h` | `Quat`: `AngleAxis`, `FromEuler`, `LookRotation`, `FromTo`; `Slerp`, `Nlerp`, `RotateTowards`, `EulerAngles`, `ToMat4` |
| `kmath/transform.h` | `Translation`, `Rotation`, `Scaling`, `Compose`/`Decompose`, `LookAt`, `Perspective`, `PerspectiveReversedZ`, `Orthographic`, `TransformPoint`; `kor::Transform` |
| `kmath/geometry.h` | `Ray`, `Plane`, `Sphere`, `Aabb`, `Aabb2`, `Obb`, `Triangle`, `Frustum`; `Raycast`, `Overlaps`, `ClosestPoint` |
| `kmath/random.h` | `kor::Random` (PCG32), `Hash`, `HashCombine` |
| `kmath/noise.h` | `kor::Noise`: Perlin, simplex, value and cellular noise, 2D and 3D, and fractals of them |
| `kmath/interp.h` | `Ease` and the `Easing` curves; `Bezier`, `Hermite`, `CatmullRom`, `SamplePath` |
| `kmath/color.h` | sRGB ⇄ linear, HSV, HSL, Oklab, colour temperature; `PackUnorm4x8`, halves, octahedral normals |
| `kmath/bulk.h` | `kor::bulk::`: transforms, matrix products, bounds and frustum culling over spans, vectorised |

```cpp
using namespace kor;

const Mat4 view = LookAt(Vec3(0, 4, 8), Vec3(0.f));
Mat4 projection = Perspective(Radians(60.f), aspect, 0.1f, 100.f);
projection[1][1] *= -1.f;                          // Vulkan's clip space points Y down
const Frustum frustum = Frustum::FromMatrix(projection * view);

Transform crate{Vec3(1, 0, -3), Quat::AngleAxis(Radians(30.f), Vec3::Up())};
if (const auto hit = Raycast(Ray::Between(eye, target), Obb::FromAabb(local, crate.Matrix()))) { /* *hit is the distance */ }

Random random(seed);
const Noise terrain(seed);
const float height = terrain.Fractal(Noise::Kind::eSimplex, Vec2(x, z) * 0.01f, {.octaves = 6});
```

```csharp
using static Koral.KMath;

var view = LookAt(new Vec3(0, 4, 8), Vec3.Zero);
var crate = new Transform(new Vec3(1, 0, -3), Quat.AngleAxis(Radians(30f), Vec3.Up));
float? hit = Raycast(Ray.Between(eye, target), Obb.FromAabb(local, crate.Matrix));
```

```kotlin
val view = lookAt(Vec3(0f, 4f, 8f), Vec3.Zero)
val crate = Transform(Vec3(1f, 0f, -3f), Quat.angleAxis(radians(30f), Vec3.Up))
val hit: Float? = raycast(Ray.between(eye, target), Obb.fromAabb(local, crate.matrix))
```

## Conventions

- **Layout.** A `Vec3` is 12 bytes, a `Mat4` sixteen floats column after column — what a vertex attribute, a
  std430 array element and a shader's `float4x4` expect. (A *member* of a std140/std430 block still aligns a
  `vec3` to 16 bytes: pad such members yourself.)
- **Products.** Vectors are columns: `projection * view * model * point` applies `model` first. `a * b` of
  quaternions rotates by `b`, then by `a`.
- **Space.** Right-handed, +Y up, a camera looks down -Z, clip depth 0 (near) to 1 (far). The projections don't
  flip Y; a camera does (`[1][1] *= -1`), as the camera module's do.
- **Defaults.** Components are zero; a square matrix and a quaternion default to the identity; an `Aabb`
  defaults to empty, so `Expand` grows it from nothing. In C#, `new Mat4()`, `new Quat()` and `new Aabb()` do
  the same — `default(T)` and a new array's elements are still all zero.

## The same results everywhere

The C# and Kotlin libraries are ports, not wrappers: a `Vec3` addition is a C# or Kotlin addition. Each does
its arithmetic in the order the C++ one does, and kmath's own sources are compiled without fused
multiply-adds, so anything made of additions, multiplications, divisions, square roots and `Floor` gives **the
same bits** in all four: `Random`'s sequences, `Noise`'s fields, matrix products and inverses, `LookAt`,
ray casts, culling. That is what makes a seed reproduce the same world in every language, and a lockstep
simulation agree with itself. Their tests check it against the C interface, value for value.

What goes through `sin`, `cos`, `pow` and `log` — `AngleAxis`, `Perspective`, `Slerp`, `Ease`, the Gaussian —
agrees to within a few units in the last place, since each language's runtime rounds those its own way.

`bulk::` runs in Koral's native code in every language (C# and Kotlin call it through the C interface), and its
results equal the one-at-a-time functions' exactly.

## Names that clash

- C#: `Koral.Random` and `System.Random` are both in scope with implicit usings — write `Koral.Random`, or add
  `using Random = Koral.Random;`. `Koral.Transform` and `Koral.UI.Transform` (a 2D canvas transform) likewise:
  qualify one of them.
- Kotlin: `round` is kotlin.math's for a scalar (halves to even); `round(Vec3)` is kmath's (halves away from
  zero, as C++'s). kotlin.math's scalar functions aren't repeated — `abs`, `floor`, `sqrt`, `sin`, `min`, … are
  kotlin.math's own.
