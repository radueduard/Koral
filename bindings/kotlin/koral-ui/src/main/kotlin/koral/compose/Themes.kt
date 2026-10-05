package koral.compose

import java.lang.foreign.Arena
import java.lang.foreign.MemoryLayout
import java.lang.foreign.MemorySegment
import java.lang.foreign.SegmentAllocator
import java.lang.foreign.ValueLayout
import koral.ui.interop.KuiLayouts
import koral.ui.interop.KuiNative

// The look of an interface: its colours, how round its things are, how tall its controls and how big its text.
// Four families, each light and dark, each in an accent of the caller's choosing:
//
//     setContent(theme = KoralDarkTheme) { ... }                       // koral-ui's own: One UI's shapes, coral
//     setContent(theme = Themes.material(dark = false)) { ... }        // Material 3
//     setContent(theme = Themes.cupertino(accent = Color(0xFFFF2D55))) { ... }
//     setContent(theme = Themes.windows()) { ... }                     // Fluent: dark or light, and the accent, as Windows is set
//     ui.theme = Themes.material(dark = true)                          // changed while it runs
//
// A theme is a value: `KoralDarkTheme.copy(radius = 8.dp)` is one of the caller's own.

/** The colours, shapes and type the built-in controls draw with: koral-ui's kui::Theme. */
data class Theme(
    val background: Color, val surface: Color, val surfaceHover: Color, val surfacePressed: Color,
    val primary: Color, val primaryHover: Color, val primaryPressed: Color, val onPrimary: Color,
    val text: Color, val textMuted: Color, val border: Color, val focus: Color,
    /** How round a card, a menu, a panel is — and everything else that is not said below. */
    val radius: Dp, val controlHeight: Dp, val fontSize: TextUnit,
    /** How round a button is; a field (a text field, a dropdown, a drag value). Unspecified: [radius]. */
    val buttonRadius: Dp = Dp.Unspecified, val fieldRadius: Dp = Dp.Unspecified,
    /** How round a checkbox's corners are. Unspecified: it is a circle. */
    val checkboxRadius: Dp = Dp.Unspecified,
    /** The fonts text is drawn in, where it does not say: null is koral-ui's own. */
    val fontFamily: FontFamily? = null,
    /** Whose manner the controls are drawn in — what a switch, a slider, a field is made of: kui::ThemeDesign. */
    val design: ThemeFamily = ThemeFamily.Koral,
) {
    /** Whether it is a dark theme: by how light its background is. */
    val isDark: Boolean get() = background.luminance < 0.5f

    /** This theme in another accent: the colour of what is primary, lighter where the pointer is over it and darker where it is pressed. */
    fun withAccent(accent: Color): Theme = if (!accent.isSpecified) this else copy(
        primary = accent, primaryHover = accent.mix(Color.White, 0.14f), primaryPressed = accent.mix(Color.Black, 0.14f),
        onPrimary = if (accent.luminance > 0.6f) Color(0xFF101010) else Color.White,
        focus = accent.mix(if (isDark) Color.White else Color.Black, 0.3f))

    internal fun native(a: SegmentAllocator): MemorySegment = Struct(a.allocate(KuiLayouts.KuiTheme).also { KuiNative.kui_theme_dark(it) }, KuiLayouts.KuiTheme)
        .color("background", background).color("surface", surface).color("surface_hover", surfaceHover)
        .color("surface_pressed", surfacePressed).color("primary", primary).color("primary_hover", primaryHover)
        .color("primary_pressed", primaryPressed).color("on_primary", onPrimary).color("text", text)
        .color("text_muted", textMuted).color("border", border).color("focus", focus)
        .float("radius", radius.value).float("control_height", controlHeight.value)
        .float("text_style.size", fontSize.value).color("text_style.color", text)
        .address("text_style.font", fontFamily?.resolve(FontWeight.Normal, FontStyle.Normal)?.native ?: MemorySegment.NULL)
        .float("button_radius", if (buttonRadius.isSpecified) buttonRadius.value else -1f)
        .float("field_radius", if (fieldRadius.isSpecified) fieldRadius.value else -1f)
        .float("checkbox_radius", if (checkboxRadius.isSpecified) checkboxRadius.value else -1f)
        .int("design", design.ordinal)
        .segment

    companion object {
        /** koral-ui's own, dark: [KoralDarkTheme]. */
        val Dark: Theme by lazy { of { KuiNative.kui_theme_dark(it) } }
        /** koral-ui's own, light: [KoralLightTheme]. */
        val Light: Theme by lazy { of { KuiNative.kui_theme_light(it) } }

        private fun of(fill: (MemorySegment) -> Unit): Theme = Arena.ofConfined().use { a ->
            val t = a.allocate(KuiLayouts.KuiTheme)
            fill(t)
            fun offset(vararg path: String) = KuiLayouts.KuiTheme.byteOffset(*path.map { MemoryLayout.PathElement.groupElement(it) }.toTypedArray())
            fun c(name: String): Color {
                val o = offset(name)
                val v = FloatArray(4) { t.get(ValueLayout.JAVA_FLOAT, o + it * 4L) }
                return Color(v[0], v[1], v[2], v[3])
            }
            fun f(vararg path: String) = t.get(ValueLayout.JAVA_FLOAT, offset(*path))
            Theme(c("background"), c("surface"), c("surface_hover"), c("surface_pressed"), c("primary"), c("primary_hover"),
                  c("primary_pressed"), c("on_primary"), c("text"), c("text_muted"), c("border"), c("focus"),
                  f("radius").dp, f("control_height").dp, f("text_style", "size").sp)
        }
    }
}

