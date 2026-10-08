"""
kmath's GLSL-chapter functions, described once (see dsl.py for the body language). Every entry becomes a C
export (a wrapper of the C++ template), a C# overload in KMath and a Kotlin top-level function, for each of
its scalar types and, for the vector forms, sizes 2 to 4.

Fn(name, types, params, ret, body | raw, ...):
  types   which scalars: f float, d double, i int32, u uint32, b bool.
  params  (name, kind) or (name, kind, default). Kinds:
            T          the scalar; a vector of it in the mapped vector form
            V V2 V3 V4 a vector of T (V: of each size), BV a bool vector, IV an int vector of the same size
            int bool u32 i32 f32 f64 u16 f2 f4 u2   fixed types (f4 = Vec4, u2 = UVec2)
            int*       an int, an int vector in the mapped form (Ldexp's exponent)
          A trailing & makes it an output (C#'s out, C's pointer, Kotlin's Ref), @ an input-output (C#'s ref).
  ret     a kind as above, or "void".
  map     the scalar function also gets vector forms, component by component.
  bcast   in the vector form, these parameters may also be a scalar (a second overload; C's _s).
  sizes   for vector-kind functions: the sizes they exist for.
  raw     {"cs": ..., "kt": ...} bodies instead of the DSL: a string with $T for the scalar type's name,
          or a dict by type code. A body starting with "=" is an expression.
  c       False: no C export (an overload C cannot name).
"""

F, I, N, A = "fd", "iu", "fdiu", "fdiub"


class Fn:
    def __init__(self, name, types, params, ret, body=None, raw=None, map=False, bcast=(), sizes=(2, 3, 4), doc=None, c=True,
                 chapter=None):
        self.name, self.types, self.ret, self.body, self.raw = name, types, ret, body, raw
        self.params = [p if len(p) == 3 else (p[0], p[1], None) for p in params]
        self.map, self.bcast, self.sizes, self.doc, self.c = map, set(bcast), sizes, doc, c
        self.chapter = chapter

    @property
    def vector_kind(self):
        """Whether the function itself takes vectors (rather than being a scalar one, perhaps mapped)."""
        kinds = [k.rstrip("&@") for _, k, _ in self.params] + [self.ret]
        return any(k in ("V", "BV", "IV") for k in kinds)


FNS = []
chapter = None


def fn(*args, **kw):
    FNS.append(Fn(*args, chapter=chapter, **kw))


def cw(name, *args, **kw):
    """A component-wise function: scalar body, mapped to vectors."""
    fn(name, *args, map=True, **kw)


# ---- trigonometric (GLSL 8.1) -----------------------------------------------------------------------------

chapter = "trigonometric (GLSL 8.1, glm's trigonometric.hpp)"
cw("Radians", F, [("degrees", "T")], "T", ["return degrees * (Pi / 180)"], doc="Degrees to radians.")
cw("Degrees", F, [("radians", "T")], "T", ["return radians * (180 / Pi)"], doc="Radians to degrees.")
for f in ("Sin", "Cos", "Tan", "Asin", "Acos", "Atan", "Sinh", "Cosh", "Tanh", "Asinh", "Acosh", "Atanh"):
    cw(f, F, [("v", "T")], "T", [f"return std.{f.lower()}(v)"])
cw("Atan", F, [("y", "T"), ("x", "T")], "T", ["return std.atan2(y, x)"], bcast=["x"], c=False, doc="The angle of (x, y): GLSL's atan(y, x).")
cw("Atan2", F, [("y", "T"), ("x", "T")], "T", ["return std.atan2(y, x)"], bcast=["x"], doc="The angle of (x, y).")

# ---- exponential (GLSL 8.2) -------------------------------------------------------------------------------

chapter = "exponential (GLSL 8.2, glm's exponential.hpp)"
cw("Pow", F, [("v", "T"), ("e", "T")], "T", ["return std.pow(v, e)"], bcast=["e"])
cw("Exp", F, [("v", "T")], "T", ["return std.exp(v)"])
cw("Log", F, [("v", "T")], "T", ["return std.log(v)"], doc="The natural logarithm.")
cw("Exp2", F, [("v", "T")], "T", ["return std.exp2(v)"])
cw("Log2", F, [("v", "T")], "T", ["return std.log2(v)"])
cw("Sqrt", F, [("v", "T")], "T", ["return std.sqrt(v)"])
cw("InverseSqrt", F, [("v", "T")], "T", ["return 1 / std.sqrt(v)"])

# ---- common (GLSL 8.3) ------------------------------------------------------------------------------------

chapter = "common (GLSL 8.3, glm's common.hpp)"
cw("Min", N, [("a", "T"), ("b", "T")], "T", ["return b if b < a else a"], bcast=["b"], doc="The smaller; a when they are equal.")
cw("Max", N, [("a", "T"), ("b", "T")], "T", ["return b if a < b else a"], bcast=["b"], doc="The larger; a when they are equal.")
cw("Clamp", N, [("v", "T"), ("lo", "T"), ("hi", "T")], "T", ["return Min(Max(v, lo), hi)"], bcast=["lo", "hi"])
cw("Saturate", F, [("v", "T")], "T", ["return Clamp(v, 0, 1)"], doc="Clamp(v, 0, 1).")
cw("Abs", N, [("v", "T")], "T", {"u": ["return v"], "": ["return -v if v < 0 else v"]})
cw("Sign", N, [("v", "T")], "T", {"u": ["return 1 if 0 < v else 0"], "": ["return (1 if 0 < v else 0) - (1 if v < 0 else 0)"]}, doc="-1, 0 or 1.")
cw("Floor", F, [("v", "T")], "T", ["return std.floor(v)"])
cw("Ceil", F, [("v", "T")], "T", ["return std.ceil(v)"])
cw("Trunc", F, [("v", "T")], "T", ["return std.trunc(v)"])
cw("Round", F, [("v", "T")], "T", ["return std.round(v)"], doc="Halves away from zero, as C's round.")
cw("RoundEven", F, [("v", "T")], "T", ["return std.rint(v)"], doc="Halves to the even neighbour: 0.5 to 0, 1.5 to 2.")
cw("Fract", F, [("v", "T")], "T", ["return v - std.floor(v)"], doc="v - Floor(v): in [0, 1), negative v included.")
cw("Mod", N, [("v", "T"), ("m", "T")], "T",
   {"f": ["return v - m * std.floor(v / m)"], "d": ["return v - m * std.floor(v / m)"], "u": ["return v % m"],
    "i": ["r = v % m", "return r + m if r != 0 and (r < 0) != (m < 0) else r"]},
   bcast=["m"], doc="GLSL's mod: the result has the sign of m.")
