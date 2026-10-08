#!/usr/bin/env python3
"""
Describes every builder Koral has, from the C interface: src/core/builders/builders.g.cpp (the table behind
include/builderDescriptions.h, with a function that calls each setting's C function with values) and
src/core/builders/builders.json (the same, for tools; installed as share/Koral/builders.json).

What each setting takes comes from include/koral_c.h. Which C++ enumeration a plain uint32_t stands for comes
from the C interface's implementation (src/core/capi), which converts it: `static_cast<Image::Format>(v)`,
`FlagsOf<Image::Usage>(v)`, on a parameter, an element of one, or a struct's field. A few numbers are no
enumeration and are named in CHOICES. A uint32_t named like an enumeration that is neither is an error, so a new
setting cannot quietly become a bare number.

Run it after changing the C interface's builders; `--check` (a ctest) fails when an output is stale.
"""
import json, pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
HEADER = ROOT / "include" / "koral_c.h"
CAPI = ROOT / "src" / "core" / "capi"
OUT_CPP = ROOT / "src" / "core" / "builders" / "builders.g.cpp"
OUT_JSON = ROOT / "src" / "core" / "builders" / "builders.json"

sys.path.insert(0, str(ROOT / "bindings" / "csharp" / "tools"))
import generate_enums  # noqa: E402  (its parser reads the C++ enumerations, values and all)

# Builders that are no object of their own: a descriptor set follows from a pipeline's split, and a pass builder
# is the frame graph's.
EXCLUDED = {"descriptor_set", "pass"}

# Numbers that choose between a few things without being a C++ enumeration: (function or struct, name) -> choices.
CHOICES = {
    ("koral_framebuffer_builder_set_depth_stencil", "which"): ("DepthStencilAttachment", [("Depth", 0), ("Stencil", 1), ("DepthStencil", 2)]),
    ("KoralClearColor", "scalar_type"): ("ClearColorScalar", [("Float", 0), ("Int", 1), ("UInt", 2)]),
}

# A uint32_t named so is taken to be an enumeration, and must be found to be one (or be a choice).
ENUM_LIKE = re.compile(r"(format|type|mode|usage|stage|lang|filter|op|which|topology|face|factor|rate|kind)$")

SCALARS = {
    "bool": ("eBool", 8), "int32_t": ("eInt", 32), "int64_t": ("eInt", 64), "uint8_t": ("eUInt", 8), "uint32_t": ("eUInt", 32),
    "uint64_t": ("eUInt", 64), "size_t": ("eUInt", 64), "float": ("eFloat", 32), "double": ("eFloat", 64),
}


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", lambda m: " " * len(m.group(0)), text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def camel(snake):
    head, *rest = snake.split("_")
    return head + "".join(w[:1].upper() + w[1:] for w in rest)


def pascal(snake):
    return "".join(w[:1].upper() + w[1:] for w in snake.split("_"))


# ---- the header -------------------------------------------------------------------------------------------

def parse_header():
    raw = HEADER.read_text()
    text = strip_comments(raw)
    resources = set(re.findall(r"typedef\s+KoralResource\s+(Koral\w+)\s*;", text)) | {"KoralResource"}
    structs = {}
    for m in re.finditer(r"typedef\s+struct\s+(\w+)\s*\{", text):
        depth, i = 1, m.end()
        while depth:
            depth += {"{": 1, "}": -1}.get(text[i], 0)
            i += 1
        body = text[m.end():i - 1]
        if "(*" in body:
            continue    # callbacks: a scene's or a pass's, which no builder takes
        structs[m.group(1)] = parse_fields(body)
    functions = {}
    for m in re.finditer(r"KORAL_API\s+([\w\s\*]+?)\s*(koral_\w+)\s*\(([^)]*)\)\s*;", text, re.S):
        ret, name, params = m.group(1).strip(), m.group(2), m.group(3)
        # The doc comment right above it, if any, from the text with comments.
        doc = ""
        before = raw[:m.start()].rstrip()
        if before.endswith("*/"):
            start = before.rfind("/*")
            line_start = raw.rfind("\n", 0, start) + 1
            if raw[line_start:start].strip() == "":
                doc = re.sub(r"^\s*\*\s?", "", before[start + 2:-2].lstrip("*"), flags=re.M).strip()
                doc = re.sub(r"\s+", " ", doc)
        functions[name] = (ret, [p.strip() for p in params.split(",") if p.strip() and p.strip() != "void"], doc)
    return resources, structs, functions


