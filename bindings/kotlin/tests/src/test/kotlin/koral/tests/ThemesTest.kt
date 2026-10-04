package koral.tests

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import koral.Buffer
import koral.CommandBuffer
import koral.MouseButton
import koral.OffscreenSettings
import koral.Scene
import koral.Vec2
import koral.compose.Alignment
import koral.compose.AnimatedVisibility
import koral.compose.Box
import koral.compose.BoxWithConstraints
import koral.compose.Color
import koral.compose.Column
import koral.compose.ComposeUi
import koral.compose.CupertinoLightTheme
import koral.compose.Dp
import koral.compose.FocusRequester
import koral.compose.FontFamily
import koral.compose.FontStyle
import koral.compose.FontWeight
import koral.compose.IntSize
import koral.compose.KoralDarkTheme
import koral.compose.KoralTheme
import koral.compose.LinearProgressIndicator
import koral.compose.LocalTheme
import koral.compose.Modifier
import koral.compose.Row
import koral.compose.Slider
import koral.compose.SystemAppearance
import koral.compose.Text
import koral.compose.TextDecoration
import koral.compose.TextField
import koral.compose.TextOverflow
import koral.compose.ThemeFamily
import koral.compose.Themes
import koral.compose.background
import koral.compose.detectTapGestures
import koral.compose.dp
import koral.compose.focusRequester
import koral.compose.onSizeChanged
import koral.compose.pointerInput
import koral.compose.setContent
import koral.compose.size
import koral.compose.sp
import koral.compose.width
import kotlin.math.abs
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNotEquals
import kotlin.test.assertTrue

private const val SIDE = 260

class Themed : Scene() {
    val readback: Buffer = readback(SIDE)
    lateinit var ui: ComposeUi
    var room: Dp = 0.dp
    var longPresses = 0
    var taps = 0
    var slider by mutableStateOf(0.2f)
    var finished = 0
    var text by mutableStateOf("")
    val focus = FocusRequester()
    var cut = IntSize(0, 0)
    var narrow = IntSize(0, 0)
    var wide = IntSize(0, 0)
    var shown by mutableStateOf(true)
    var grown = IntSize(-1, -1)

    override fun initialize() {
        graph.add(Clear(0f, 0f, 0f))
        ui = setContent(theme = KoralDarkTheme) {
            Column {
                // 0 to 20: the theme as composables read it; a box told its room; one that hears a long press.
                Row {
                    Box(Modifier.size(20.dp).background(LocalTheme.current.primary))
                    BoxWithConstraints(Modifier.size(30.dp, 20.dp)) { room = maxWidth }
                    Box(Modifier.size(20.dp).background(Color.Blue).pointerInput(Unit) {
                        detectTapGestures(onLongPress = { longPresses++ }, onTap = { taps++ })
                    })
                }
                // 20 to 30, 30 to 40: one of koral-ui's own controls, in the interface's theme and in one of its own.
                LinearProgressIndicator({ 1f }, Modifier.size(100.dp, 10.dp))
                KoralTheme(CupertinoLightTheme) { LinearProgressIndicator({ 1f }, Modifier.size(100.dp, 10.dp)) }
                // 40 on, 24 each: text as it is; heavier; slanted; with a line under it.
                Box(Modifier.size(260.dp, 24.dp)) { Text("HHHHHHHH", color = Color.White, fontSize = 16.sp) }
                Box(Modifier.size(260.dp, 24.dp)) { Text("HHHHHHHH", color = Color.White, fontSize = 16.sp, fontWeight = FontWeight.Black) }
                Box(Modifier.size(260.dp, 24.dp)) { Text("HHHHHHHH", color = Color.White, fontSize = 16.sp, fontStyle = FontStyle.Italic) }
                Box(Modifier.size(260.dp, 24.dp)) { Text("HHHHHHHH", color = Color.White, fontSize = 16.sp, textDecoration = TextDecoration.Underline) }
                // 136 to 160: a line too long for its 60, cut with an ellipsis; and a family of the system's.
                Row(Modifier.size(260.dp, 24.dp)) {
                    Box(Modifier.width(60.dp)) {
                        Text("a line that is a good deal too long", maxLines = 1, overflow = TextOverflow.Ellipsis, modifier = Modifier.onSizeChanged { cut = it })
                    }
                    Text("iiii", fontFamily = FontFamily.Monospace, modifier = Modifier.onSizeChanged { narrow = it })
                    Text("MMMM", fontFamily = FontFamily.Monospace, modifier = Modifier.onSizeChanged { wide = it })
                }
                // 160 to 200: a slider that says when it is let go of.
                Box(Modifier.size(260.dp, 40.dp), contentAlignment = Alignment.CenterStart) {
                    Slider(slider, { slider = it }, Modifier.width(200.dp), onValueChangeFinished = { finished++ })
                }
                // 200 on: a field given the keyboard without being pressed, and something that goes by shrinking.
                TextField(text, { text = it }, Modifier.focusRequester(focus), singleLine = true, width = 120.dp)
                AnimatedVisibility(shown) { Box(Modifier.size(20.dp).background(Color.Green).onSizeChanged { grown = it }) }
            }
        }
        graph.add(ReadScreen(readback))
    }
}