cw("Modf", F, [("v", "T"), ("whole", "T&")], "T", raw={
    "cs": ["whole = $T.Truncate(v);", "return $T.IsInfinity(v) ? $T.CopySign($0, v) : v - whole;"],
    "kt": ["whole.value = kotlin.math.truncate(v)", "return if (v.isInfinite()) $0.withSign(v) else v - whole.value"]},
   doc="The fractional part, the whole part in whole (both with v's sign).")
cw("Mix", F, [("a", "T"), ("b", "T"), ("t", "T")], "T", ["return a + (b - a) * t"], bcast=["t"], doc="a + (b - a) * t: not clamped.")
cw("Lerp", F, [("a", "T"), ("b", "T"), ("t", "T")], "T", ["return a + (b - a) * t"], bcast=["t"], doc="Mix by its other name.")
cw("Mix", A, [("a", "T"), ("b", "T"), ("pick", "bool")], "T", ["return b if pick else a"], c=False, doc="b where pick, a elsewhere.")
cw("Step", F, [("edge", "T"), ("v", "T")], "T", ["return 0 if v < edge else 1"], bcast=["edge"], doc="0 below edge, 1 from it on.")
cw("SmoothStep", F, [("edge0", "T"), ("edge1", "T"), ("v", "T")], "T",
   ["t = Saturate((v - edge0) / (edge1 - edge0))", "return t * t * (3 - 2 * t)"], bcast=["edge0", "edge1"],
   doc="Hermite 3t² - 2t³ between the edges, clamped.")
cw("SmootherStep", F, [("edge0", "T"), ("edge1", "T"), ("v", "T")], "T",
   ["t = Saturate((v - edge0) / (edge1 - edge0))", "return t * t * t * (t * (t * 6 - 15) + 10)"], bcast=["edge0", "edge1"],
   doc="Perlin's 6t⁵ - 15t⁴ + 10t³.")
fn("InverseLerp", F, [("a", "T"), ("b", "T"), ("v", "T")], "T", ["return 0 if a == b else (v - a) / (b - a)"],
   doc="The t for which Mix(a, b, t) is v; 0 when a == b.")
fn("Remap", F, [("v", "T"), ("inMin", "T"), ("inMax", "T"), ("outMin", "T"), ("outMax", "T")], "T",
   ["return Mix(outMin, outMax, InverseLerp(inMin, inMax, v))"], doc="v from [inMin, inMax] onto [outMin, outMax], not clamped.")
cw("IsNan", F, [("v", "T")], "bool", ["return std.isnan(v)"])
cw("IsInf", F, [("v", "T")], "bool", ["return std.isinf(v)"])
cw("IsFinite", F, [("v", "T")], "bool", ["return std.isfinite(v)"])
cw("Fma", F, [("a", "T"), ("b", "T"), ("c", "T")], "T", ["return std.fma(a, b, c)"], doc="a * b + c, rounded once.")
cw("Frexp", F, [("v", "T"), ("exponent", "int&")], "T", raw={
    "cs": ["if (v == 0 || !$T.IsFinite(v)) { exponent = 0; return v; }",
           "exponent = $T.ILogB(v) + 1;", "return $T.ScaleB(v, -exponent);"],
    "kt": ["if (v == $0 || !v.isFinite()) { exponent.value = 0; return v }",
           "val e = (if (kotlin.math.abs(v) < java.lang.$T.MIN_NORMAL) Math.getExponent(Math.scalb(v, 64)) - 64 else Math.getExponent(v)) + 1",
           "exponent.value = e", "return Math.scalb(v, -e)"]},
   doc="v = mantissa * 2^exponent, the mantissa in [0.5, 1).")
cw("Ldexp", F, [("v", "T"), ("exponent", "int*")], "T", ["return std.ldexp(v, exponent)"], doc="v * 2^exponent.")
cw("FloatBitsToInt", "f", [("v", "T")], "i32", raw={"cs": "=BitConverter.SingleToInt32Bits(v)", "kt": "=v.toRawBits()"})
cw("FloatBitsToUint", "f", [("v", "T")], "u32", raw={"cs": "=BitConverter.SingleToUInt32Bits(v)", "kt": "=v.toRawBits().toUInt()"})
cw("IntBitsToFloat", "i", [("v", "T")], "f32", raw={"cs": "=BitConverter.Int32BitsToSingle(v)", "kt": "=Float.fromBits(v)"})
cw("UintBitsToFloat", "u", [("v", "T")], "f32", raw={"cs": "=BitConverter.UInt32BitsToSingle(v)", "kt": "=Float.fromBits(v.toInt())"})

# ---- geometric (GLSL 8.5) ---------------------------------------------------------------------------------

chapter = "geometric (GLSL 8.5, glm's geometric.hpp and gtx/norm)"
XYZW = "xyzw"


def each(n, template, sep):
    return sep.join(template.format(c=c) for c in XYZW[:n])


