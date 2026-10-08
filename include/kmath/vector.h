#pragma once

// kor::Vec and everything over it — glm's core (types and the GLSL function chapters) and its vector
// extensions. The vector half of kmath.h; kmath/matrix.h adds matrices.
//
//   detail/vector_types.h  Vec<T, N>: Vec2/3/4, DVec, IVec, UVec, BVec, the sized I8…U64 vectors; operators, swizzles
//   trigonometric.h        Radians, Sin … Atanh
//   exponential.h          Pow, Exp, Log, Exp2, Log2, Sqrt, InverseSqrt
//   common.h               Abs, Sign, Floor … Mix, Step, SmoothStep, IsNan, Fma, Frexp, bit casts
//   geometric.h            Dot, Cross, Length, Distance, Normalize, FaceForward, Reflect, Refract; norms
//   relational.h           LessThan … NotEqual, Any, All, Not, Select
//   integer.h              BitCount, FindLSB/MSB, Bitfield*, extended arithmetic; powers of two, multiples
//   packing.h              Pack/UnpackUnorm, Snorm, Half, Double2x32
//   epsilon.h              EpsilonEqual, ApproxEqual
//   component_wise.h       CompMin, CompMax, CompAdd, CompMul
//   vector_ext.h           Angle, OrientedAngle, Proj, Perp, Polar, Rotate*, SmoothDamp, ...

#include "detail/vector_types.h"
#include "trigonometric.h"
#include "exponential.h"
#include "common.h"
#include "geometric.h"
#include "relational.h"
#include "integer.h"
#include "packing.h"
#include "epsilon.h"
#include "component_wise.h"
#include "vector_ext.h"