/** How light a colour is, from 0 to 1, as the eye weighs red, green and blue. */
val Color.luminance: Float get() = 0.2126f * red + 0.7152f * green + 0.0722f * blue

/** This colour, so far of the way to [other]. */
fun Color.mix(other: Color, amount: Float): Color =
    Color(red + (other.red - red) * amount, green + (other.green - green) * amount, blue + (other.blue - blue) * amount, alpha + (other.alpha - alpha) * amount)

/** How the system itself is set: dark or light, and its accent — what [Themes.windows] follows. */
object SystemAppearance {
    private data class Read(val known: Boolean, val dark: Boolean, val accent: Color)
    private fun read(): Read = Arena.ofConfined().use { a ->
        val dark = a.allocate(ValueLayout.JAVA_BOOLEAN)
        val accent = a.allocate(KuiLayouts.KuiColor)
        val known = KuiNative.kui_system_appearance(dark, accent)
        fun f(i: Long) = accent.getAtIndex(ValueLayout.JAVA_FLOAT, i)
        Read(known, dark.get(ValueLayout.JAVA_BOOLEAN, 0), Color(f(0), f(1), f(2), f(3)))
    }
    /** Whether the system said how it is set: false where it has no such setting to ask. */
    val isKnown: Boolean get() = read().known
    val isDark: Boolean get() = read().dark
    val accent: Color get() = read().accent
}

/** The families of look an interface can have: each has its colours and sizes, and its own design of every control. In kui::ThemeDesign's order. */
enum class ThemeFamily { Koral, Material, Cupertino, Windows }

/** The themes, by family: each dark or light, each in its own accent or the caller's. */
object Themes {
    /** Coral: koral-ui's accent. */
    val Coral = Color(0xFFFF7F50)

    /** koral-ui's own: One UI's shapes — round buttons and fields, a circle for a checkbox — on black, or on a soft grey. */
    fun koral(dark: Boolean = true, accent: Color = Color.Unspecified): Theme = (if (dark) Theme.Dark else Theme.Light).withAccent(accent)

    /** Material 3: filled, outlined and text buttons, filled fields with a line under them, square checkboxes, a bar for a slider's handle; violet unless [accent] says. */
    fun material(dark: Boolean = true, accent: Color = Color.Unspecified): Theme = (if (dark) Theme(
        background = Color(0xFF141218), surface = Color(0xFF211F26), surfaceHover = Color(0xFF2B2930), surfacePressed = Color(0xFF36343B),
        primary = Color(0xFFD0BCFF), primaryHover = Color(0xFFDCCBFF), primaryPressed = Color(0xFFB69DF8), onPrimary = Color(0xFF381E72),
        text = Color(0xFFE6E0E9), textMuted = Color(0xFFCAC4D0), border = Color(0xFF49454F), focus = Color(0xFFD0BCFF),
        radius = 12.dp, controlHeight = 40.dp, fontSize = 14.sp, buttonRadius = 20.dp, fieldRadius = 4.dp, checkboxRadius = 2.dp, design = ThemeFamily.Material)
    else Theme(
        background = Color(0xFFFEF7FF), surface = Color(0xFFF3EDF7), surfaceHover = Color(0xFFECE6F0), surfacePressed = Color(0xFFE6E0E9),
        primary = Color(0xFF6750A4), primaryHover = Color(0xFF7965AF), primaryPressed = Color(0xFF5B4597), onPrimary = Color.White,
        text = Color(0xFF1D1B20), textMuted = Color(0xFF49454F), border = Color(0xFFCAC4D0), focus = Color(0xFF6750A4),
        radius = 12.dp, controlHeight = 40.dp, fontSize = 14.sp, buttonRadius = 20.dp, fieldRadius = 4.dp, checkboxRadius = 2.dp, design = ThemeFamily.Material)).withAccent(accent)