def per_size(name, types, params, ret, make_body, sizes=(2, 3, 4), **kw):
    """A vector function whose body depends on the size: one entry per size."""
    for n in sizes:
        fn(name, types, [(p, k.replace("V", f"V{n}") if k in ("V",) else k.replace("BV", f"BV{n}") if k == "BV" else k, d)
                         for p, k, d in [p if len(p) == 3 else (*p, None) for p in params]],
           ret.replace("V", f"V{n}") if ret == "V" else (f"BV{n}" if ret == "BV" else ret), make_body(n), sizes=(n,), **kw)


per_size("Dot", N, [("a", "V"), ("b", "V")], "T", lambda n: ["return " + each(n, "a.{c} * b.{c}", " + ")])
fn("Cross", "fdi", [("a", "V3"), ("b", "V3")], "V3", ["return V3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x)"], sizes=(3,))
fn("Cross", "fdi", [("a", "V2"), ("b", "V2")], "T", ["return a.x * b.y - a.y * b.x"], sizes=(2,),
   doc="The z of the 3D cross product: positive when b is counter-clockwise from a.")
fn("Length", F, [("v", "V")], "T", ["return std.sqrt(Dot(v, v))"])
fn("Length2", N, [("v", "V")], "T", ["return Dot(v, v)"], doc="The squared length.")
fn("Distance", F, [("a", "V"), ("b", "V")], "T", ["return Length(b - a)"])
fn("Distance2", N, [("a", "V"), ("b", "V")], "T", ["return Length2(b - a)"], doc="The squared distance.")
fn("Normalize", F, [("v", "V")], "V", ["len = Length(v)", "return v * (1 / len) if len > 0 else v"],
   doc="v / Length(v); a zero vector stays zero rather than becoming NaN.")
fn("FaceForward", F, [("n", "V"), ("i", "V"), ("nRef", "V")], "V", ["return n if Dot(nRef, i) < 0 else -n"],
   doc="n if Dot(nRef, i) < 0, else -n: a normal turned toward the viewer.")
fn("Reflect", F, [("i", "V"), ("n", "V")], "V", ["return i - n * (2 * Dot(n, i))"],
   doc="The mirror of incident direction i about the surface with (normalised) normal n.")
fn("Refract", F, [("i", "V"), ("n", "V"), ("eta", "T")], "V",
   ["d = Dot(n, i)", "k = 1 - eta * eta * (1 - d * d)", "return V(0) if k < 0 else i * eta - n * (eta * d + std.sqrt(k))"],
   doc="The refraction of i through n with ratio of indices eta; zero on total internal reflection.")
per_size("L1Norm", N, [("v", "V")], "T", lambda n: ["return " + each(n, "Abs(v.{c})", " + ")], doc="The sum of |v|: the taxicab length.")
fn("L2Norm", F, [("v", "V")], "T", ["return Length(v)"])
per_size("LxNorm", F, [("v", "V"), ("p", "T")], "T", lambda n: ["return std.pow(" + each(n, "std.pow(Abs(v.{c}), p)", " + ") + ", 1 / p)"])

# ---- relational (GLSL 8.7) and epsilon comparisons ---------------------------------------------------------

chapter = "relational (GLSL 8.7, glm's vector_relational.hpp, ext/vector_relational)"
for name, op, types in (("LessThan", "<", N), ("LessThanEqual", "<=", N), ("GreaterThan", ">", N), ("GreaterThanEqual", ">=", N),
                        ("Equal", "==", A), ("NotEqual", "!=", A)):
    per_size(name, types, [("a", "V"), ("b", "V")], "BV", lambda n, op=op: ["return BV(" + each(n, "a.{c} " + op + " b.{c}", ", ") + ")"])
per_size("Any", "b", [("v", "V")], "bool", lambda n: ["return " + each(n, "v.{c}", " or ")])
per_size("All", "b", [("v", "V")], "bool", lambda n: ["return " + each(n, "v.{c}", " and ")])
per_size("Not", "b", [("v", "V")], "V", lambda n: ["return V(" + each(n, "not v.{c}", ", ") + ")"])
per_size("Select", A, [("mask", "BV"), ("ifTrue", "V"), ("ifFalse", "V")], "V",
         lambda n: ["return V(" + each(n, "ifTrue.{c} if mask.{c} else ifFalse.{c}", ", ") + ")"],
         doc="Per component: ifTrue where the mask is set, ifFalse elsewhere.")
per_size("Mix", A, [("a", "V"), ("b", "V"), ("pick", "BV")], "V", lambda n: ["return V(" + each(n, "b.{c} if pick.{c} else a.{c}", ", ") + ")"],
         doc="b where pick, a elsewhere (GLSL's mix with a bool vector).", c=False)
fn("EpsilonEqual", F, [("a", "T"), ("b", "T"), ("epsilon", "T", "Epsilon")], "bool", ["return Abs(a - b) <= epsilon"], doc="|a - b| <= epsilon.")
fn("EpsilonNotEqual", F, [("a", "T"), ("b", "T"), ("epsilon", "T", "Epsilon")], "bool", ["return Abs(a - b) > epsilon"])
per_size("EpsilonEqual", F, [("a", "V"), ("b", "V"), ("epsilon", "T", "Epsilon")], "BV",
         lambda n: ["return BV(" + each(n, "Abs(a.{c} - b.{c}) <= epsilon", ", ") + ")"])
fn("EpsilonNotEqual", F, [("a", "V"), ("b", "V"), ("epsilon", "T", "Epsilon")], "BV", ["return Not(EpsilonEqual(a, b, epsilon))"])
fn("ApproxEqual", F, [("a", "T"), ("b", "T"), ("epsilon", "T", "Epsilon")], "bool", ["return Abs(a - b) <= epsilon"],
   doc="Within epsilon: one bool, for checks and tests.")
fn("ApproxEqual", F, [("a", "V"), ("b", "V"), ("epsilon", "T", "Epsilon")], "bool", ["return All(EpsilonEqual(a, b, epsilon))"])

# ---- integer (GLSL 8.8) and rounding ----------------------------------------------------------------------

