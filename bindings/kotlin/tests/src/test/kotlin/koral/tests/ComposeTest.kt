package koral.tests

import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import koral.Buffer
import koral.MouseButton
import koral.OffscreenSettings
import koral.Scene
import koral.SceneArgs
import koral.Vec2
import koral.compose.Arrangement
import koral.compose.Box
import koral.compose.Canvas
import koral.compose.Color
import koral.compose.ComposeUi
import koral.compose.Modifier
import koral.compose.Row
import koral.compose.Text
import koral.compose.background
import koral.compose.clickable
import koral.compose.dp
import koral.compose.fillMaxSize
import koral.compose.setContent
import koral.compose.size
import kotlin.test.Test
import kotlin.test.assertEquals

const val SIZE = 64

/** A square that counts its taps, going from red to blue, and a canvas drawing a green circle beside it. */
@Composable
fun Tapper(onTap: () -> Unit) {
    var taps by remember { mutableStateOf(0) }
    Row(horizontalArrangement = Arrangement.spacedBy(0.dp)) {
        Box(Modifier.size(32.dp).background(if (taps == 0) Color.Red else Color.Blue).clickable { taps++; onTap() }) {
            Text("$taps")
        }
        Canvas(Modifier.size(32.dp)) { drawRect(Color.Green) }
    }
}

class Composed(@Suppress("UNUSED_PARAMETER") args: SceneArgs) : Scene() {
    val readback: Buffer = readback(SIZE)
    var taps = 0
    lateinit var ui: ComposeUi

    override fun initialize() {
        graph.add(Clear(0f, 0f, 0f))
        ui = setContent { Tapper { taps++ } }
        graph.add(ReadScreen(readback))
    }
}

/** Compose on koral-ui: composables become widgets, taps reach the lambdas, and state changes recompose. */
class ComposeTest {
    private fun Composed.at(x: Int, y: Int) = readback.pixel(SIZE, x, y)

    @Test
    fun composablesDrawTapAndRecompose() = headlessApp().use { app ->
        app.register("Composed") { Composed(it) }
        val scene = app.openOffscreen("Composed", OffscreenSettings(width = SIZE, height = SIZE), SceneArgs()) as Composed
        scene.input.feedMousePosition(Vec2(60f, 60f))   // away: over the box, it shows it is hovered
        app.frames(3)

        assertEquals(listOf(255, 0, 0, 255), scene.at(4, 28), "the box, red before it is tapped")
        assertEquals(listOf(0, 255, 0, 255), scene.at(48, 16), "the canvas beside it")
        assertEquals(listOf(0, 0, 0, 255), scene.at(16, 48), "nothing below: the Row is as tall as what it holds")

        scene.input.feedMousePosition(Vec2(16f, 16f))
        app.frames(1)
        scene.input.feedMouseButton(MouseButton.e1, true)
        app.frames(1)
        scene.input.feedMouseButton(MouseButton.e1, false)
        app.frames(1)
        scene.input.feedMousePosition(Vec2(60f, 60f))
        app.frames(3)

        assertEquals(1, scene.taps, "the tap reached the lambda")
        assertEquals(listOf(0, 0, 255, 255), scene.at(4, 28), "the state change recomposed the box blue")
    }
}

class Filled(@Suppress("UNUSED_PARAMETER") args: SceneArgs) : Scene() {
    val readback: Buffer = readback(SIZE)
    override fun initialize() {
        setContent { Box(Modifier.fillMaxSize().background(Color.Blue)) { Text("corner") } }
        graph.add(ReadScreen(readback))
    }
}

/** Modifiers reach koral-ui: fillMaxSize takes the whole screen. (A class of its own: one application per process.) */
class ComposeFillTest {
    @Test
    fun fillMaxSizeFillsTheScreen() = headlessApp().use { app ->
        app.register("Filled") { Filled(it) }
        val scene = app.openOffscreen("Filled", OffscreenSettings(width = SIZE, height = SIZE), SceneArgs()) as Filled
        scene.input.feedMousePosition(Vec2(-10f, -10f))
        app.frames(3)
        assertEquals(listOf(0, 0, 255, 255), scene.readback.pixel(SIZE, SIZE - 2, SIZE - 2))
    }
}