def parse_fields(body):
    """A struct's fields, in order: (name, ctype, array length or None, union fields or None)."""
    fields = []
    # Unions first: `union { float f[4]; int32_t i[4]; } value;` -> one field.
    unions = {}

    def keep_union(m):
        key = f"__union{len(unions)}"
        unions[key] = parse_fields(m.group(1))
        return f"{key} {m.group(2)};"
    body = re.sub(r"union\s*\{(.*?)\}\s*(\w+)\s*;", keep_union, body, flags=re.S)
    for statement in [s.strip() for s in body.split(";") if s.strip()]:
        declarators = [d.strip() for d in statement.split(",")]
        head = re.match(r"(.*?)(\w+(?:\[\d+\])?)$", declarators[0], re.S)
        base = re.sub(r"\s+", " ", head.group(1).strip())
        for d in [head.group(2)] + declarators[1:]:
            m = re.match(r"(\w+)(?:\[(\d+)\])?$", d)
            if base.startswith("__union"):
                fields.append((m.group(1), "union", None, unions[base]))
            else:
                fields.append((m.group(1), base, int(m.group(2)) if m.group(2) else None, None))
    return fields


# ---- the implementation: which enumeration each number is -------------------------------------------------

def parse_casts(known_enums):
    """(function, parameter position) -> enum, and struct field name -> the enums it is converted to, from the C
    interface's conversions."""
    by_param, by_field = {}, {}
    cast = r"(static_cast|FlagsOf)<\s*(?:kor::)?([\w:]+)\s*>\(\s*"
    for path in sorted(CAPI.glob("*.cpp")):
        text = strip_comments(path.read_text())
        for m in re.finditer(r"\b[\w:\*\s]+?\b(koral_\w+)\s*\(([^)]*)\)\s*\{", text):
            name, params = m.group(1), [p.strip() for p in m.group(2).split(",") if p.strip()]
            depth, i = 1, m.end()
            while depth and i < len(text):
                depth += {"{": 1, "}": -1}.get(text[i], 0)
                i += 1
            body = text[m.end():i]
            names = [re.match(r".*?(\w+)(?:\[\d*\])?$", p).group(1) for p in params]
            for c in re.finditer(cast + r"(\w+)\s*(\[|\))", body):
                if c.group(3) in names:
                    by_param[(name, names.index(c.group(3)))] = (c.group(2), c.group(1) == "FlagsOf")
        for c in re.finditer(cast + r"[\w\.\[\]>-]*?(?:\.|->)(\w+)\s*\)", text):
            # Only conversions to an enumeration: those the other way, to a C integer, say nothing.
            if c.group(2) not in known_enums:
                continue
            by_field.setdefault(c.group(3), set()).add((c.group(2), c.group(1) == "FlagsOf"))
    return by_param, by_field


def enum_index():
    """C++ enumeration (as a cast names it, `Image::Format` or `Filter`) -> (header, owner, name)."""
    index = {}
    for header, owner, name, _, _ in generate_enums.ENUMS:
        index[f"{owner}::{name}" if owner else name] = (header, owner, name)
    return index


# ---- types -------------------------------------------------------------------------------------------------