chapter = "integer (GLSL 8.8, glm's integer.hpp, ext/scalar_integer, gtc/round)"
cw("BitCount", I, [("v", "T")], "int", raw={"cs": {"i": "=BitOperations.PopCount((uint)v)", "u": "=BitOperations.PopCount(v)"},
                                           "kt": "=v.countOneBits()"}, doc="How many bits are set.")
cw("FindLSB", I, [("v", "T")], "int", raw={"cs": "=v == 0 ? -1 : BitOperations.TrailingZeroCount(v)",
                                          "kt": "=if (v == $0) -1 else v.countTrailingZeroBits()"}, doc="The lowest set bit's index; -1 for 0.")
cw("FindMSB", I, [("v", "T")], "int", raw={
    "cs": {"i": ["var u = v < 0 ? ~v : v;", "return u == 0 ? -1 : 31 - BitOperations.LeadingZeroCount((uint)u);"],
           "u": "=v == 0 ? -1 : 31 - BitOperations.LeadingZeroCount(v)"},
    "kt": {"i": ["val u = if (v < 0) v.inv() else v", "return if (u == 0) -1 else 31 - u.countLeadingZeroBits()"],
           "u": "=if (v == 0u) -1 else 31 - v.countLeadingZeroBits()"}},
   doc="The highest set bit's index; -1 for 0. For a negative value: the highest bit that is 0.")
cw("BitfieldExtract", I, [("v", "T"), ("offset", "int"), ("bits", "int")], "T", raw={
    "cs": {"u": ["if (bits <= 0) return 0u;", "return bits >= 32 ? v >> offset : (v >> offset) & ((1u << bits) - 1u);"],
           "i": ["if (bits <= 0) return 0;", "uint u = (uint)v;", "uint f = bits >= 32 ? u >> offset : (u >> offset) & ((1u << bits) - 1u);",
                 "if (bits < 32 && ((f >> (bits - 1)) & 1u) != 0) return (int)(f | ~((1u << bits) - 1u));", "return (int)f;"]},
    "kt": {"u": ["if (bits <= 0) return 0u", "return if (bits >= 32) v shr offset else (v shr offset) and ((1u shl bits) - 1u)"],
           "i": ["if (bits <= 0) return 0", "val u = v.toUInt()", "val f = if (bits >= 32) u shr offset else (u shr offset) and ((1u shl bits) - 1u)",
                 "if (bits < 32 && ((f shr (bits - 1)) and 1u) != 0u) return (f or ((1u shl bits) - 1u).inv()).toInt()", "return f.toInt()"]}},
   doc="bits bits of v from bit offset, at the bottom of the result: sign-extended for a signed type.")
cw("BitfieldInsert", I, [("base", "T"), ("insert", "T"), ("offset", "int"), ("bits", "int")], "T", raw={
    "cs": ["if (bits <= 0) return @base;", "uint low = bits >= 32 ? ~0u : (1u << bits) - 1u;", "uint mask = low << offset;",
           "return unchecked(($T)(((uint)@base & ~mask) | (((uint)insert << offset) & mask)));"],
    "kt": ["if (bits <= 0) return base", "val low = if (bits >= 32) 0u.inv() else (1u shl bits) - 1u", "val mask = low shl offset",
           "return ((base.toUInt() and mask.inv()) or ((insert.toUInt() shl offset) and mask)).to$T()"]},
   doc="base with bits bits from bit offset replaced by the low bits of insert.")
cw("BitfieldReverse", I, [("v", "T")], "T", raw={
    "cs": ["uint u = (uint)v, r = 0;", "for (int i = 0; i < 32; ++i, u >>= 1) r = (r << 1) | (u & 1u);", "return unchecked(($T)r);"],
    "kt": ["var u = v.toUInt()", "var r = 0u", "repeat(32) { r = (r shl 1) or (u and 1u); u = u shr 1 }", "return r.to$T()"]},
   doc="The bits in reverse order.")
fn("UaddCarry", "u", [("x", "T"), ("y", "T"), ("carry", "T&")], "T", raw={
    "cs": ["uint s = x + y;", "carry = s < x ? 1u : 0u;", "return s;"],
    "kt": ["val s = x + y", "carry.value = if (s < x) 1u else 0u", "return s"]}, map=True, doc="x + y, and in carry whether it overflowed.")
fn("UsubBorrow", "u", [("x", "T"), ("y", "T"), ("borrow", "T&")], "T", raw={
    "cs": ["borrow = x < y ? 1u : 0u;", "return x - y;"],
    "kt": ["borrow.value = if (x < y) 1u else 0u", "return x - y"]}, map=True, doc="x - y, and in borrow whether it went below zero.")
fn("UmulExtended", "u", [("x", "T"), ("y", "T"), ("msb", "T&"), ("lsb", "T&")], "void", raw={
    "cs": ["ulong p = (ulong)x * y;", "msb = (uint)(p >> 32);", "lsb = (uint)p;"],
    "kt": ["val p = x.toULong() * y.toULong()", "msb.value = (p shr 32).toUInt()", "lsb.value = p.toUInt()"]}, map=True,
   doc="The 64-bit product, its high and low halves.")
fn("ImulExtended", "i", [("x", "T"), ("y", "T"), ("msb", "T&"), ("lsb", "T&")], "void", raw={
    "cs": ["long p = (long)x * y;", "msb = (int)(p >> 32);", "lsb = (int)p;"],
    "kt": ["val p = x.toLong() * y.toLong()", "msb.value = (p shr 32).toInt()", "lsb.value = p.toInt()"]}, map=True)
cw("IsPowerOfTwo", I, [("v", "T")], "bool", ["return v > 0 and (v & (v - 1)) == 0"])
cw("CeilPowerOfTwo", I, [("v", "T")], "T", raw={
    "cs": {"i": "=v <= 1 ? 1 : (int)BitOperations.RoundUpToPowerOf2((uint)v)", "u": "=v <= 1 ? 1u : BitOperations.RoundUpToPowerOf2(v)"},
    "kt": {"i": "=if (v <= 1) 1 else Integer.highestOneBit(v - 1) shl 1",
           "u": "=if (v <= 1u) 1u else (Integer.highestOneBit((v - 1u).toInt()) shl 1).toUInt()"}},
   doc="The smallest power of two >= v (1 for 0 and below).")
