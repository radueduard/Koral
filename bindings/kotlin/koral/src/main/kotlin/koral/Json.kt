package koral

/**
 * Just enough JSON for what Koral exchanges as text — scene arguments, input bindings: objects become
 * Maps, arrays Lists, numbers Doubles.
 */
internal object Json {
    fun parse(text: String): Any? = Reader(text).run { value().also { space(); require(at == text.length) { "JSON: trailing text at $at" } } }

    fun write(value: Any?): String = buildString { write(this, value) }

    private fun write(out: StringBuilder, value: Any?) {
        when (value) {
            null -> out.append("null")
            is String -> quote(out, value)
            is Boolean -> out.append(value)
            is Float -> out.append(if (value % 1f == 0f && kotlin.math.abs(value) < 1e15f) value.toLong().toString() else value.toString())
            is Double -> out.append(if (value % 1.0 == 0.0 && kotlin.math.abs(value) < 1e15) value.toLong().toString() else value.toString())
            is Number -> out.append(value.toString())
            is Map<*, *> -> {
                out.append('{')
                value.entries.forEachIndexed { i, (k, v) -> if (i > 0) out.append(','); quote(out, k.toString()); out.append(':'); write(out, v) }
                out.append('}')
            }
            is Iterable<*> -> {
                out.append('[')
                value.forEachIndexed { i, v -> if (i > 0) out.append(','); write(out, v) }
                out.append(']')
            }
            else -> quote(out, value.toString())
        }
    }

    private fun quote(out: StringBuilder, s: String) {
        out.append('"')
        for (c in s) when (c) {
            '"' -> out.append("\\\""); '\\' -> out.append("\\\\"); '\n' -> out.append("\\n"); '\r' -> out.append("\\r"); '\t' -> out.append("\\t")
            else -> if (c < ' ') out.append("\\u%04x".format(c.code)) else out.append(c)
        }
        out.append('"')
    }

    private class Reader(val text: String) {
        var at = 0

        fun space() { while (at < text.length && text[at].isWhitespace()) at++ }

        fun value(): Any? {
            space()
            require(at < text.length) { "JSON: unexpected end" }
            return when (val c = text[at]) {
                '{' -> obj()
                '[' -> arr()
                '"' -> str()
                't' -> literal("true", true)
                'f' -> literal("false", false)
                'n' -> literal("null", null)
                else -> if (c == '-' || c.isDigit()) num() else throw IllegalArgumentException("JSON: unexpected '$c' at $at")
            }
        }

        fun literal(word: String, value: Any?): Any? {
            require(text.startsWith(word, at)) { "JSON: expected $word at $at" }
            at += word.length
            return value
        }

        fun num(): Double {
            val start = at
            while (at < text.length && (text[at].isDigit() || text[at] in "+-.eE")) at++
            return text.substring(start, at).toDouble()
        }

        fun str(): String {
            val out = StringBuilder()
            at++
            while (text[at] != '"') {
                if (text[at] == '\\') {
                    at++
                    when (val e = text[at]) {
                        'n' -> out.append('\n'); 't' -> out.append('\t'); 'r' -> out.append('\r'); 'b' -> out.append('\b'); 'f' -> out.append('\u000c')
                        'u' -> { out.append(text.substring(at + 1, at + 5).toInt(16).toChar()); at += 4 }
                        else -> out.append(e)
                    }
                } else out.append(text[at])
                at++
            }
            at++
            return out.toString()
        }

        fun obj(): Map<String, Any?> {
            val map = LinkedHashMap<String, Any?>()
            at++
            space()
            if (text[at] == '}') { at++; return map }
            while (true) {
                space()
                val key = str()
                space(); require(text[at] == ':') { "JSON: expected ':' at $at" }; at++
                map[key] = value()
                space()
                if (text[at] == ',') { at++; continue }
                require(text[at] == '}') { "JSON: expected '}' at $at" }
                at++
                return map
            }
        }

        fun arr(): List<Any?> {
            val list = ArrayList<Any?>()
            at++
            space()
            if (text[at] == ']') { at++; return list }
            while (true) {
                list += value()
                space()
                if (text[at] == ',') { at++; continue }
                require(text[at] == ']') { "JSON: expected ']' at $at" }
                at++
                return list
            }
        }
    }
}
