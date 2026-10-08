# Mathematics

`#include <kmath.h>` — or one of `kmath/*.h` for less. Everything is in `kor::`, and the same library is in
C (`koral_math_c.h`), C# (`Koral`, with the free functions in `Koral.KMath`) and Kotlin (package `koral`,
the free functions top-level), under the same names in each language's own casing.

kmath is laid out like glm: the vector and matrix types, then GLSL's function chapters — each defined once for a
scalar and component by component for vectors — then glm's extensions. `kmath/vector.h` includes all of the
chapters.

| Header | What | glm |
|---|---|---|
| `kmath/setup.h`, `constants.h` | `u8`…`u64`, `i8`…`i64`; `Pi`, `TwoPi`/`Tau`, `HalfPi`, `QuarterPi`, `OneOverPi`, `E`, `GoldenRatio`, `RootTwo`, `Ln2`, `Epsilon`, … (`Pi<double>` for the double) | `gtc/constants` |
| `kmath/vector.h` | `Vec<T, N>`: `Vec2/3/4` (float), `DVec*`, `IVec*`, `UVec*`, `BVec*`, `I8Vec*`…`U64Vec*`; every read swizzle (`v.ZYX()`, `v.XXYY()`); arithmetic, bit and comparison operators | `vec*`, `GLM_FORCE_SWIZZLE` |
| `kmath/trigonometric.h` | `Radians`, `Degrees`, `Sin`…`Atanh`, `Atan(y, x)`/`Atan2` | `trigonometric.hpp` |
| `kmath/exponential.h` | `Pow`, `Exp`, `Log`, `Exp2`, `Log2`, `Sqrt`, `InverseSqrt` | `exponential.hpp` |
| `kmath/common.h` | `Min`, `Max`, `Clamp`, `Saturate`, `Abs`, `Sign`, `Floor`, `Ceil`, `Trunc`, `Round`, `RoundEven`, `Fract`, `Mod`, `Modf`, `Mix`/`Lerp`, `Step`, `SmoothStep`, `SmootherStep`, `InverseLerp`, `Remap`, `IsNan`, `IsInf`, `Fma`, `Frexp`, `Ldexp`, `FloatBitsToInt`… | `common.hpp` |
| `kmath/geometric.h` | `Dot`, `Cross`, `Length`, `Length2`, `Distance`, `Distance2`, `Normalize`, `FaceForward`, `Reflect`, `Refract`, `L1Norm`, `LxNorm` | `geometric.hpp`, `gtx/norm` |
| `kmath/relational.h`, `epsilon.h` | `LessThan`…`NotEqual` (bool vectors), `Any`, `All`, `Not`, `Select`; `EpsilonEqual`, `ApproxEqual` | `vector_relational.hpp`, `ext/*_relational` |
| `kmath/integer.h` | `BitCount`, `FindLSB`, `FindMSB`, `BitfieldExtract/Insert/Reverse`, `UaddCarry`, `UsubBorrow`, `UmulExtended`, `ImulExtended`; `IsPowerOfTwo`, `Ceil/Floor/RoundPowerOfTwo`, `Ceil/Floor/RoundMultiple`, `AlignUp`, `DivideRoundUp` | `integer.hpp`, `gtc/round` |
| `kmath/packing.h` | `Pack/UnpackUnorm4x8`, `Snorm4x8`, `Unorm2x16`, `Snorm2x16`, `Half2x16`, `Double2x32`; `FloatToHalf`, `HalfToFloat` | `packing.hpp` |
| `kmath/component_wise.h`, `vector_ext.h` | `CompMin`, `CompMax`, `CompAdd`, `CompMul`; `Angle`, `OrientedAngle`, `Proj`, `Perp`, `Orthonormalize`, `TriangleNormal`, `Polar`, `Euclidean`, `Rotate`(2D), `RotateX/Y/Z`; and `NormalizeOr`, `ClampLength`, `AnyPerpendicular`, `MoveTowards`, `SmoothDamp`, `DeltaAngle`, `WrapAngle` | `gtx/component_wise`, `vector_angle`, `projection`, `perpendicular`, `rotate_vector`, `polar_coordinates`, `normal` |
| `kmath/matrix.h` | `Mat<T, C, R>`: `Mat2/3/4`, every `MatCxR` (`Mat2x3`, `Mat4x3`, …), and `DMat*`; `Transpose`, `Determinant`, `Inverse`, `NormalMatrix`, `MatrixCompMult`, `OuterProduct`, `Row`/`Column`, `Diagonal`, `Orthonormalize` | `mat*`, `matrix.hpp`, `gtc/matrix_access`, `gtx/orthonormalize` |
| `kmath/quaternion.h` | `Quat` (`DQuat`): `AngleAxis`, `FromEuler`, `LookRotation`, `FromTo`; `Slerp`, `Nlerp`, `Lerp`, `Mix`, `Rotate`, `RotateTowards`, `EulerAngles`, `Pitch`, `Yaw`, `Roll`, `ToMat3/4` | `gtc/quaternion` |
| `kmath/transform.h` | `Translation`, `Rotation`, `Scaling`, `Translate`, `Rotate`, `Scale`, `Compose`/`Decompose`; `LookAt` (`LookAtRH`, `LookAtLH`), `Perspective`, `PerspectiveFov`, `InfinitePerspective`, `FrustumProjection`, `PerspectiveReversedZ`, `Orthographic` — each for any `ClipSpace`; `Project`, `UnProject`, `PickMatrix`; `kor::Transform` | `gtc/matrix_transform` |
| `kmath/euler.h` | `EulerAngleX/Y/Z`, the two-axis `EulerAngleXY`…, all twelve orders `EulerAngleXYZ`…`EulerAngleZYZ` and their `ExtractEulerAngle*`; `EulerAngles(EulerOrder, …)`, `YawPitchRoll`, `QuatFromEuler` | `gtx/euler_angles` |
| `kmath/geometry.h` | `Ray`, `Plane`, `Sphere`, `Aabb`, `Aabb2`, `Obb`, `Triangle`, `Frustum`; `Raycast`, `Overlaps`, `ClosestPoint` | `gtx/intersect` and more |
| `kmath/random.h`, `noise.h` | `kor::Random` (PCG32), `Hash`; `kor::Noise`: Perlin, simplex, value and cellular noise, and fractals of them | `gtc/random`, `gtc/noise` |
| `kmath/interp.h` | `Ease` and the `Easing` curves; `Bezier`, `Hermite`, `CatmullRom`, `SamplePath` | `gtx/easing`, `gtx/spline` |
| `kmath/color.h` | sRGB ⇄ linear, HSV, HSL, Oklab, colour temperature, octahedral normals | `gtc/color_space` |
| `kmath/material.h` | Google's Material colours: the 2014 palette (`material::Red500`, `MaterialColor(MaterialHue::eTeal, 300)`, `MaterialAccent`), and Material 3's `Hct`, `TonalPalette`, `MaterialScheme::FromSeed` (every role, light or dark, nine variants), `SeedColors` (from an image), `Harmonize`, `ContrastRatio` | — |
| `kmath/bulk.h` | `kor::bulk::`: transforms, matrix products, bounds and frustum culling over spans, vectorised | — |

