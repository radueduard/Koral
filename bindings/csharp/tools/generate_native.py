#!/usr/bin/env python3
"""
Writes the P/Invokes from the C interfaces: src/Koral/Generated/KoralNative.g.cs from include/koral_c.h,
and src/Koral.UI/Generated/KuiNative.g.cs from the UI module's koralUI_c.h. Every function as a P/Invoke
of the same name, and every struct as a blittable struct of the same layout (bool is one byte, as C's is
where Koral builds). `--check` (a ctest) fails when either file is stale.
"""
import pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
SRC = pathlib.Path(__file__).resolve().parents[1] / "src"

# What is generated from which header: the macro its functions are marked with, their prefix, and the
# class and namespace they go in.
TARGETS = [
    {"header": ROOT / "include" / "koral_c.h", "out": SRC / "Koral" / "Generated" / "KoralNative.g.cs",
     "api": "KORAL_API", "prefix": "koral_", "cls": "KoralNative", "ns": "Koral.Native"},
    # kmath's C interface. Its structs are the public math types' own layouts, so they are not generated:
    # each maps onto the C# type (TYPES below), which the P/Invokes then take and return directly.
    {"header": ROOT / "include" / "koral_math_c.h", "out": SRC / "Koral" / "Generated" / "KoralMathNative.g.cs",
     "api": "KORAL_API", "prefix": "koral_", "cls": "KoralMathNative", "ns": "Koral.Native", "library": "KoralNative.Library"},
    # The same again for the tests, which check the managed kmath against the native one through it (the
    # library's own copy is internal, and opening the internals to the tests changes how they must override).
    {"header": ROOT / "include" / "koral_math_c.h", "out": SRC.parent / "tests" / "Koral.Tests" / "Generated" / "KoralMathNative.g.cs",
     "api": "KORAL_API", "prefix": "koral_", "cls": "KoralMathNative", "ns": "Koral.Tests.Native", "library": '"Koral"'},
    {"header": ROOT / "modules" / "ui" / "include" / "koralUI_c.h", "out": SRC / "Koral.UI" / "Generated" / "KuiNative.g.cs",
     "api": "KUI_API", "prefix": "kui_", "cls": "KuiNative", "ns": "Koral.UI.Native"},
    {"header": ROOT / "modules" / "net" / "include" / "koralNet_c.h", "out": SRC / "Koral.Net" / "Generated" / "KnetNative.g.cs",
     "api": "KNET_API", "prefix": "knet_", "cls": "KnetNative", "ns": "Koral.Net.Native"},
    {"header": ROOT / "modules" / "net" / "include" / "koralNet_c.h", "out": SRC.parent / "tests" / "Koral.Tests" / "Generated" / "KnetNative.g.cs",
     "api": "KNET_API", "prefix": "knet_", "cls": "KnetNative", "ns": "Koral.Tests.Native", "library": '"koral-net"'},
]

SCALARS = {"void": "void", "bool": "byte", "uint8_t": "byte", "uint16_t": "ushort", "uint32_t": "uint", "int32_t": "int", "uint64_t": "ulong", "int64_t": "long",
           "size_t": "nuint", "int": "int", "float": "float", "double": "double", "char": "byte",
           "KoralStatus": "int", "KoralLogLevel": "int", "KoralResourceKind": "int", "KoralPlatform": "int"}
HAND_WRITTEN = {"KoralClearColor"}   # the union
# C structs that are a public C# type's exact layout: not generated, and named as that type.
TYPES = {**{f"Koral{p}Vec{n}": f"{p}Vec{n}" for p in ("", "D", "I", "U", "B") for n in (2, 3, 4)},
         **{f"Koral{p}Mat{c}x{r}" if c != r else f"Koral{p}Mat{c}": f"{p}Mat{c}x{r}" if c != r else f"{p}Mat{c}"
            for p in ("", "D") for c in (2, 3, 4) for r in (2, 3, 4)},
         "KoralQuat": "Quat", "KoralHct": "Hct", "KoralTonalPalette": "TonalPalette", "KoralMaterialScheme": "MaterialScheme",
         "KoralTransform": "Transform", "KoralRay": "Ray", "KoralPlane": "Plane",
         "KoralSphere": "Sphere", "KoralAabb": "Aabb", "KoralObb": "Obb", "KoralTriangle": "Triangle", "KoralAabb2": "Aabb2",
         "KoralFrustum": "Frustum", "KoralTriangleHit": "TriangleHit"}
# Function-pointer typedefs, by name.
TYPEDEFS = {"KoralSceneFactory": "delegate* unmanaged[Cdecl]<byte*, IntPtr, KoralSceneCallbacks>",
            "KoralMainThreadBody": "delegate* unmanaged[Cdecl]<IntPtr, void>"}


