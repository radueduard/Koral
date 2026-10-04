package koral.compose

import java.io.File
import java.lang.foreign.Arena
import java.lang.foreign.MemorySegment
import java.lang.foreign.ValueLayout
import koral.ui.interop.KuiNative

// Fonts of one's own, as Compose writes them:
//
//     val Inter = FontFamily(
//         Font("assets/fonts/Inter-Regular.ttf"),
//         Font("assets/fonts/Inter-Bold.ttf", FontWeight.Bold),
//         Font("assets/fonts/Inter-Italic.ttf", style = FontStyle.Italic))
//     Text("Hello", fontFamily = Inter, fontWeight = FontWeight.Bold)
//     setContent(theme = KoralDarkTheme.copy(fontFamily = Inter)) { ... }     // everywhere, the controls too
//
// A family gives the font nearest the weight and the style asked for. Where it has none that is that heavy, or
// that slanted, koral-ui thickens or slants the one it has — which a family's own bold and italic do better.

/** One font file of a family: what weight and style it is. It is read when it is first drawn with. */
class Font private constructor(private val load: () -> MemorySegment, val weight: FontWeight, val style: FontStyle, val name: String) {
    /** A font file — TrueType or OpenType — by its path. */
    constructor(path: String, weight: FontWeight = FontWeight.Normal, style: FontStyle = FontStyle.Normal) :
        this({ KuiNative.kui_font_load(path) }, weight, style, path)

    constructor(file: File, weight: FontWeight = FontWeight.Normal, style: FontStyle = FontStyle.Normal) : this(file.path, weight, style)

    /** A font from its bytes: one read from a resource, or downloaded. */
    constructor(identity: String, data: ByteArray, weight: FontWeight = FontWeight.Normal, style: FontStyle = FontStyle.Normal) :
        this({
            Arena.ofConfined().use { a ->
                val bytes = a.allocate(data.size.toLong())
                MemorySegment.copy(data, 0, bytes, ValueLayout.JAVA_BYTE, 0L, data.size)
                KuiNative.kui_font_from_memory(bytes, data.size.toLong(), identity)
            }
        }, weight, style, identity)

    /** The font koral-ui draws with: null where the file could not be read, and the default is drawn instead. */
    internal val native: MemorySegment? by lazy {
        val handle = try { load() } catch (e: Throwable) { MemorySegment.NULL }
        if (handle == MemorySegment.NULL) { koral.Log.warn("[koral.compose] the font '$name' could not be loaded: the default is drawn in its place"); null }
        else handle
    }

    companion object {
        /** A font among the program's resources: `Font.resource("fonts/Inter-Regular.ttf")`. */
        fun resource(path: String, weight: FontWeight = FontWeight.Normal, style: FontStyle = FontStyle.Normal): Font {
            val bytes = (Thread.currentThread().contextClassLoader ?: Font::class.java.classLoader).getResourceAsStream(path)?.use { it.readBytes() }
                ?: ByteArray(0)
            return Font(path, bytes, weight, style)
        }
    }
}

/** A family of fonts: the same face at several weights, upright and slanted. */
class FontFamily internal constructor(val name: String, val fonts: List<Font>) {
    /** The font of the family nearest [weight] and [style] — and whether it is that weight and that style, or must be made so. */
    internal fun resolve(weight: FontWeight, style: FontStyle): Font? =
        fonts.filter { it.native != null }.minByOrNull { (if (it.style == style) 0 else 1000) + kotlin.math.abs(it.weight.weight - weight.weight) }

    override fun toString() = "FontFamily($name)"

    companion object {
        /** koral-ui's own font. */
        val Default = FontFamily("default", emptyList())
        /** The system's, where it has them; koral-ui's own where it does not. */
        val SansSerif: FontFamily by lazy { system("segoeui.ttf", "segoeuib.ttf", "segoeuii.ttf", "segoeuiz.ttf", "sans-serif") }
        val Serif: FontFamily by lazy { system("times.ttf", "timesbd.ttf", "timesi.ttf", "timesbi.ttf", "serif") }
        val Monospace: FontFamily by lazy { system("consola.ttf", "consolab.ttf", "consolai.ttf", "consolaz.ttf", "monospace") }
        val Cursive: FontFamily by lazy { system("segoesc.ttf", "segoescb.ttf", name = "cursive") }

        /** Where the system keeps its fonts. */
        private val folders: List<File> get() = listOfNotNull(
            System.getenv("WINDIR")?.let { File(it, "Fonts") }, File("/usr/share/fonts/truetype"), File("/usr/share/fonts"),
            File("/System/Library/Fonts"), File("/Library/Fonts"))

        /** A family of the system's own fonts, by their file names: the ones that are not there are left out. */
        fun system(regular: String, bold: String? = null, italic: String? = null, boldItalic: String? = null, name: String = regular): FontFamily {
            fun find(file: String?): File? = file?.let { f -> folders.map { File(it, f) }.firstOrNull { it.isFile } }
            return FontFamily(name, listOfNotNull(
                find(regular)?.let { Font(it.path) }, find(bold)?.let { Font(it.path, FontWeight.Bold) },
                find(italic)?.let { Font(it.path, style = FontStyle.Italic) }, find(boldItalic)?.let { Font(it.path, FontWeight.Bold, FontStyle.Italic) }))
        }
    }
}

/** A family of these fonts. */
fun FontFamily(vararg fonts: Font): FontFamily = FontFamily(fonts.firstOrNull()?.name ?: "empty", fonts.toList())
fun FontFamily(fonts: List<Font>): FontFamily = FontFamily(fonts.firstOrNull()?.name ?: "empty", fonts)