class Model:
    def __init__(self):
        self.resources, self.structs, self.functions = parse_header()
        self.enum_sources = enum_index()
        self.param_casts, self.field_casts = parse_casts(self.enum_sources)
        self.enumerations = {}      # qualified name -> (flags, [(name, value)])
        self.choices = {}           # key -> [(name, value)]
        self.used_structs = []      # in the order first used

    def enumeration(self, cast_name, flags):
        qualified = "kor::" + cast_name
        if qualified not in self.enumerations:
            if cast_name not in self.enum_sources:
                raise SystemExit(f"{cast_name}: no such enumeration among generate_enums.ENUMS")
            header, owner, name = self.enum_sources[cast_name]
            _, entries, is_flags = generate_enums.parse(ROOT / "include", header, owner, name)
            self.enumerations[qualified] = (flags or is_flags, [(key, value) for key, value, _ in entries])
        return qualified

    def scalar(self, ctype, enum, choice):
        ctype = ctype.replace("const ", "").strip()
        if choice:
            key, values = CHOICES[choice]
            self.choices[key] = values
            return {"kind": "eChoice", "bits": 32, "name": key}
        if enum:
            qualified = self.enumeration(*enum)
            return {"kind": "eFlags" if self.enumerations[qualified][0] else "eEnum", "bits": SCALARS.get(ctype, ("", 32))[1], "name": qualified}
        if ctype not in SCALARS:
            raise SystemExit(f"no scalar type {ctype}")
        kind, bits = SCALARS[ctype]
        return {"kind": kind, "bits": bits}

    def struct(self, name):
        if name not in self.used_structs:
            self.used_structs.append(name)
            for _, ctype, _, _ in self.structs[name]:
                inner = ctype.replace("const ", "").replace("*", "").strip()
                if inner in self.structs:
                    self.struct(inner)
        return {"kind": "eStruct", "name": name, "fields": self.struct_fields(name)}

    def struct_fields(self, name):
        out, fields = [], self.structs[name]
        skip = set()
        for i, (field, ctype, length, union) in enumerate(fields):
            if field in skip:
                continue
            if union is not None:
                out.append({"name": field, "type": {"kind": "eUnion", "fields": [
                    {"name": f, "type": self.field_type(name, f, t, n)} for f, t, n, _ in union]}})
                continue
            bare = ctype.replace("const ", "").strip()
            if bare.endswith("*") and bare[:-1].strip() in self.structs and i + 1 < len(fields) and fields[i + 1][0].endswith("_count"):
                skip.add(fields[i + 1][0])
                element = self.struct(bare[:-1].strip())
                out.append({"name": field, "type": {"kind": "eList", "element": element}, "count": fields[i + 1][0]})
                continue
            out.append({"name": field, "type": self.field_type(name, field, ctype, length)})
        return out

    def field_type(self, owner, field, ctype, length):
        bare = ctype.replace("const ", "").strip()
        if bare == "char*":
            t = {"kind": "eString"}
        elif bare in self.structs:
            t = self.struct(bare)
        else:
            choice = (owner, field) if (owner, field) in CHOICES else None
            candidates = self.field_casts.get(field, set())
            if len(candidates) > 1:
                raise SystemExit(f"{owner}.{field}: converted to {sorted(c[0] for c in candidates)}, so which is it?")
            enum = next(iter(candidates)) if candidates else None
            if bare == "uint32_t" and not enum and not choice and ENUM_LIKE.search(field):
                raise SystemExit(f"{owner}.{field}: a uint32_t named like an enumeration, converted to none")
            t = self.scalar(bare, enum, choice)
        return {"kind": "eArray", "length": length, "element": t} if length else t

    def arguments(self, function, params):
        """A function's parameters after the builder, as fields, with how each is passed."""
        out, i = [], 0
        while i < len(params):
            p = params[i]
            array = re.search(r"\[(\d+)\]$", p)
            decl = p[:array.start()] if array else p
            m = re.match(r"(.*?)(\w+)$", decl.strip(), re.S)
            ctype, name = re.sub(r"\s+", " ", m.group(1).strip()), m.group(2)
            bare = ctype.replace("const ", "").strip()
            position = i + 1   # the builder is parameter 0 in the implementation
            if bare == "void*" and i + 1 < len(params) and re.search(r"uint(32|64)_t\s+\w+$", params[i + 1]):
                out.append({"name": name, "type": {"kind": "eBytes"}, "pass": "bytes", "ctype": params[i + 1].split()[0]})
                i += 2
                continue
            if bare == "char*":
                out.append({"name": name, "type": {"kind": "eString"}, "pass": "string"})
            elif bare.endswith("*") and bare[:-1].strip() in self.resources:
                kind = bare[:-1].strip()
                out.append({"name": name, "type": {"kind": "eResource", "name": "Resource" if kind == "KoralResource" else kind[len("Koral"):]},
                            "pass": "resource"})
            elif bare.endswith("*") and bare[:-1].strip() in self.structs:
                out.append({"name": name, "type": self.struct(bare[:-1].strip()), "pass": "struct", "ctype": bare[:-1].strip()})
            else:
                enum = self.param_casts.get((function, position))
                choice = (function, name) if (function, name) in CHOICES else None
                if bare == "uint32_t" and not enum and not choice and ENUM_LIKE.search(name):
                    raise SystemExit(f"{function}({name}): a uint32_t named like an enumeration, converted to none")
                element = self.scalar(bare, enum, choice)
                if array:
                    out.append({"name": name, "type": {"kind": "eArray", "length": int(array.group(1)), "element": element},
                                "pass": "array", "ctype": bare})
                else:
                    out.append({"name": name, "type": element, "pass": "scalar", "ctype": bare})
            i += 1
        return out

    def builders(self):
        out = []
        for name in self.functions:
            m = re.fullmatch(r"koral_(\w+)_builder_new", name)
            if not m or m.group(1) in EXCLUDED:
                continue
            key = m.group(1)
            prefix = f"koral_{key}_builder_"
            _, ctor_params, doc = self.functions[name]
            finish = prefix + "get_or_build" if prefix + "get_or_build" in self.functions else prefix + "build"
            result = self.functions[prefix + "build"][0].replace("*", "").strip()
            settings = []
            for fname, (ret, params, fdoc) in self.functions.items():
                if not fname.startswith(prefix) or fname in (name, prefix + "build", prefix + "get_or_build"):
                    continue
                verb = fname[len(prefix):]
                repeatable = verb.startswith("add_")
                setting = verb[4:] if verb.startswith(("set_", "add_")) else verb
                settings.append({"name": camel(setting), "function": fname, "repeatable": repeatable, "doc": fdoc,
                                 "arguments": self.arguments(fname, params[1:])})
            out.append({"name": pascal(key), "result": result[len("Koral"):], "doc": doc, "create": name, "finish": finish,
                        "finishTakesIdentifier": finish.endswith("get_or_build"),
                        "constructor": self.arguments(name, ctor_params), "settings": settings})
        return out