cw("FloorPowerOfTwo", I, [("v", "T")], "T", raw={
    "cs": {"i": "=v <= 0 ? 0 : 1 << (31 - BitOperations.LeadingZeroCount((uint)v))", "u": "=v == 0 ? 0u : 1u << (31 - BitOperations.LeadingZeroCount(v))"},
    "kt": {"i": "=if (v <= 0) 0 else Integer.highestOneBit(v)", "u": "=if (v == 0u) 0u else Integer.highestOneBit(v.toInt()).toUInt()"}},
   doc="The largest power of two <= v (0 for 0 and below).")
cw("RoundPowerOfTwo", I, [("v", "T")], "T", ["up = CeilPowerOfTwo(v)", "down = FloorPowerOfTwo(v)", "return up if up - v <= v - down else down"],
   doc="The nearest power of two (the larger, halfway between two).")
cw("IsMultiple", I, [("v", "T"), ("multiple", "T")], "bool", ["return multiple != 0 and v % multiple == 0"], bcast=["multiple"])
cw("CeilMultiple", N, [("v", "T"), ("multiple", "T")], "T",
   {"f": ["return std.ceil(v / multiple) * multiple"], "d": ["return std.ceil(v / multiple) * multiple"],
    "": ["r = Mod(v, multiple)", "return v if r == 0 else v + multiple - r"]}, bcast=["multiple"],
   doc="v rounded up (toward +infinity) to a multiple.")
cw("FloorMultiple", N, [("v", "T"), ("multiple", "T")], "T",
   {"f": ["return std.floor(v / multiple) * multiple"], "d": ["return std.floor(v / multiple) * multiple"],
    "": ["return v - Mod(v, multiple)"]}, bcast=["multiple"])
cw("RoundMultiple", N, [("v", "T"), ("multiple", "T")], "T",
   {"f": ["return std.round(v / multiple) * multiple"], "d": ["return std.round(v / multiple) * multiple"],
    "": ["down = FloorMultiple(v, multiple)", "up = down + multiple", "return down if v - down < up - v else up"]}, bcast=["multiple"])
fn("AlignUp", "u", [("v", "T"), ("alignment", "T")], "T", ["return (v + alignment - 1) & ~(alignment - 1)"],
   doc="v rounded up to a multiple of alignment, which must be a power of two.")
fn("DivideRoundUp", I, [("a", "T"), ("b", "T")], "T", ["return (a + b - 1) / b"], doc="a / b rounded up: how many groups of b cover a.")

# ---- packing (GLSL 8.4) -----------------------------------------------------------------------------------

chapter = "packing (GLSL 8.4, glm's packing.hpp)"
fn("FloatToHalf", "f", [("value", "T")], "u16", raw={
    "cs": ["uint bits = BitConverter.SingleToUInt32Bits(value);", "uint sign = (bits >> 16) & 0x8000u;", "uint exponent = (bits >> 23) & 0xffu;",
           "uint mantissa = bits & 0x7fffffu;", "if (exponent == 0xffu) return (ushort)(sign | 0x7c00u | (mantissa != 0 ? 0x200u : 0u));",
           "int e = (int)exponent - 127 + 15;", "if (e >= 31) return (ushort)(sign | 0x7c00u);", "if (e <= 0)", "{",
           "    if (e < -10) return (ushort)sign;", "    mantissa |= 0x800000u;", "    int shift = 14 - e;", "    uint half = mantissa >> shift;",
           "    uint rem = mantissa & ((1u << shift) - 1u), halfway = 1u << (shift - 1);",
           "    if (rem > halfway || (rem == halfway && (half & 1u) != 0)) ++half;", "    return (ushort)(sign | half);", "}",
           "uint h = ((uint)e << 10) | (mantissa >> 13);", "uint r = mantissa & 0x1fffu;",
           "if (r > 0x1000u || (r == 0x1000u && (h & 1u) != 0)) ++h;", "return (ushort)(sign | h);"],
    "kt": ["val bits = value.toRawBits().toUInt()", "val sign = (bits shr 16) and 0x8000u", "val exponent = (bits shr 23) and 0xffu",
           "var mantissa = bits and 0x7fffffu", "if (exponent == 0xffu) return (sign or 0x7c00u or (if (mantissa != 0u) 0x200u else 0u)).toUShort()",
           "val e = exponent.toInt() - 127 + 15", "if (e >= 31) return (sign or 0x7c00u).toUShort()", "if (e <= 0) {",
           "    if (e < -10) return sign.toUShort()", "    mantissa = mantissa or 0x800000u", "    val shift = 14 - e", "    var half = mantissa shr shift",
           "    val rem = mantissa and ((1u shl shift) - 1u)", "    val halfway = 1u shl (shift - 1)",
           "    if (rem > halfway || (rem == halfway && (half and 1u) != 0u)) half++", "    return (sign or half).toUShort()", "}",
           "var h = (e.toUInt() shl 10) or (mantissa shr 13)", "val r = mantissa and 0x1fffu",
           "if (r > 0x1000u || (r == 0x1000u && (h and 1u) != 0u)) h++", "return (sign or h).toUShort()"]},
   doc="IEEE half precision, round to nearest even; overflow becomes infinity, NaN stays NaN.")
