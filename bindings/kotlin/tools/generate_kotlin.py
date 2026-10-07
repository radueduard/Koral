#!/usr/bin/env python3
"""
Writes the Kotlin interop from the C interfaces, as tools/generate_native.py does for C#:
koral/src/main/kotlin/koral/interop/KoralNative.kt from include/koral_c.h, and
koral-ui/src/main/kotlin/koral/ui/interop/KuiNative.kt from the UI module's koralUI_c.h.

Every function becomes a downcall through java.lang.foreign (FFM) with a Kotlin wrapper of the same
name: strings in and out are converted, and a struct returned by value takes the allocator it is made
in. Every struct becomes a layout with C's padding, and accessors by field name. `--check` (a ctest)
fails when a file is stale.
"""
import pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
KOTLIN = pathlib.Path(__file__).resolve().parents[1]

TARGETS = [
    {"header": ROOT / "include" / "koral_c.h", "out": KOTLIN / "koral" / "src" / "main" / "kotlin" / "koral" / "interop" / "KoralNative.kt",
     "api": "KORAL_API", "prefix": "koral_", "obj": "KoralNative", "layouts": "KoralLayouts", "pkg": "koral.interop",
     "library": "Native.koral", "imports": []},
    # kmath's C interface: what the Kotlin math is tested against, and what its bulk operations call.
    {"header": ROOT / "include" / "koral_math_c.h", "out": KOTLIN / "koral" / "src" / "main" / "kotlin" / "koral" / "interop" / "KoralMathNative.kt",
     "api": "KORAL_API", "prefix": "koral_", "obj": "KoralMathNative", "layouts": "KoralMathLayouts", "pkg": "koral.interop",
     "library": "Native.koral", "imports": []},
    {"header": ROOT / "modules" / "ui" / "include" / "koralUI_c.h", "out": KOTLIN / "koral-ui" / "src" / "main" / "kotlin" / "koral" / "ui" / "interop" / "KuiNative.kt",
     "api": "KUI_API", "prefix": "kui_", "obj": "KuiNative", "layouts": "KuiLayouts", "pkg": "koral.ui.interop",
     "library": "Native.koralUi", "imports": ["koral.interop.Native", "koral.interop.KoralLayouts"]},
]

# C scalars: (FFM layout, Kotlin type, size).
SCALARS = {
    "bool": ("JAVA_BOOLEAN", "Boolean", 1), "char": ("JAVA_BYTE", "Byte", 1), "uint8_t": ("JAVA_BYTE", "Byte", 1),
    "int32_t": ("JAVA_INT", "Int", 4), "uint32_t": ("JAVA_INT", "Int", 4), "int": ("JAVA_INT", "Int", 4),
    "float": ("JAVA_FLOAT", "Float", 4), "double": ("JAVA_DOUBLE", "Double", 8),
    "uint16_t": ("JAVA_SHORT", "Short", 2), "int64_t": ("JAVA_LONG", "Long", 8), "uint64_t": ("JAVA_LONG", "Long", 8), "size_t": ("JAVA_LONG", "Long", 8),
}
ENUMS = {"KoralStatus", "KoralLogLevel", "KoralResourceKind", "KoralPlatform"}
POINTER = ("ADDRESS", "MemorySegment", 8)
KEYWORDS = {"in", "is", "as", "object", "fun", "val", "var", "when", "typealias", "interface", "class", "package", "type"}
# Functions that also get a `<name>_at` overload taking their strings as C strings already made, for whoever
# calls them a great many times and keeps the strings in an arena of their own.
AT_OVERLOADS = {"kui_widget_set_key"}