# ---- output -------------------------------------------------------------------------------------------------

def public(t):
    """A type as the JSON says it: without how the C call passes it."""
    out = {k: v for k, v in t.items() if k in ("kind", "bits", "name", "length")}
    if "element" in t:
        out["element"] = public(t["element"])
    if "fields" in t:
        out["fields"] = [{"name": f["name"], "type": public(f["type"])} for f in t["fields"]]
    return out


def to_json(model, builders):
    def args(fields):
        return [{"name": camel(a["name"]), "type": public(a["type"])} for a in fields]
    return json.dumps({
        "comment": "Generated by scripts/builders/generate.py from include/koral_c.h: do not edit.",
        "builders": [{"name": b["name"], "result": b["result"], "doc": b["doc"], "constructor": args(b["constructor"]),
                      "settings": [{"name": s["name"], "function": s["function"], "repeatable": s["repeatable"], "doc": s["doc"],
                                    "arguments": args(s["arguments"])} for s in b["settings"]]} for b in builders],
        "enumerations": {name: {"flags": flags, "values": {k: v for k, v in values}} for name, (flags, values) in model.enumerations.items()},
        "choices": {name: {k: v for k, v in values} for name, values in model.choices.items()},
    }, indent=2) + "\n"


class Cpp:
    def __init__(self, model):
        self.model = model
        self.types = []         # (id, C++ initializer)
        self.lines = []
        self.counter = 0

    def type_ref(self, t):
        """Emits @p t's static Type (and what it needs) and returns its name."""
        kind = t["kind"]
        extra = []
        if kind in ("eEnum", "eFlags"):
            extra.append(f".enumeration = &Enum_{ident(t['name'])}")
        if kind == "eChoice":
            extra.append(f".enumeration = &Enum_{ident(t['name'])}")
        if kind in ("eArray", "eList"):
            extra.append(f".element = &{self.type_ref(t['element'])}")
        if kind in ("eStruct", "eUnion"):
            fields = self.fields(t["fields"])
            extra.append(f".fields = {fields}")
        if "length" in t and t.get("length"):
            extra.append(f".length = {t['length']}")
        self.counter += 1
        name = f"type{self.counter}"
        parts = [f".kind = Kind::{kind}", f".bits = {t.get('bits', 32)}"]
        if t.get("name"):
            parts.append(f'.name = "{t["name"]}"')
        order = {".kind": 0, ".bits": 1, ".name": 2, ".length": 3, ".element": 4, ".fields": 5, ".enumeration": 6}
        parts = sorted(parts + extra, key=lambda p: order[p.split(" =")[0]])
        self.lines.append(f"    const Type {name} {{ {', '.join(parts)} }};")
        return name

    def fields(self, fields):
        refs = [(f["name"], self.type_ref(f["type"]), f.get("doc", "")) for f in fields]
        self.counter += 1
        name = f"fields{self.counter}"
        body = ", ".join(f'{{ "{camel(n)}", &{r}, {cstr(d)} }}' for n, r, d in refs)
        self.lines.append(f"    const Field {name}[] = {{ {body} }};" if refs else f"    const std::span<const Field> {name};")
        return name