fn("HalfToFloat", "f", [("half", "u16")], "f32", raw={
    "cs": ["uint sign = (uint)(half & 0x8000u) << 16;", "uint exponent = ((uint)half >> 10) & 0x1fu;", "uint mantissa = half & 0x3ffu;",
           "if (exponent == 0)", "{", "    if (mantissa == 0) return BitConverter.UInt32BitsToSingle(sign);", "    int e = -1;",
           "    do { ++e; mantissa <<= 1; } while ((mantissa & 0x400u) == 0);",
           "    return BitConverter.UInt32BitsToSingle(sign | ((uint)(127 - 15 - e) << 23) | ((mantissa & 0x3ffu) << 13));", "}",
           "if (exponent == 31) return BitConverter.UInt32BitsToSingle(sign | 0x7f800000u | (mantissa << 13));",
           "return BitConverter.UInt32BitsToSingle(sign | ((exponent + 127 - 15) << 23) | (mantissa << 13));"],
    "kt": ["val h = half.toUInt()", "val sign = (h and 0x8000u) shl 16", "val exponent = (h shr 10) and 0x1fu", "var mantissa = h and 0x3ffu",
           "if (exponent == 0u) {", "    if (mantissa == 0u) return Float.fromBits(sign.toInt())", "    var e = -1",
           "    do { ++e; mantissa = mantissa shl 1 } while ((mantissa and 0x400u) == 0u)",
           "    return Float.fromBits((sign or ((127 - 15 - e).toUInt() shl 23) or ((mantissa and 0x3ffu) shl 13)).toInt())", "}",
           "if (exponent == 31u) return Float.fromBits((sign or 0x7f800000u or (mantissa shl 13)).toInt())",
           "return Float.fromBits((sign or ((exponent + 127u - 15u) shl 23) or (mantissa shl 13)).toInt())"]})
UNORM = {"cs": "(uint)RoundAwayToInt(Clamp({v}, 0f, 1f) * {s})", "kt": "roundAwayToInt(clamp({v}, 0f, 1f) * {s}).toUInt()"}
SNORM = {"cs": "RoundAwayToInt(Clamp({v}, -1f, 1f) * {s})", "kt": "roundAwayToInt(clamp({v}, -1f, 1f) * {s})"}


def un(lang, v, s): return UNORM[lang].format(v=v, s=s)
def sn(lang, v, s): return SNORM[lang].format(v=v, s=s)


fn("PackUnorm4x8", "f", [("v", "f4")], "u32", raw={
    "cs": "=" + " | ".join(f"({un('cs', 'v.' + c, '255f')} << {8 * i})" for i, c in enumerate("XYZW")),
    "kt": "=" + " or ".join(f"({un('kt', 'v.' + c, '255f')} shl {8 * i})" for i, c in enumerate("xyzw"))},
   doc="Four [0, 1] floats into 8 bits each, x in the lowest byte.")
fn("UnpackUnorm4x8", "f", [("p", "u32")], "f4", raw={
    "cs": "=new Vec4(" + ", ".join(f"((p >> {8 * i}) & 0xffu) / 255f" for i in range(4)) + ")",
    "kt": "=Vec4(" + ", ".join(f"((p shr {8 * i}) and 0xffu).toFloat() / 255f" for i in range(4)) + ")"})
fn("PackSnorm4x8", "f", [("v", "f4")], "u32", raw={
    "cs": "=" + " | ".join(f"(((uint){sn('cs', 'v.' + c, '127f')} & 0xffu) << {8 * i})" for i, c in enumerate("XYZW")),
    "kt": "=" + " or ".join(f"(({sn('kt', 'v.' + c, '127f')}.toUInt() and 0xffu) shl {8 * i})" for i, c in enumerate("xyzw"))},
   doc="Four [-1, 1] floats into 8 signed bits each.")
fn("UnpackSnorm4x8", "f", [("p", "u32")], "f4", raw={
    "cs": "=new Vec4(" + ", ".join(f"Clamp((sbyte)(byte)(p >> {8 * i}) / 127f, -1f, 1f)" for i in range(4)) + ")",
    "kt": "=Vec4(" + ", ".join(f"clamp((p shr {8 * i}).toByte().toFloat() / 127f, -1f, 1f)" for i in range(4)) + ")"})
fn("PackUnorm2x16", "f", [("v", "f2")], "u32", raw={
    "cs": f"={un('cs', 'v.X', '65535f')} | ({un('cs', 'v.Y', '65535f')} << 16)",
    "kt": f"={un('kt', 'v.x', '65535f')} or ({un('kt', 'v.y', '65535f')} shl 16)"}, doc="Two [0, 1] floats into 16 bits each.")
fn("UnpackUnorm2x16", "f", [("p", "u32")], "f2", raw={
    "cs": "=new Vec2((p & 0xffffu) / 65535f, (p >> 16) / 65535f)",
    "kt": "=Vec2((p and 0xffffu).toFloat() / 65535f, (p shr 16).toFloat() / 65535f)"})
fn("PackSnorm2x16", "f", [("v", "f2")], "u32", raw={
    "cs": f"=((uint){sn('cs', 'v.X', '32767f')} & 0xffffu) | (((uint){sn('cs', 'v.Y', '32767f')} & 0xffffu) << 16)",
    "kt": f"=({sn('kt', 'v.x', '32767f')}.toUInt() and 0xffffu) or (({sn('kt', 'v.y', '32767f')}.toUInt() and 0xffffu) shl 16)"},
   doc="Two [-1, 1] floats into 16 signed bits each.")
fn("UnpackSnorm2x16", "f", [("p", "u32")], "f2", raw={
    "cs": "=new Vec2(Clamp((short)(ushort)p / 32767f, -1f, 1f), Clamp((short)(ushort)(p >> 16) / 32767f, -1f, 1f))",
    "kt": "=Vec2(clamp(p.toShort().toFloat() / 32767f, -1f, 1f), clamp((p shr 16).toShort().toFloat() / 32767f, -1f, 1f))"})
fn("PackHalf2x16", "f", [("v", "f2")], "u32", raw={
    "cs": "=FloatToHalf(v.X) | ((uint)FloatToHalf(v.Y) << 16)", "kt": "=floatToHalf(v.x).toUInt() or (floatToHalf(v.y).toUInt() shl 16)"},
   doc="Two floats as halves, x in the low 16 bits.")