### The same API in C, C# and Kotlin

The vector and matrix types and every function of the chapters above exist in each binding for every scalar
type and size C++ has them for — generated from one description (`scripts/kmath/spec.py`), so they cannot
drift apart (a ctest fails when the generated code is stale):

| | C++ | C | C# | Kotlin |
|---|---|---|---|---|
| a type | `DVec3`, `Mat2x3` | `KoralDVec3`, `KoralMat2x3` | `DVec3`, `Mat2x3` | `DVec3`, `Mat2x3` |
| a function | `Floor(v)` | `koral_dvec3_floor(v)` | `KMath.Floor(v)` | `floor(v)` |
| vector with a scalar | `Mix(a, b, 0.5f)` | `koral_vec3_mix_s(a, b, 0.5f)` | `Mix(a, b, 0.5f)` | `mix(a, b, 0.5f)` |
| a swizzle | `v.ZYX()` | — (fields) | `v.ZYX` | `v.zyx` |
| an output | `Modf(v, whole)` | `koral_vec3_modf(v, &whole)` | `Modf(v, out var whole)` | `modf(v, Ref(...))` |
| a constant | `Pi<double>` | `KORAL_PI` (`KORAL_PI_F`) | `DPi` (`Pi`) | `DPi` (`Pi`) |

