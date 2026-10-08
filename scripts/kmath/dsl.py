"""
The little language kmath's function bodies are written in once, for C# and for Kotlin: Python's expression
syntax, rendered into each. A body is a list of statements:

    "name = expr"            a local (C# `var`, Kotlin `val`)
    "if cond: return expr"   an early return
    "return expr"

In expressions:
  - numbers are the function's scalar type T (2 is 2f for float, 2.0 for double, 2u for uint); `int(3)` is an int;
  - `a if c else b`, and / or / not, the arithmetic, comparison and bit operators as in C;
  - `v.x` is a component (C#'s v.X);
  - `std.sqrt(x)` and friends are the platform's scalar functions for T (see LIB);
  - `V(...)`, `V2(...)`, `V3(...)`, `V4(...)` construct a vector of T (V is the function's own size N);
    `BV(...)` a bool vector; `S(x)` converts to T; `i32(x)`, `u32(x)`, `f32(x)`, `f64(x)` to those types;
  - any other call is a kmath function, PascalCase here (Kotlin's lowerCamelCase there).
"""
import ast

SCALAR = {   # code: (C#, Kotlin)
    "f": ("float", "Float"), "d": ("double", "Double"), "i": ("int", "Int"), "u": ("uint", "UInt"), "b": ("bool", "Boolean"),
}
VEC_PREFIX = {"f": "", "d": "D", "i": "I", "u": "U", "b": "B"}


def vec_name(t, n):
    return f"{VEC_PREFIX[t]}Vec{n}"


def lower_camel(name):
    return name[0].lower() + name[1:]


def literal(value, t, lang):
    if t in ("f", "d"):
        text = repr(float(value))
        if "e" in text or "inf" in text:
            text = repr(float(value))
        if t == "f":
            if text.endswith(".0"):
                text = text[:-2]
            return f"{text}f"
        return text if "." in text or "e" in text else text + ".0"
    if t == "u":
        return f"{int(value)}u"
    if t == "b":
        return ("true" if value else "false")
    return str(int(value))


# The platform's scalar functions: name -> (C# for float with {T} = float|double, Kotlin).
LIB = {
    "sqrt": ("{T}.Sqrt({0})", "kotlin.math.sqrt({0})"),
    "sin": ("{T}.Sin({0})", "kotlin.math.sin({0})"),
    "cos": ("{T}.Cos({0})", "kotlin.math.cos({0})"),
    "tan": ("{T}.Tan({0})", "kotlin.math.tan({0})"),
    "asin": ("{T}.Asin({0})", "kotlin.math.asin({0})"),
    "acos": ("{T}.Acos({0})", "kotlin.math.acos({0})"),
    "atan": ("{T}.Atan({0})", "kotlin.math.atan({0})"),
    "atan2": ("{T}.Atan2({0}, {1})", "kotlin.math.atan2({0}, {1})"),
    "sinh": ("{T}.Sinh({0})", "kotlin.math.sinh({0})"),
    "cosh": ("{T}.Cosh({0})", "kotlin.math.cosh({0})"),
    "tanh": ("{T}.Tanh({0})", "kotlin.math.tanh({0})"),
    "asinh": ("{T}.Asinh({0})", "kotlin.math.asinh({0})"),
    "acosh": ("{T}.Acosh({0})", "kotlin.math.acosh({0})"),
    "atanh": ("{T}.Atanh({0})", "kotlin.math.atanh({0})"),
    "pow": ("{T}.Pow({0}, {1})", "({0}).pow({1})"),
    "exp": ("{T}.Exp({0})", "kotlin.math.exp({0})"),
    "log": ("{T}.Log({0})", "kotlin.math.ln({0})"),
    "exp2": ("{T}.Exp2({0})", "({two}).pow({0})"),
    "log2": ("{T}.Log2({0})", "kotlin.math.log2({0})"),
    "floor": ("{T}.Floor({0})", "kotlin.math.floor({0})"),
    "ceil": ("{T}.Ceiling({0})", "kotlin.math.ceil({0})"),
    "trunc": ("{T}.Truncate({0})", "kotlin.math.truncate({0})"),
    "round": ("{T}.Round({0}, MidpointRounding.AwayFromZero)", "roundAway({0})"),
    "rint": ("{T}.Round({0})", "kotlin.math.round({0})"),
    "fma": ("{T}.FusedMultiplyAdd({0}, {1}, {2})", "Math.fma({0}, {1}, {2})"),
    "ldexp": ("{T}.ScaleB({0}, {1})", "Math.scalb({0}, {1})"),
    "isnan": ("{T}.IsNaN({0})", "({0}).isNaN()"),
    "isinf": ("{T}.IsInfinity({0})", "({0}).isInfinite()"),
    "isfinite": ("{T}.IsFinite({0})", "({0}).isFinite()"),
    "inf": ("{T}.PositiveInfinity", "{K}.POSITIVE_INFINITY"),
    "machine_epsilon": ("{T}.Epsilon_", "{K}.Epsilon_"),   # replaced per type below
}
MACHINE_EPSILON = {"f": ("1.1920929E-07f", "1.1920929E-07f"), "d": ("2.220446049250313E-16", "2.220446049250313E-16")}