def strip(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def csharp_type(ctype, structs, in_struct=False):
    """A C type (without a name) as the C# one. Pointers to opaque handles are IntPtr."""
    ctype = ctype.replace("const ", "").strip()
    stars = ctype.count("*")
    base = ctype.replace("*", "").strip()
    if base in TYPEDEFS and stars == 0:
        return TYPEDEFS[base]
    if base in TYPES:
        return TYPES[base] + "*" * stars
    if stars == 0:
        if base in SCALARS:
            return SCALARS[base]
        if base in structs:
            return base
        raise SystemExit(f"unknown type {ctype}")
    if base == "char" and stars == 1:
        return "IntPtr" if in_struct else "byte*"
    if base in SCALARS and base not in ("KoralStatus", "KoralLogLevel", "KoralResourceKind", "KoralPlatform"):
        return SCALARS[base] + "*" * stars
    if base in structs or base == "KoralClearColor":
        return base + "*" * stars
    return "IntPtr" + "*" * (stars - 1)   # a handle


def function_pointer(ret, params, structs):
    types = [csharp_type(re.sub(r"\b\w+(\[\d*\])?$", lambda m: "*" if m.group(1) else "", p.strip()).strip() or p, structs)
             if p.strip() != "void" else None for p in params.split(",")]
    types = [t for t in types if t]
    return "delegate* unmanaged[Cdecl]<" + ", ".join(types + [csharp_type(ret, structs)]) + ">"


KEYWORDS = {"string", "object", "base", "params", "in", "out", "ref", "as", "event", "fixed", "checked", "interface",
            "delegate", "operator", "is", "lock", "default", "internal", "public", "private", "protected", "new", "this"}


def escape(name):
    return "@" + name if name in KEYWORDS else name


def split_declarator(decl):
    """'const float from[3]' -> ('const float*', 'from'); 'KoralScene* scene' -> ('KoralScene*', 'scene')."""
    decl = decl.strip()
    array = re.search(r"\[(\d*)\]$", decl)
    if array:
        decl = decl[:array.start()]
    m = re.match(r"(.*?)(\w+)$", decl)
    ctype, name = m.group(1).strip(), m.group(2)
    if array:
        ctype += "*"
    return ctype, name


def parse_structs(text):
    structs = {}
    for m in re.finditer(r"typedef\s+struct\s+(\w+)\s*\{(.*?)\}\s*(\w+)\s*;", text, re.S):
        structs[m.group(1)] = m.group(2)
    for m in re.finditer(r"(?<!typedef )struct\s+(\w+)\s*\{(.*?)\}\s*;", text, re.S):
        structs.setdefault(m.group(1), m.group(2))
    return structs


def emit_struct(name, body, structs):
    lines = ["[StructLayout(LayoutKind.Sequential)]", f"internal unsafe struct {name}", "{"]
    for field in [f.strip() for f in body.split(";") if f.strip()]:
        fp = re.match(r"(.+?)\(\s*\*\s*(\w+)\s*\)\s*\((.*)\)$", field, re.S)
        if fp:
            lines.append(f"    public {function_pointer(fp.group(1).strip(), fp.group(3), structs)} {escape(fp.group(2))};")
            continue
        declarators = [d.strip() for d in field.split(",")]
        head = re.match(r"(.*?)(\w+(\[\d+\])?)$", declarators[0])
        base = head.group(1).strip()
        for d in [head.group(2)] + declarators[1:]:
            m = re.match(r"(\w+)(?:\[(\d+)\])?$", d)
            cs = csharp_type(base, structs, in_struct=True)
            if m.group(2):
                lines.append(f"    public fixed {cs} {escape(m.group(1))}[{m.group(2)}];")
            else:
                lines.append(f"    public {cs} {escape(m.group(1))};")
    lines.append("}")
    return lines


def generate(target):
    text = strip(target["header"].read_text())
    structs = parse_structs(text)
    source = target["header"].relative_to(ROOT).as_posix()
    out = [f"// <auto-generated> by tools/generate_native.py from {source}: do not edit. </auto-generated>",
           "#nullable enable", "#pragma warning disable CS1591, CS0649, IDE1006", "",
           "using System.Runtime.InteropServices;", "", f"namespace {target['ns']};", ""]
    for enum in re.findall(r"typedef\s+enum\s+\w*\s*\{[^}]*\}\s*(\w+)\s*;", text):
        SCALARS.setdefault(enum, "int")   # enumerations cross as their int value
    for name, body in structs.items():
        if name in HAND_WRITTEN or name in TYPES:
            continue
        out += emit_struct(name, body, structs) + [""]
    out += [f"internal static unsafe partial class {target['cls']}", "{"]
    if "library" in target:
        out.append(f"    private const string Library = {target['library']};")
    for m in re.finditer(target["api"] + r"\s+(.+?)\b(" + target["prefix"] + r"\w+)\s*\((.*?)\)\s*;", text, re.S):
        ret, name, params = m.group(1).strip(), m.group(2), " ".join(m.group(3).split())
        args = []
        if params and params != "void":
            depth, cur, parts = 0, "", []
            for ch in params:
                depth += ch == "("
                depth -= ch == ")"
                if ch == "," and depth == 0:
                    parts.append(cur); cur = ""
                else:
                    cur += ch
            parts.append(cur)
            for p in parts:
                fp = re.match(r"(.+?)\(\s*\*\s*(\w+)\s*\)\s*\((.*)\)$", p.strip())
                if fp:
                    args.append(f"{function_pointer(fp.group(1).strip(), fp.group(3), structs)} {fp.group(2)}")
                    continue
                ctype, pname = split_declarator(p)
                bare = " ".join(ctype.split())
                if bare == "const char*":
                    cs = "string?"          # UTF-8, copied by the callee
                elif bare == "const char* const*":
                    cs = "string[]?"
                else:
                    cs = csharp_type(ctype, structs)
                args.append(f"{cs} {escape(pname)}")
        cret = csharp_type(ret, structs)
        attribute = "[LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]" if any(a.startswith("string") for a in args) else "[LibraryImport(Library)]"
        out.append(f'    {attribute} internal static partial {cret} {name}({", ".join(args)});')
    out += ["}", ""]
    return "\n".join(out)


if __name__ == "__main__":
    stale = False
    for target in TARGETS:
        text, out = generate(target), target["out"]
        if "--check" in sys.argv:
            if not out.exists() or out.read_text() != text:
                print(f"{out} is stale: run {pathlib.Path(__file__).name}")
                stale = True
        else:
            out.parent.mkdir(parents=True, exist_ok=True)
            out.write_text(text)
            print(f"wrote {out}")
    if "--check" in sys.argv:
        if stale:
            sys.exit(1)
        print("up to date")