def ident(name):
    return re.sub(r"\W", "_", name)


def cstr(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def convert(t, expr, ctype):
    """C++ that turns Value @p expr into C type @p ctype for a scalar type @p t."""
    kind = t["kind"]
    if kind == "eBool":
        return f"{expr}.Bool()"
    if kind == "eInt":
        return f"static_cast<{ctype}>({expr}.Int())"
    if kind == "eFloat":
        return f"static_cast<{ctype}>({expr}.Float())"
    if kind == "eString":
        return f"{expr}.CString()"
    if kind == "eResource":
        return f"{expr}.Resource()"
    return f"static_cast<{ctype}>({expr}.UInt())"


def fill_struct(model, name):
    """A function filling C struct @p name from a Value."""
    lines = [f"    void Fill({name}& out, const Value& value, Keep& keep)", "    {",
             "        const auto items = value.Items();", "        std::size_t i = 0;", "        (void)keep;"]
    fields = model.structs[name]
    skip = set()
    for index, (field, ctype, length, union) in enumerate(fields):
        if field in skip:
            continue
        bare = ctype.replace("const ", "").strip()
        if union is not None:
            lines.append("        {")
            lines.append("            const auto choice = At(items, i++).Items();")
            lines.append("            const auto which = At(choice, 0).UInt();")
            for u, (uf, ut, ul, _) in enumerate(union):
                t = model.field_type(name, uf, ut, ul)
                element = t["element"] if t["kind"] == "eArray" else t
                ub = ut.replace("const ", "").strip()
                if ul:
                    lines.append(f"            if (which == {u}) {{ const auto e = At(choice, 1).Items(); "
                                 f"for (std::size_t k = 0; k < {ul}; ++k) out.{field}.{uf}[k] = {convert(element, 'At(e, k)', ub)}; }}")
                else:
                    lines.append(f"            if (which == {u}) out.{field}.{uf} = {convert(element, 'At(choice, 1)', ub)};")
            lines.append("        }")
            continue
        if bare.endswith("*") and bare[:-1].strip() in model.structs and index + 1 < len(fields) and fields[index + 1][0].endswith("_count"):
            element = bare[:-1].strip()
            count = fields[index + 1][0]
            skip.add(count)
            lines.append("        {")
            lines.append("            const auto list = At(items, i++).Items();")
            lines.append(f"            auto& storage = keep.Make<std::vector<{element}>>(list.size());")
            lines.append("            for (std::size_t k = 0; k < list.size(); ++k) Fill(storage[k], list[k], keep);")
            lines.append(f"            out.{field} = storage.empty() ? nullptr : storage.data();")
            lines.append(f"            out.{count} = storage.size();")
            lines.append("        }")
            continue
        t = model.field_type(name, field, ctype, length)
        if bare in model.structs:
            lines.append(f"        Fill(out.{field}, At(items, i++), keep);")
        elif length:
            element = t["element"]
            lines.append(f"        {{ const auto e = At(items, i++).Items(); for (std::size_t k = 0; k < {length}; ++k) "
                         f"out.{field}[k] = {convert(element, 'At(e, k)', bare)}; }}")
        else:
            lines.append(f"        out.{field} = {convert(t, 'At(items, i++)', bare)};")
    lines.append("    }")
    return lines


def call(function, arguments, builder_first):
    """The body of a function calling C @p function with Values."""
    lines, args = [], (["builder"] if builder_first else [])
    for k, a in enumerate(arguments):
        v = f"At(arguments, {k})"
        how = a["pass"]
        if how == "bytes":
            lines.append(f"        const auto bytes{k} = {v}.Bytes();")
            args += [f"bytes{k}.empty() ? nullptr : bytes{k}.data()", f"static_cast<{a['ctype']}>(bytes{k}.size())"]
        elif how in ("string", "resource", "scalar"):
            args.append(convert(a["type"], v, a.get("ctype", "")))
        elif how == "struct":
            lines.append(f"        {a['ctype']} struct{k} {{}};")
            lines.append(f"        const bool given{k} = !{v}.Empty();")
            lines.append(f"        if (given{k}) Fill(struct{k}, {v}, keep);")
            args.append(f"given{k} ? &struct{k} : nullptr")
        elif how == "array":
            t = a["type"]
            lines.append(f"        {a['ctype']} array{k}[{t['length']}] {{}};")
            lines.append(f"        {{ const auto e = {v}.Items(); for (std::size_t n = 0; n < {t['length']}; ++n) "
                         f"array{k}[n] = {convert(t['element'], 'At(e, n)', a['ctype'])}; }}")
            args.append(f"array{k}")
    return lines, f"{function}({', '.join(args)})"


def to_cpp(model, builders):
    cpp = Cpp(model)
    out = ["// Generated by scripts/builders/generate.py from include/koral_c.h: do not edit.",
           "// The table behind builderDescriptions.h, and a function a setting calls its C function through.", "",
           "#include <builderDescriptions.h>", "", "#include <cstddef>", "#include <vector>", "",
           '#include "builderValues.h"', "", "namespace kor::builders::detail", "{"]
    for name, (flags, values) in model.enumerations.items():
        body = ", ".join(f'{{ "{k}", {v}u }}' for k, v in values)
        out.append(f"    const Enumerator EnumValues_{ident(name)}[] = {{ {body} }};")
        out.append(f'    const Enumeration Enum_{ident(name)} {{ "{name}", {"true" if flags else "false"}, EnumValues_{ident(name)} }};')
    for name, values in model.choices.items():
        body = ", ".join(f'{{ "{k}", {v}u }}' for k, v in values)
        out.append(f"    const Enumerator EnumValues_{ident(name)}[] = {{ {body} }};")
        out.append(f'    const Enumeration Enum_{ident(name)} {{ "{name}", false, EnumValues_{ident(name)} }};')
    out.append("")
    # Struct fillers, declared first: they call each other.
    for name in model.used_structs:
        out.append(f"    void Fill({name}& out, const Value& value, Keep& keep);")
    for name in model.used_structs:
        out += fill_struct(model, name)
    out.append("")

    descriptions = []
    for b in builders:
        settings = []
        for s in b["settings"]:
            fields = cpp.fields([{"name": a["name"], "type": a["type"]} for a in s["arguments"]])
            body, expr = call(s["function"], s["arguments"], True)
            fn = f"Invoke_{s['function']}"
            out.append(f"    void {fn}(KoralBuilder* builder, const std::span<const Value> arguments)")
            out.append("    {")
            out.append("        Keep keep;")
            out += body
            out.append(f"        {expr};")
            out.append("    }")
            settings.append(f'{{ "{s["name"]}", "{s["function"]}", {"true" if s["repeatable"] else "false"}, {fields}, {cstr(s["doc"])}, &{fn} }}')
        ctor = cpp.fields([{"name": a["name"], "type": a["type"]} for a in b["constructor"]])
        body, expr = call(b["create"], b["constructor"], False)
        out.append(f"    KoralBuilder* Create_{b['create']}(const std::span<const Value> arguments)")
        out.append("    {")
        out.append("        Keep keep;")
        out += body
        out.append(f"        return {expr};")
        out.append("    }")
        finish_call = f"{b['finish']}(builder, nullptr)" if b["finishTakesIdentifier"] else f"{b['finish']}(builder)"
        out.append(f"    KoralResource* Finish_{b['create']}(KoralBuilder* builder) {{ return {finish_call}; }}")
        out.append(f"    const Setting Settings_{b['name']}[] = {{ {', '.join(settings)} }};" if settings
                   else f"    const std::span<const Setting> Settings_{b['name']};")
        descriptions.append(f'{{ "{b["name"]}", "{b["result"]}", {ctor}, Settings_{b["name"]}, {cstr(b["doc"])}, '
                            f'&Create_{b["create"]}, &Finish_{b["create"]} }}')
    # The types go before everything that refers to them.
    head = out[:out.index("namespace kor::builders::detail") + 2]
    tail = out[out.index("namespace kor::builders::detail") + 2:]
    enums = [l for l in tail if "Enumerator EnumValues_" in l or "const Enumeration Enum_" in l]
    rest = [l for l in tail if l not in enums]
    out = head + enums + [""] + cpp.lines + [""] + rest
    out.append(f"    const Description Descriptions[] = {{")
    out += [f"        {d}," for d in descriptions]
    out.append("    };")
    out.append(f"    const Enumeration* const EnumerationList[] = {{ {', '.join('&Enum_' + ident(n) for n in model.enumerations)} }};")
    out.append("}")
    out.append("")
    out.append("namespace kor::builders")
    out.append("{")
    out.append("    std::span<const Description> All() { return detail::Descriptions; }")
    out.append("    std::span<const Enumeration> Enumerations()")
    out.append("    {")
    out.append("        static const std::vector<Enumeration> all = [] {")
    out.append("            std::vector<Enumeration> list;")
    out.append("            for (const auto* e : detail::EnumerationList) list.push_back(*e);")
    out.append("            return list;")
    out.append("        }();")
    out.append("        return all;")
    out.append("    }")
    out.append("}")
    return "\n".join(out) + "\n"


def main():
    model = Model()
    builders = model.builders()
    outputs = {OUT_CPP: to_cpp(model, builders), OUT_JSON: to_json(model, builders)}
    if "--check" in sys.argv:
        stale = [str(p.relative_to(ROOT)) for p, text in outputs.items() if not p.exists() or p.read_text() != text]
        if stale:
            raise SystemExit("stale, run scripts/builders/generate.py: " + ", ".join(stale))
        return
    for path, text in outputs.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        print(f"wrote {path.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