    /** Cupertino: Apple's, after Liquid Glass — capsule buttons, fields and segments of glass, clear or the accent's, with a bright rim; the system blue unless [accent] says. */
    fun cupertino(dark: Boolean = true, accent: Color = Color.Unspecified): Theme = (if (dark) Theme(
        background = Color(0xFF000000), surface = Color(0xFF1C1C1E), surfaceHover = Color(0xFF2C2C2E), surfacePressed = Color(0xFF3A3A3C),
        primary = Color(0xFF0A84FF), primaryHover = Color(0xFF3B9BFF), primaryPressed = Color(0xFF0871DB), onPrimary = Color.White,
        text = Color.White, textMuted = Color(0xFF98989F), border = Color(0xFF38383A), focus = Color(0xFF64B1FF),
        radius = 16.dp, controlHeight = 34.dp, fontSize = 15.sp, buttonRadius = 17.dp, fieldRadius = 17.dp, design = ThemeFamily.Cupertino)
    else Theme(
        background = Color(0xFFF2F2F7), surface = Color(0xFFFFFFFF), surfaceHover = Color(0xFFE5E5EA), surfacePressed = Color(0xFFD1D1D6),
        primary = Color(0xFF007AFF), primaryHover = Color(0xFF3395FF), primaryPressed = Color(0xFF0068D9), onPrimary = Color.White,
        text = Color(0xFF000000), textMuted = Color(0xFF8A8A8E), border = Color(0xFFC6C6C8), focus = Color(0xFF007AFF),
        radius = 16.dp, controlHeight = 34.dp, fontSize = 15.sp, buttonRadius = 17.dp, fieldRadius = 17.dp, design = ThemeFamily.Cupertino)).withAccent(accent)

    /**
     * Windows' own (Fluent): small corners all round, in Segoe UI where Windows has it. Unless told, it is dark
     * or light as Windows is set, and in Windows' accent — which is what makes it the native look, not a likeness.
     */
    fun windows(dark: Boolean = SystemAppearance.isDark, accent: Color = Color.Unspecified): Theme {
        val segoe = FontFamily.system("segoeui.ttf", bold = "segoeuib.ttf", italic = "segoeuii.ttf", boldItalic = "segoeuiz.ttf")
        val base = if (dark) Theme(
            background = Color(0xFF202020), surface = Color(0xFF2B2B2B), surfaceHover = Color(0xFF323232), surfacePressed = Color(0xFF272727),
            primary = Color(0xFF60CDFF), primaryHover = Color(0xFF5BC0EE), primaryPressed = Color(0xFF56B3DD), onPrimary = Color(0xFF000000),
            text = Color(0xFFFFFFFF), textMuted = Color(0xFFC5C5C5), border = Color(0xFF3D3D3D), focus = Color(0xFFFFFFFF),
            radius = 8.dp, controlHeight = 32.dp, fontSize = 14.sp, buttonRadius = 4.dp, fieldRadius = 4.dp, checkboxRadius = 4.dp, fontFamily = segoe, design = ThemeFamily.Windows)
        else Theme(
            background = Color(0xFFF3F3F3), surface = Color(0xFFFBFBFB), surfaceHover = Color(0xFFF0F0F0), surfacePressed = Color(0xFFE9E9E9),
            primary = Color(0xFF005FB8), primaryHover = Color(0xFF1A6FC0), primaryPressed = Color(0xFF337FC7), onPrimary = Color.White,
            text = Color(0xFF1A1A1A), textMuted = Color(0xFF5F5F5F), border = Color(0xFFE0E0E0), focus = Color(0xFF1A1A1A),
            radius = 8.dp, controlHeight = 32.dp, fontSize = 14.sp, buttonRadius = 4.dp, fieldRadius = 4.dp, checkboxRadius = 4.dp, fontFamily = segoe, design = ThemeFamily.Windows)
        // Windows' own accent where it has one, lightened on dark as Windows itself lightens it.
        val system = if (SystemAppearance.isKnown) SystemAppearance.accent.let { if (dark) it.mix(Color.White, 0.45f) else it } else Color.Unspecified
        return base.withAccent(if (accent.isSpecified) accent else system)
    }

    fun of(family: ThemeFamily, dark: Boolean = true, accent: Color = Color.Unspecified): Theme = when (family) {
        ThemeFamily.Koral -> koral(dark, accent)
        ThemeFamily.Material -> material(dark, accent)
        ThemeFamily.Cupertino -> cupertino(dark, accent)
        ThemeFamily.Windows -> windows(dark, accent)
    }
}

/** koral-ui's own look, dark: every choice it makes of a colour or a size, as one value. */
val KoralDarkTheme: Theme by lazy { Themes.koral(dark = true) }
val KoralLightTheme: Theme by lazy { Themes.koral(dark = false) }
val MaterialDarkTheme: Theme by lazy { Themes.material(dark = true) }
val MaterialLightTheme: Theme by lazy { Themes.material(dark = false) }
val CupertinoDarkTheme: Theme by lazy { Themes.cupertino(dark = true) }
val CupertinoLightTheme: Theme by lazy { Themes.cupertino(dark = false) }
val WindowsDarkTheme: Theme by lazy { Themes.windows(dark = true) }
val WindowsLightTheme: Theme by lazy { Themes.windows(dark = false) }