def strip(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    # The one union (KoralClearColor's value: four floats, ints or uints) is laid out as four ints.
    return re.sub(r"union\s*\{[^}]*\}\s*(\w+)\s*;", r"uint32_t \1[4];", text)


def ident(name):
    return f"`{name}`" if name in KEYWORDS else name


def split_top(text, sep=","):
    depth, cur, parts = 0, "", []
    for ch in text:
        depth += ch in "({"
        depth -= ch in ")}"
        if ch == sep and depth == 0:
            parts.append(cur); cur = ""
        else:
            cur += ch
    if cur.strip():
        parts.append(cur)
    return parts


class Structs:
    """The header's structs, with C's layout: each field's offset, the padding between, alignment."""

    def __init__(self, text, known):
        self.order, self.fields = [], {}
        self.known = known   # structs of other headers (by name -> (size, align))
        # Function-pointer typedefs (KoralSceneFactory): pointers.
        self.function_pointers = set(re.findall(r"typedef\s+[^;]*?\(\s*\*\s*(\w+)\s*\)\s*\(", text))
        for m in re.finditer(r"typedef\s+struct\s+(\w+)\s*\{(.*?)\}\s*(\w+)\s*;", text, re.S):
            self.add(m.group(1), m.group(2))

    def add(self, name, body):
        fields = []
        for decl in [d.strip() for d in body.split(";") if d.strip()]:
            fp = re.match(r"(.+?)\(\s*\*\s*(\w+)\s*\)\s*\((.*)\)$", decl, re.S)
            if fp:
                fields.append((fp.group(2), POINTER, 1))
                continue
            declarators = split_top(decl)
            head = re.match(r"(.*?)(\w+(\[\d+\])?)$", declarators[0].strip())
            base = head.group(1).strip()
            for d in [head.group(2)] + [x.strip() for x in declarators[1:]]:
                m = re.match(r"(\w+)(?:\[(\d+)\])?$", d)
                fields.append((m.group(1), self.type_of(base), int(m.group(2)) if m.group(2) else 1))
        self.order.append(name)
        self.fields[name] = fields

    def type_of(self, ctype):
        ctype = ctype.replace("const ", "").strip()
        if "*" in ctype or ctype in self.function_pointers:
            return POINTER
        if ctype in SCALARS:
            return SCALARS[ctype]
        if ctype in ENUMS:
            return ("JAVA_INT", "Int", 4)
        if ctype in self.fields or ctype in self.known:
            return ("STRUCT:" + ctype, "MemorySegment", None)
        raise SystemExit(f"unknown type {ctype}")

    def size_align(self, t):
        layout = t[0]
        if layout.startswith("STRUCT:"):
            name = layout[7:]
            return self.known[name] if name in self.known else self.layout(name)[1:]
        return t[2], t[2]

    def layout(self, name):
        """(members, size, align): the FFM members with padding, and the struct's size and alignment."""
        members, offset, align = [], 0, 1
        for field, t, count in self.fields[name]:
            size, a = self.size_align(t)
            align = max(align, a)
            if offset % a:
                pad = a - offset % a
                members.append(f"MemoryLayout.paddingLayout({pad})")
                offset += pad
            base = t[0][7:] if t[0].startswith("STRUCT:") else t[0]
            if t[0].startswith("STRUCT:"):
                base = self.qualified(t[0][7:])
            element = base if count == 1 else f"MemoryLayout.sequenceLayout({count}, {base})"
            members.append(f'{element}.withName("{field}")')
            offset += size * count
        if offset % align:
            members.append(f"MemoryLayout.paddingLayout({align - offset % align})")
            offset += align - offset % align
        return members, offset, align

    layouts_name = "Layouts"
    other_layouts = None

    def qualified(self, struct):
        return f"{self.other_layouts}.{struct}" if struct in self.known else struct


def ffm(t, structs):
    """A layout as a function descriptor names it: a struct by its layouts object, whichever header it is in."""
    if t[0].startswith("STRUCT:"):
        name = t[0][7:]
        return structs.qualified(name) if name in structs.known else f"{structs.own_layouts}.{name}"
    return t[0]


def generate(target, known):
    text = strip(target["header"].read_text())
    ENUMS.update(re.findall(r"typedef\s+enum\s+\w*\s*\{[^}]*\}\s*(\w+)\s*;", text))   # each crosses as its int
    structs = Structs(text, known)
    structs.other_layouts = "KoralLayouts"
    structs.own_layouts = target["layouts"]
    lines = [f"// <auto-generated> by tools/generate_kotlin.py from {target['header'].relative_to(ROOT).as_posix()}: do not edit. </auto-generated>",
             '@file:Suppress("FunctionName", "LocalVariableName", "unused", "ObjectPropertyName", "PropertyName")', "",
             f"package {target['pkg']}", "",
             "import java.lang.foreign.Arena", "import java.lang.foreign.FunctionDescriptor", "import java.lang.foreign.MemoryLayout",
             "import java.lang.foreign.MemorySegment", "import java.lang.foreign.SegmentAllocator", "import java.lang.foreign.StructLayout",
             "import java.lang.foreign.ValueLayout.*", "import java.lang.invoke.MethodHandle"]
    lines += [f"import {i}" for i in target["imports"]]
    lines += ["", "/** The C structs' layouts, with C's padding. */", f"object {target['layouts']} {{"]
    sizes = {}
    for name in structs.order:
        members, size, align = structs.layout(name)
        sizes[name] = (size, align)
        lines.append(f"    val {name}: StructLayout = MemoryLayout.structLayout(")
        lines += [f"        {m}," for m in members]
        lines.append("    )")
    lines.append("}")
    lines.append("")

    lines += [f"object {target['obj']} {{",
              "    // A call to a void function is a statement, which Kotlin's call site types as void, as the handle is;",
              "    // anything else is cast, which types the call site's return.",
              "    private fun handle(name: String, descriptor: FunctionDescriptor): MethodHandle {",
              f"        val symbol = {target['library']}.find(name).orElseThrow {{ UnsatisfiedLinkError(\"{target['obj']}: no \" + name) }}",
              "        return Native.linker.downcallHandle(symbol, descriptor)",
              "    }", ""]
    for m in re.finditer(target["api"] + r"\s+(.+?)\b(" + target["prefix"] + r"\w+)\s*\((.*?)\)\s*;", text, re.S):
        ret, name, params = m.group(1).strip(), m.group(2), " ".join(m.group(3).split())
        args = []
        if params and params != "void":
            for p in split_top(params):
                p = p.strip()
                fp = re.match(r"(.+?)\(\s*\*\s*(\w+)\s*\)\s*\((.*)\)$", p)
                if fp:
                    args.append((fp.group(2), "fnptr", POINTER))
                    continue
                arr = re.search(r"\[(\d*)\]$", p)
                core = p[:arr.start()] if arr else p
                mm = re.match(r"(.*?)(\w+)$", core)
                ctype, pname = mm.group(1).strip(), mm.group(2)
                if arr:
                    ctype += "*"
                bare = " ".join(ctype.split())
                if bare == "const char*":
                    args.append((pname, "string", POINTER))
                else:
                    args.append((pname, "value", structs.type_of(ctype)))
        rbare = " ".join(ret.split())
        if rbare == "void":
            rt = None
        elif rbare == "const char*":
            rt = ("string", POINTER)
        else:
            rt = ("value", structs.type_of(ret))
        struct_return = rt is not None and rt[1][0].startswith("STRUCT:")

        layouts = [ffm(t, structs) for _, _, t in args]
        if rt is None:
            descriptor = f"FunctionDescriptor.ofVoid({', '.join(layouts)})"
        else:
            descriptor = f"FunctionDescriptor.of({', '.join([ffm(rt[1], structs)] + layouts)})"
        h = f"h_{name}"
        lines.append(f"    private val {h} by lazy {{ handle(\"{name}\", {descriptor}) }}")

        kparams, call, strings = [], [], False
        if struct_return:
            kparams.append("allocator: SegmentAllocator")
            call.append("allocator")
        for pname, kind, t in args:
            pid = ident(pname)
            if kind == "string":
                kparams.append(f"{pid}: String?")
                call.append(f"Native.cString(a, {pid})")
                strings = True
            else:
                kparams.append(f"{pid}: {t[1]}")
                call.append(pid)
        invoke = f"{h}.invokeExact({', '.join(call)})"
        if rt is None:
            body = f"{invoke}"
            rtype = "Unit"
            expr = f"{{ {body} }}" if False else None
        elif rt[0] == "string":
            body = f"Native.kString({invoke} as MemorySegment)"
            rtype = "String"
        else:
            body = f"{invoke} as {rt[1][1]}"
            rtype = rt[1][1]
        sig = f"    fun {name}({', '.join(kparams)}): {rtype}"
        if strings:
            inner = f"{invoke}" if rt is None else body
            if rt is None:
                lines.append(f"{sig} {{ Arena.ofConfined().use {{ a -> {invoke}; Unit }} }}")
            else:
                lines.append(f"{sig} = Arena.ofConfined().use {{ a -> {inner} }}")
            if name in AT_OVERLOADS:
                raw = [f"{ident(p)}: MemorySegment" if kind == "string" else kp for (p, kind, _), kp in zip(args, kparams)]
                raw_invoke = f"{h}.invokeExact({', '.join(ident(p) for p, _, _ in args)})"
                lines.append("    /** The same, with the strings already C strings: for whoever calls it a great many times. */")
                lines.append(f"    fun {name}_at({', '.join(raw)}): {rtype} " +
                             (f"{{ {raw_invoke} }}" if rt is None else f"= {raw_invoke} as {rtype}"))
        else:
            if rt is None:
                lines.append(f"{sig} {{ {invoke} }}")
            else:
                lines.append(f"{sig} = {body}")
    lines.append("}")
    lines.append("")
    return "\n".join(lines), sizes


# ---- enumerations, from the C++ headers, with the C# generator's parser ----------------------------------

sys.path.insert(0, str(ROOT / "bindings" / "csharp" / "tools"))
import generate_enums as cs_enums  # noqa: E402

ENUM_TARGETS = [
    # Every enumeration the C# API shares with C++: the same list, so the two bindings cannot drift.
    {"include": ROOT / "include", "pkg": "koral", "cpp": "kor",
     "out": KOTLIN / "koral" / "src" / "main" / "kotlin" / "koral" / "Enums.kt",
     "enums": [(header, owner, name) for header, owner, name, _, _ in cs_enums.ENUMS]},
    {"include": ROOT / "modules" / "ui" / "include" / "kui", "pkg": "koral.ui", "cpp": "kui",
     "out": KOTLIN / "koral-ui" / "src" / "main" / "kotlin" / "koral" / "ui" / "Enums.kt",
     "enums": [(header, owner, name) for header, owner, name, _, _ in cs_enums.UI_ENUMS]},
]


def generate_enums(t):
    lines = ["// <auto-generated> by tools/generate_kotlin.py from the C++ headers: do not edit. </auto-generated>",
             '@file:Suppress("EnumEntryName", "unused")', "", f"package {t['pkg']}", ""]
    for header, owner, name in t["enums"]:
        _, entries, flags = cs_enums.parse(t["include"], header, owner, name)
        kname = f"{owner}{name}" if owner else name
        lines.append(f"/** {t['cpp']}::{owner + '::' if owner else ''}{name}: the same names and values, so a value crosses the C interface as itself. */")
        lines.append(f"enum class {kname}(val value: Int) {{")
        values = {}
        for key, value, alias in entries:
            values[key] = value
            v = values[alias] if alias else value
            lines.append(f"    {key}({v - (1 << 32) if v >= (1 << 31) else v}),")   # a uint32 as the Int with its bits
        lines.append("    ;")
        if flags:
            lines.append(f"    companion object {{")
            lines.append(f"        fun of(value: Int): {kname} = entries.first {{ it.value == value }}")
            lines.append(f"        /** The flags set in [bits]. */")
            lines.append(f"        fun flagsOf(bits: Int): Set<{kname}> = entries.filterTo(mutableSetOf()) {{ it.value != 0 && bits and it.value == it.value }}")
            lines.append(f"    }}")
        else:
            lines.append(f"    companion object {{ fun of(value: Int): {kname} = entries.first {{ it.value == value }} }}")
        lines.append("}")
        lines.append("")
    return "\n".join(lines)


if __name__ == "__main__":
    known, stale = {}, False
    for t in ENUM_TARGETS:
        text, out = generate_enums(t), t["out"]
        if "--check" in sys.argv:
            if not out.exists() or out.read_text() != text:
                print(f"{out} is stale: run {pathlib.Path(__file__).name}")
                stale = True
        else:
            out.parent.mkdir(parents=True, exist_ok=True)
            out.write_text(text)
            print(f"wrote {out}")
    for target in TARGETS:
        text, sizes = generate(target, known)
        known.update(sizes)
        out = target["out"]
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