C names a function by its first vector argument's type (`koral_ivec2_clamp`), or its scalar type
(`koral_float_mod`, `koral_uint_bit_count`); `_s` is the overload whose broadcast arguments are scalars.
Vector arithmetic in C is inline: `koral_vec3_add`, `_sub`, `_mul`, `_div` (and `_s` forms), `koral_vec3_splat`,
`koral_ivec3_from_vec3`, …; matrices are `koral_<a>_mul_<b>`, `koral_mat4_mul_vec4`, `koral_mat2x3_transpose`.

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

## Material colours

The 2014 palette is data from the guidelines — `material::Red500` in C++, `KORAL_MATERIAL_RED_500` (0xRRGGBB) in C,
`MaterialColors.Red500` in C# and Kotlin — or by hue and shade: `MaterialColor(MaterialHue::eIndigo, 700)`,
`MaterialAccent(MaterialHue::eTeal, 400)` (A400).

Material 3's colours are Google's own code (material-color-utilities, Apache 2.0, compiled into Koral; its
licence ships in `share/Koral/licenses`), so a scheme here is the scheme Android and Compose make:

```cpp
const MaterialScheme dark = MaterialScheme::FromSeed(ColorFromHex(0x6750A4), /*dark*/ true);
draw(dark.surfaceContainer, dark.onSurface, dark.primary);           // every role Compose's ColorScheme has
const auto seeds = SeedColors(image.Pixels(), 4);                    // a wallpaper's colours, best first
const Vec4 brand = Harmonize(ColorFromHex(0xFF7043), seeds[0]);      // a brand colour that fits the scheme
assert(ContrastRatio(dark.onPrimary, dark.primary) >= 4.5f);
```

`Hct` is hue, chroma and tone: tone is perceived lightness, the same for every hue, which is what lets a
scheme promise contrast. These go through Koral's native code in every language, so they agree exactly.

## Conventions

- **Layout.** A `Vec3` is 12 bytes, a `Mat4` sixteen floats column after column — what a vertex attribute, a
  std430 array element and a shader's `float4x4` expect. (A *member* of a std140/std430 block still aligns a
  `vec3` to 16 bytes: pad such members yourself.)
- **Products.** Vectors are columns: `projection * view * model * point` applies `model` first. `a * b` of
  quaternions rotates by `b`, then by `a`.
- **Space.** Right-handed, +Y up, a camera looks down -Z, clip depth 0 (near) to 1 (far) — `ClipSpace::eRightHandedZeroToOne`,
  every projection's default. The others (glm's `RH_NO`, `LH_ZO`, `LH_NO`) are one argument away:
  `Perspective(fov, aspect, near, far, ClipSpace::eRightHandedNegativeOneToOne)` is OpenGL's. The projections
  don't flip Y; a camera does (`[1][1] *= -1`), as the camera module's do.
- **Euler angles.** `EulerAngleXYZ(a, b, c)` is `X(a) * Y(b) * Z(c)`, as glm's: Z turns a vector first.
  `Quat::FromEuler`/`EulerAngles(q)` are glm's (pitch, yaw, roll) about X, Y, Z.
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
- C++: `FrustumProjection` is glm's `frustum` (the projection); `Frustum` is the shape in `geometry.h`.
- C#: matrices have no `Mat4x4` alias (C has `KoralMat4x4`, Kotlin `typealias Mat4x4`): it is `Mat4`.
- Kotlin: where kotlin.math has the same scalar function (`sin`, `sqrt`, `floor`, `round`, `abs`, `sign`, `min`,
  `max`, …), the scalar one is kotlin.math's — kmath's would make every call ambiguous — while the vector forms
  (`floor(Vec3)`) are kmath's. Mind `round`: kotlin.math's halves to even, kmath's (C++'s) away from zero; and
  `min`/`max` of floats, which kotlin.math makes NaN-propagating. `UVec*` hold `UInt`s: `UVec2(1u, 2u)`, or
  `UVec2(1, 2)` from Ints.
