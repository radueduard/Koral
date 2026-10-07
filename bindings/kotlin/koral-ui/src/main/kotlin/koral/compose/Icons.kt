package koral.compose

import androidx.compose.runtime.Composable
import java.lang.foreign.MemorySegment
import java.lang.ref.Cleaner
import java.util.concurrent.ConcurrentHashMap
import koral.ui.IconStyle
import koral.ui.interop.KuiLayouts
import koral.ui.interop.KuiNative

// Icons, as Compose's material-icons-core and -extended have them: `Icon(Icons.Default.Add, contentDescription = "Add")`.
// They are Google's own drawings — koral-ui carries the SVGs Material publishes, all of Compose's in all five styles
// — read and drawn natively, in the colour they are tinted. Their names are generated, in MaterialIcons.kt.

private val vectorCleaner: Cleaner = Cleaner.create()

/** A drawing of filled shapes — one of the Material icons, or an SVG's — scaled to whatever size it is shown at. */
class ImageVector internal constructor(val name: String, internal val handle: MemorySegment) {
    companion object {
        /** [svg], the text of an SVG document, read: its paths, circles, rects and polygons, drawn in one colour. */
        fun fromSvg(svg: String, name: String = "svg"): ImageVector {
            val handle = KuiNative.kui_vector_image_from_svg(svg, svg.encodeToByteArray().size.toLong())
            return ImageVector(name, handle).also {
                if (handle != MemorySegment.NULL) vectorCleaner.register(it) { KuiNative.kui_vector_image_release(handle) }
            }
        }
    }
}

/** An [imageVector], [tint]ed — the content colour around it, unless given — 24 units square unless [modifier] says. */
@Composable
fun Icon(imageVector: ImageVector, contentDescription: String?, modifier: Modifier = Modifier, tint: Color = LocalContentColor.current) {
    @Suppress("UNUSED_VARIABLE") val described = contentDescription
    Node(modifier, imageVector to tint, { _, _ ->
        scratch { a ->
            val color = Struct(a, KuiLayouts.KuiColor)
            // Unspecified: the theme's text colour, which koral-ui reads under whatever theme is over it.
            if (tint.isSpecified) color.color("r", tint) else color.floats("r", 0f, 0f, 0f, -1f)
            KuiNative.kui_icon(imageVector.handle, color.segment)
        }
    })
}

/** One style of the Material icons — their names are [MaterialIcons]' and [MirroredIcons]' — each made the first time it is asked for, then kept. */
abstract class IconSet internal constructor(private val style: IconStyle) {
    internal fun icon(name: String): ImageVector = loaded.getOrPut(style.value to name) {
        ImageVector(name, KuiNative.kui_material_icon(name, style.value))
    }

    private companion object {
        val loaded = ConcurrentHashMap<Pair<Int, String>, ImageVector>()
    }
}

/** The icons, by style and name: `Icons.Filled.Add`, `Icons.Outlined.Lock`, `Icons.AutoMirrored.Filled.ArrowBack`. */
object Icons {
    val Default get() = Filled
    object Filled : MaterialIcons(IconStyle.eFilled)
    object Outlined : MaterialIcons(IconStyle.eOutlined)
    object Rounded : MaterialIcons(IconStyle.eRounded)
    object Sharp : MaterialIcons(IconStyle.eSharp)
    object TwoTone : MaterialIcons(IconStyle.eTwoTone)

    object AutoMirrored {
        val Default get() = Filled
        object Filled : MirroredIcons(IconStyle.eFilled)
        object Outlined : MirroredIcons(IconStyle.eOutlined)
        object Rounded : MirroredIcons(IconStyle.eRounded)
        object Sharp : MirroredIcons(IconStyle.eSharp)
        object TwoTone : MirroredIcons(IconStyle.eTwoTone)
    }
}