/** Themes by family, light and dark and in an accent, changed while running; fonts and what text can be; what Compose has that was missing. */
class ThemesTest {
    @Test
    fun theFamiliesOfThemesAreWhatTheySay() {
        for (family in ThemeFamily.entries) {
            assertTrue(Themes.of(family, dark = true).isDark, "$family, dark")
            assertTrue(!Themes.of(family, dark = false).isDark, "$family, light")
            val pink = Themes.of(family, dark = true, accent = Color(0xFFFF2D55))
            assertEquals(Color(0xFFFF2D55), pink.primary, "$family takes an accent")
            assertNotEquals(pink.primary, pink.primaryHover)
        }
        assertEquals(Themes.Coral, KoralDarkTheme.primary, "koral-ui's own is coral")
        assertTrue(!KoralDarkTheme.checkboxRadius.value.isFinite() || KoralDarkTheme.checkboxRadius.value < 0f, "and its checkbox a circle")
        assertTrue(Themes.material().buttonRadius.value > Themes.windows(dark = true).buttonRadius.value)
        // Whatever the system says, asking is safe; on Windows it says.
        SystemAppearance.accent
        if (System.getProperty("os.name").startsWith("Windows")) assertTrue(SystemAppearance.isKnown, "Windows says how it is set")
    }