fn("UnpackHalf2x16", "f", [("p", "u32")], "f2", raw={
    "cs": "=new Vec2(HalfToFloat((ushort)p), HalfToFloat((ushort)(p >> 16)))",
    "kt": "=Vec2(halfToFloat(p.toUShort()), halfToFloat((p shr 16).toUShort()))"})
fn("PackDouble2x32", "f", [("v", "u2")], "f64", raw={
    "cs": "=BitConverter.UInt64BitsToDouble(v.X | ((ulong)v.Y << 32))",
    "kt": "=Double.fromBits((v.x.toULong() or (v.y.toULong() shl 32)).toLong())"}, doc="A double from its bits as two uints, low first.")
fn("UnpackDouble2x32", "f", [("d", "f64")], "u2", raw={
    "cs": ["ulong b = BitConverter.DoubleToUInt64Bits(d);", "return new UVec2((uint)b, (uint)(b >> 32));"],
    "kt": ["val b = d.toRawBits().toULong()", "return UVec2(b.toUInt(), (b shr 32).toUInt())"]})

# ---- component-wise (glm's gtx/component_wise) ------------------------------------------------------------

chapter = "component-wise folds (glm's gtx/component_wise)"
per_size("CompMin", N, [("v", "V")], "T", lambda n: ["return " + ("Min(" * (n - 1)) + "v.x, " + ", ".join(f"v.{c})" for c in XYZW[1:n])])
per_size("CompMax", N, [("v", "V")], "T", lambda n: ["return " + ("Max(" * (n - 1)) + "v.x, " + ", ".join(f"v.{c})" for c in XYZW[1:n])])
per_size("CompAdd", N, [("v", "V")], "T", lambda n: ["return " + each(n, "v.{c}", " + ")])
per_size("CompMul", N, [("v", "V")], "T", lambda n: ["return " + each(n, "v.{c}", " * ")])

# ---- glm's gtx vector extensions, and Koral's own ---------------------------------------------------------

chapter = "vector extensions (glm's gtx/vector_angle, projection, perpendicular, rotate_vector, polar_coordinates; Koral's own)"
fn("Angle", F, [("a", "V"), ("b", "V")], "T",
   ["denom = std.sqrt(Length2(a) * Length2(b))", "return std.acos(Clamp(Dot(a, b) / denom, -1, 1)) if denom > 0 else 0"],
   doc="The unsigned angle between two vectors, in radians.")
fn("OrientedAngle", F, [("a", "V2"), ("b", "V2")], "T", ["return std.atan2(Cross(a, b), Dot(a, b))"], sizes=(2,),
   doc="The angle from a to b, positive counter-clockwise.")
fn("OrientedAngle", F, [("a", "V3"), ("b", "V3"), ("axis", "V3")], "T", ["return std.atan2(Dot(Cross(a, b), axis), Dot(a, b))"], sizes=(3,),
   doc="The angle from a to b around axis: positive when counter-clockwise looking down the axis.")
fn("Proj", F, [("x", "V"), ("direction", "V")], "V",
   ["sq = Dot(direction, direction)", "return direction * (Dot(x, direction) / sq) if sq > 0 else V(0)"], doc="The part of x along direction.")
fn("Perp", F, [("x", "V"), ("direction", "V")], "V", ["return x - Proj(x, direction)"], doc="The part of x perpendicular to direction.")
fn("Orthonormalize", F, [("x", "V"), ("y", "V")], "V", ["return Normalize(x - y * Dot(y, x))"], doc="x made perpendicular to (normalised) y, and normalised.")
fn("TriangleNormal", F, [("a", "V3"), ("b", "V3"), ("c", "V3")], "V3", ["return Normalize(Cross(b - a, c - a))"], sizes=(3,),
   doc="The unit normal of a triangle, counter-clockwise front.")
fn("Polar", F, [("euclidean", "V3")], "V3",
   ["len = Length(euclidean)", "n = euclidean / len", "return V3(std.asin(Clamp(n.y, -1, 1)), std.atan2(n.x, n.z), len)"], sizes=(3,),
   doc="A Euclidean vector as (latitude, longitude, length).")
fn("Euclidean", F, [("polar", "V2")], "V3",
   ["lat = polar.x", "lon = polar.y", "return V3(std.cos(lat) * std.sin(lon), std.sin(lat), std.cos(lat) * std.cos(lon))"], sizes=(2,),
   doc="A unit vector from (latitude, longitude).")
fn("Rotate", F, [("v", "V2"), ("angle", "T")], "V2", ["c = std.cos(angle)", "s = std.sin(angle)", "return V2(v.x * c - v.y * s, v.x * s + v.y * c)"],
   sizes=(2,), doc="v turned angle radians counter-clockwise.")
fn("RotateX", F, [("v", "V3"), ("angle", "T")], "V3", ["c = std.cos(angle)", "s = std.sin(angle)", "return V3(v.x, v.y * c - v.z * s, v.y * s + v.z * c)"], sizes=(3,))
fn("RotateY", F, [("v", "V3"), ("angle", "T")], "V3", ["c = std.cos(angle)", "s = std.sin(angle)", "return V3(v.x * c + v.z * s, v.y, -v.x * s + v.z * c)"], sizes=(3,))
fn("RotateZ", F, [("v", "V3"), ("angle", "T")], "V3", ["c = std.cos(angle)", "s = std.sin(angle)", "return V3(v.x * c - v.y * s, v.x * s + v.y * c, v.z)"], sizes=(3,))
fn("NormalizeOr", F, [("v", "V"), ("fallback", "V")], "V", ["len = Length(v)", "return v * (1 / len) if len > std.machine_epsilon else fallback"],
   doc="Normalize, or fallback when v is (nearly) zero.")
fn("ClampLength", F, [("v", "V"), ("maxLength", "T")], "V",
   ["sq = Length2(v)", "return v * (maxLength / std.sqrt(sq)) if sq > maxLength * maxLength else v"], doc="v with its length capped at maxLength.")