class Renderer(ast.NodeVisitor):
    def __init__(self, lang, t, n=None, locals_types=None):
        self.lang, self.t, self.n = lang, t, n

    def render(self, text):
        return self.expr(ast.parse(text, mode="eval").body)

    # ---- expressions ----

    def expr(self, node, t=None):
        t = t or self.t
        m = getattr(self, "e_" + type(node).__name__, None)
        if not m:
            raise SystemExit(f"dsl: unsupported {ast.dump(node)}")
        return m(node, t)

    def e_Constant(self, node, t):
        if isinstance(node.value, bool):
            return "true" if node.value else "false"
        return literal(node.value, t, self.lang)

    CONSTANTS = {"Pi", "TwoPi", "Tau", "HalfPi", "QuarterPi", "OneOverPi", "TwoOverPi", "E", "GoldenRatio", "RootTwo", "RootThree",
                 "Ln2", "Ln10", "Epsilon"}

    def e_Name(self, node, t):
        if node.id in self.CONSTANTS and self.t == "d":
            return "D" + node.id
        return node.id

    def e_Attribute(self, node, t):
        base = self.expr(node.value)
        if isinstance(node.value, ast.Name) and node.value.id == "std":
            return self.lib(node.attr, [])
        attr = node.attr
        if self.lang == "cs" and attr in ("x", "y", "z", "w"):
            attr = attr.upper()
        return f"{base}.{attr}"

    OPS = {ast.Add: "+", ast.Sub: "-", ast.Mult: "*", ast.Div: "/", ast.Mod: "%", ast.BitAnd: "&", ast.BitOr: "|",
           ast.BitXor: "^", ast.LShift: "<<", ast.RShift: ">>"}
    KT_OPS = {ast.BitAnd: "and", ast.BitOr: "or", ast.BitXor: "xor", ast.LShift: "shl", ast.RShift: "shr"}

    def e_BinOp(self, node, t):
        l, r = self.expr(node.left, t), self.expr(node.right, t)
        if self.lang == "kt" and type(node.op) in self.KT_OPS:
            return f"({l} {self.KT_OPS[type(node.op)]} {r})"
        return f"({l} {self.OPS[type(node.op)]} {r})"

    def e_UnaryOp(self, node, t):
        v = self.expr(node.operand, t)
        if isinstance(node.op, ast.USub):
            if isinstance(node.operand, ast.Constant):
                return f"-{v}"
            return f"-{v}" if v.startswith("(") or v.replace("_", "").replace(".", "").isalnum() else f"-({v})"
        if isinstance(node.op, ast.Not):
            return f"!({v})"
        if isinstance(node.op, ast.Invert):
            return f"({v}).inv()" if self.lang == "kt" else f"~({v})"
        raise SystemExit("dsl: unary")

    CMP = {ast.Lt: "<", ast.LtE: "<=", ast.Gt: ">", ast.GtE: ">=", ast.Eq: "==", ast.NotEq: "!="}

    def e_Compare(self, node, t):
        assert len(node.ops) == 1
        return f"({self.expr(node.left, t)} {self.CMP[type(node.ops[0])]} {self.expr(node.comparators[0], t)})"

    def e_BoolOp(self, node, t):
        op = " && " if isinstance(node.op, ast.And) else " || "
        return "(" + op.join(self.expr(v, t) for v in node.values) + ")"

    def e_IfExp(self, node, t):
        c, a, b = self.expr(node.test, t), self.expr(node.body, t), self.expr(node.orelse, t)
        if self.lang == "kt":
            return f"(if ({c}) {a} else {b})"
        return f"({c} ? {a} : {b})"

    def scalar_type(self, t):
        return SCALAR[t][0 if self.lang == "cs" else 1]

    def lib(self, name, args):
        cs, kt = LIB[name]
        if name == "machine_epsilon":
            return MACHINE_EPSILON[self.t][0 if self.lang == "cs" else 1]
        fmt = cs if self.lang == "cs" else kt
        two = literal(2, self.t, self.lang)
        return fmt.format(*args, T=self.scalar_type(self.t), K=self.scalar_type(self.t), two=two)

    CASTS = {"i32": ("i", "int", "toInt()"), "u32": ("u", "uint", "toUInt()"), "f32": ("f", "float", "toFloat()"),
             "f64": ("d", "double", "toDouble()")}

    def e_Call(self, node, t):
        f = node.func
        if isinstance(f, ast.Attribute) and isinstance(f.value, ast.Name) and f.value.id == "std":
            return self.lib(f.attr, [self.expr(a, t) for a in node.args])
        name = f.id
        if name == "int":
            return literal(node.args[0].value, "i", self.lang)
        if name in self.CASTS:
            code, cs, kt = self.CASTS[name]
            inner = self.expr(node.args[0], t)
            return f"(({cs})({inner}))" if self.lang == "cs" else f"({inner}).{kt}"
        if name == "S":
            inner = self.expr(node.args[0], t)
            ty = self.scalar_type(self.t)
            if self.lang == "cs":
                return f"(({ty})({inner}))"
            return f"({inner}).to{ty}()"
        if name in ("V", "V2", "V3", "V4", "BV"):
            n = self.n if name in ("V", "BV") else int(name[1])
            ty = vec_name("b" if name == "BV" else self.t, n)
            args = ", ".join(self.expr(a, t) for a in node.args)
            return f"new {ty}({args})" if self.lang == "cs" else f"{ty}({args})"
        args = ", ".join(self.expr(a, t) for a in node.args)
        if name[0].isupper() and self.lang == "kt" and not name.startswith(("Vec", "DVec", "IVec", "UVec", "BVec", "Mat", "DMat", "Quat")):
            name = lower_camel(name)
        if self.lang == "cs" and name.startswith(("Vec", "DVec", "IVec", "UVec", "BVec", "Mat", "DMat", "Quat")):
            return f"new {name}({args})"
        return f"{name}({args})"


def body(lang, t, n, statements, indent):
    """The statements as a block's lines (C# or Kotlin), at `indent`."""
    r = Renderer(lang, t, n)
    out = []
    pad = " " * indent
    for s in statements:
        s = s.strip()
        if s.startswith("if "):
            cond, ret = s[3:].split(": return ", 1)
            out.append(f"{pad}if ({r.render(cond)}) return {r.render(ret)}{';' if lang == 'cs' else ''}")
        elif s.startswith("return "):
            out.append(f"{pad}return {r.render(s[7:])}{';' if lang == 'cs' else ''}")
        else:
            name, value = s.split(" = ", 1)
            out.append(f"{pad}{'var' if lang == 'cs' else 'val'} {name.strip()} = {r.render(value)}{';' if lang == 'cs' else ''}")
    return out