    @Test
    fun aThemeChangedWhileRunningAndTextInItsWeights(): Unit = headlessApp().use { app ->
        val scene = app.openOffscreen("Themed", Themed(), OffscreenSettings(width = SIDE, height = SIDE))
        val input = scene.input
        fun at(x: Float, y: Float) { input.feedMousePosition(Vec2(x, y)); app.frames(1) }
        fun button(down: Boolean) { input.feedMouseButton(MouseButton.e1, down); app.frames(1) }
        fun pixels(): ByteArray { CommandBuffer.singleTimeCommand { }.waitBlocking(); return scene.readback.read() }
        fun rgb(bytes: ByteArray, x: Int, y: Int) = (0 until 3).map { bytes[(y * SIDE + x) * 4 + it].toInt() and 0xff }
        fun near(expected: Color, actual: List<Int>, what: String) {
            val want = listOf(expected.red, expected.green, expected.blue).map { Math.round(it * 255f) }
            assertTrue(want.zip(actual).all { (a, b) -> abs(a - b) <= 4 }, "$what: wanted $want, drawn $actual")
        }
        /** How much of the band from [top], 24 tall, is lit: the sum of its red. */
        fun lit(bytes: ByteArray, top: Int): Int { var sum = 0; for (y in top until top + 24) for (x in 0 until SIDE) sum += rgb(bytes, x, y)[0]; return sum }

        at(250f, 250f); app.frames(4)
        var frame = pixels()
        near(Themes.Coral, rgb(frame, 10, 10), "the theme, read by a composable")
        near(Themes.Coral, rgb(frame, 50, 25), "the theme, drawn by koral-ui's own control")
        near(CupertinoLightTheme.primary, rgb(frame, 50, 35), "a theme of its own for part of the interface")
        assertEquals(30.dp, scene.room, "BoxWithConstraints: the room there is")

        val plain = lit(frame, 40); val heavy = lit(frame, 64); val slanted = lit(frame, 88); val underlined = lit(frame, 112)
        assertTrue(plain > 0, "text draws")
        assertTrue(heavy > plain * 1.15, "a heavier weight is heavier: $heavy to $plain")
        assertTrue(underlined > plain * 1.1, "an underline is drawn: $underlined to $plain")
        assertTrue(abs(slanted - plain) < plain * 0.25 && (0 until SIDE * 24).any { i ->
            val x = i % SIDE; val y = 88 + i / SIDE
            rgb(frame, x, y) != rgb(frame, x, y - 48)
        }, "italic is the same letters, leaning")
        assertTrue(scene.cut.width in 1..60, "an ellipsis keeps a line to its width: ${scene.cut}")
        if (FontFamily.Monospace.fonts.isNotEmpty()) assertEquals(scene.narrow.width, scene.wide.width, "a font of one's own: every letter as wide")
        else assertTrue(scene.narrow.width < scene.wide.width)

        // The whole interface in another theme, where it stands.
        scene.ui.theme = Themes.material(dark = false)
        app.frames(4)
        frame = pixels()
        near(Themes.material(dark = false).primary, rgb(frame, 10, 10), "another theme: what composables read")
        near(Themes.material(dark = false).primary, rgb(frame, 50, 25), "another theme: koral-ui's own controls")
        near(CupertinoLightTheme.primary, rgb(frame, 50, 35), "the part with its own keeps it")

        // A slider let go of.
        at(50f, 180f); button(true); at(150f, 180f); app.frames(1)
        assertEquals(0, scene.finished)
        button(false); app.frames(2)
        assertEquals(1, scene.finished, "onValueChangeFinished")
        assertTrue(scene.slider > 0.5f, "and it moved: ${scene.slider}")

        // The keyboard, asked for.
        scene.focus.requestFocus()
        app.frames(3)
        input.feedText("k"); app.frames(3)
        assertEquals("k", scene.text, "a FocusRequester gives its field the keyboard")
        scene.ui.clearFocus()
        app.frames(2)
        input.feedText("x"); app.frames(3)
        assertEquals("k", scene.text, "clearFocus takes it away")

        // Held down half a second: a long press, and no tap.
        at(60f, 10f); button(true)
        Thread.sleep(600)
        app.frames(2)
        button(false); app.frames(2)
        assertEquals(1, scene.longPresses, "onLongPress")
        assertEquals(0, scene.taps, "which is not a tap")
        at(60f, 10f); button(true); button(false); app.frames(2)
        assertEquals(1, scene.taps)

        // Going out of view: there, less of it on the way, and then gone.
        fun green(bytes: ByteArray) = (0 until SIDE * SIDE).count { rgb(bytes, it % SIDE, it / SIDE) == listOf(0, 255, 0) }
        assertEquals(IntSize(20, 20), scene.grown)
        assertEquals(400, green(pixels()), "in view")
        scene.shown = false
        app.frames(2)       // it starts to go
        Thread.sleep(120)
        app.frames(2)
        val going = green(pixels())
        assertTrue(going < 400, "shrinking as it goes: $going")
        Thread.sleep(1500)
        app.frames(3)
        assertEquals(0, green(pixels()), "and gone")
    }
}