fn("AnyPerpendicular", F, [("v", "V3")], "V3", ["other = V3(1, 0, 0) if Abs(v.x) < 0.9 else V3(0, 1, 0)", "return Normalize(Cross(v, other))"],
   sizes=(3,), doc="A unit vector perpendicular to v (any one).")
fn("MoveTowards", F, [("current", "T"), ("target", "T"), ("maxDelta", "T")], "T",
   ["return target if Abs(target - current) <= maxDelta else current + Sign(target - current) * maxDelta"],
   doc="current moved toward target by at most maxDelta, never past it.")
fn("MoveTowards", F, [("current", "V"), ("target", "V"), ("maxDistance", "T")], "V",
   ["d = target - current", "len = Length(d)", "return target if len <= maxDistance or len == 0 else current + d * (maxDistance / len)"],
   doc="The vector form: along a straight line.")
fn("DeltaAngle", F, [("fromAngle", "T"), ("toAngle", "T")], "T", ["d = Mod(toAngle - fromAngle, Tau)", "return d - Tau if d > Pi else d"],
   doc="The shortest signed difference between two angles in radians, in [-Pi, Pi].")
fn("WrapAngle", F, [("radians", "T")], "T", ["return Mod(radians + Pi, Tau) - Pi"], doc="An angle wrapped into [-Pi, Pi).")
SMOOTH_CS = ["smoothTime = Max(@{0.0001}, smoothTime);", "var omega = @{2} / smoothTime;", "var x = omega * deltaTime;",
             "var exp = @{1} / (@{1} + x + @{0.48} * x * x + @{0.235} * x * x * x);"]
SMOOTH_KT = ["val st = max(@{0.0001}, smoothTime)", "val omega = @{2} / st", "val x = omega * deltaTime",
             "val exp = @{1} / (@{1} + x + @{0.48} * x * x + @{0.235} * x * x * x)"]
fn("SmoothDamp", F, [("current", "T"), ("target", "T"), ("velocity", "T@"), ("smoothTime", "T"), ("deltaTime", "T"), ("maxSpeed", "T", "inf")], "T", raw={
    "cs": SMOOTH_CS + ["var maxChange = maxSpeed * smoothTime;", "var change = Clamp(current - target, -maxChange, maxChange);",
                       "var clampedTarget = current - change;", "var temp = (velocity + omega * change) * deltaTime;",
                       "velocity = (velocity - omega * temp) * exp;", "var output = clampedTarget + (change + temp) * exp;",
                       "if ((target - current > @{0}) == (output > target)) { output = target; velocity = (output - target) / deltaTime; }", "return output;"],
    "kt": SMOOTH_KT + ["val maxChange = maxSpeed * st", "val change = clamp(current - target, -maxChange, maxChange)",
                       "val clampedTarget = current - change", "val temp = (velocity.value + omega * change) * deltaTime",
                       "velocity.value = (velocity.value - omega * temp) * exp", "var output = clampedTarget + (change + temp) * exp",
                       "if ((target - current > @{0}) == (output > target)) { output = target; velocity.value = (output - target) / deltaTime }", "return output"]},
   doc="A critically damped spring toward target: frame-rate independent, never overshoots. velocity is its state.")
fn("SmoothDamp", F, [("current", "V"), ("target", "V"), ("velocity", "V@"), ("smoothTime", "T"), ("deltaTime", "T"), ("maxSpeed", "T", "inf")], "V", raw={
    "cs": SMOOTH_CS + ["var change = ClampLength(current - target, maxSpeed * smoothTime);", "var clampedTarget = current - change;",
                       "var temp = (velocity + change * omega) * deltaTime;", "velocity = (velocity - temp * omega) * exp;",
                       "var output = clampedTarget + (change + temp) * exp;",
                       "if (Dot(target - current, output - target) > @{0}) { output = target; velocity = (output - target) / deltaTime; }", "return output;"],
    "kt": SMOOTH_KT + ["val change = clampLength(current - target, maxSpeed * st)", "val clampedTarget = current - change",
                       "val temp = (velocity.value + change * omega) * deltaTime", "velocity.value = (velocity.value - temp * omega) * exp",
                       "var output = clampedTarget + (change + temp) * exp",
                       "if (dot(target - current, output - target) > @{0}) { output = target; velocity.value = (output - target) / deltaTime }", "return output"]})

# The constants (glm's gtc/constants), as C++ has them: each the float or double nearest the true value.
import math, struct


def f32(x):
    return struct.unpack("f", struct.pack("f", x))[0]


CONSTANTS = [  # name, double value, float value (computed as C++ does: T(2) * pi_v<float> and so on), doc
    ("Pi", math.pi, f32(math.pi), None),
    ("TwoPi", 2 * math.pi, 2 * f32(math.pi), None),
    ("Tau", 2 * math.pi, 2 * f32(math.pi), "TwoPi by its other name: a full turn."),
    ("HalfPi", math.pi / 2, f32(math.pi) / 2, None),
    ("QuarterPi", math.pi / 4, f32(math.pi) / 4, None),
    ("OneOverPi", 1 / math.pi, f32(1 / math.pi), None),
    ("TwoOverPi", 2 / math.pi, 2 * f32(1 / math.pi), None),
    ("E", math.e, f32(math.e), None),
    ("GoldenRatio", (1 + math.sqrt(5)) / 2, f32((1 + math.sqrt(5)) / 2), None),
    ("RootTwo", math.sqrt(2), f32(math.sqrt(2)), None),
    ("RootThree", math.sqrt(3), f32(math.sqrt(3)), None),
    ("Ln2", math.log(2), f32(math.log(2)), None),
    ("Ln10", math.log(10), f32(math.log(10)), None),
    ("Epsilon", 1e-5, f32(1e-5), "The tolerance ApproxEqual and EpsilonEqual use when given none."),
]
