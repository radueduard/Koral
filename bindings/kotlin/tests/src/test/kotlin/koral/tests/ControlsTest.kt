package koral.tests

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
import koral.compose.Alignment
import koral.compose.Arrangement
import koral.compose.Button
import koral.compose.Canvas
import koral.compose.Checkbox
import koral.compose.Color
import koral.compose.Column
import koral.compose.LinearProgressIndicator
import koral.compose.Modifier
import koral.compose.Path
import koral.compose.Row
import koral.compose.Slider
import koral.compose.Spacer
import koral.compose.Stroke
import koral.compose.Switch
import koral.compose.Text
import koral.compose.TextField
import koral.compose.Theme
import koral.compose.dp
import koral.compose.height
import koral.compose.setContent
import koral.compose.size
import koral.compose.width
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

const val WIDE = 256

class Controls(@Suppress("UNUSED_PARAMETER") args: SceneArgs) : Scene() {
    val readback: Buffer = readback(WIDE)
    var clicks = 0
    var typed = ""
    var compositions = 0
    var labelCompositions = 0

    override fun initialize() {
        graph.add(Clear(0f, 0f, 0f))
        setContent(Theme.Light) {
            var count by remember { mutableStateOf(0) }
            var checked by remember { mutableStateOf(false) }
            var level by remember { mutableStateOf(0.5f) }
            compositions++
            Column(verticalArrangement = Arrangement.spacedBy(4.dp), horizontalAlignment = Alignment.Start) {
                Button(onClick = { count++; clicks = count }) { labelCompositions++; Text("Clicked $count") }
                Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.width(200.dp)) {
                    Checkbox(checked, { checked = it })
                    Spacer(Modifier.weight(1f))
                    Switch(checked, { checked = it })
                }
                Slider(level, { level = it }, Modifier.width(120.dp))
                LinearProgressIndicator({ level }, Modifier.width(120.dp))
                TextField("", { typed = it }, placeholder = { Text("name") }, width = 120.dp)
                Spacer(Modifier.height(4.dp))
                Canvas(Modifier.size(40.dp)) {
                    drawPath(Path().apply { moveTo(0f, 40f); lineTo(20f, 0f); lineTo(40f, 40f); close() }, Color.Magenta)
                    drawArc(Color.Cyan, 0f, 180f, useCenter = false, style = Stroke(width = 3f))
                }
            }
        }
        graph.add(ReadScreen(readback))
    }
}

/** The controls, composed: each one builds, draws, and calls back into Kotlin. */
class ControlsTest {
    @Test
    fun controlsBuildDrawAndCallBack() = headlessApp().use { app ->
        app.register("Controls") { Controls(it) }
        val scene = app.openOffscreen("Controls", OffscreenSettings(width = WIDE, height = WIDE), SceneArgs()) as Controls
        val input = scene.input
        input.feedMousePosition(Vec2(250f, 250f))
        app.frames(3)
        val composedOnce = scene.compositions
        val labelOnce = scene.labelCompositions

        // The button, at the top-left, in the light theme's primary colour.
        // Sampled in from its corner, which a pill does not reach: coral.
        val button = scene.readback.pixel(WIDE, 20, 8)
        assertTrue(button[0] > 200 && button[0] > button[1] && button[1] > button[2], "the light theme's primary is coral: $button")
        // The canvas's triangle, at the bottom of the column.
        val bytes = scene.readback.read()
        val magenta = (0 until WIDE * WIDE).any { i ->
            (0 until 4).map { bytes[i * 4 + it].toInt() and 0xff } == listOf(255, 0, 255, 255)
        }
        assertTrue(magenta, "the Canvas drew its magenta path")

        app.frames(3)
        assertEquals(composedOnce, scene.compositions, "nothing changed: nothing recomposed")

        input.feedMousePosition(Vec2(6f, 6f)); app.frames(1)
        input.feedMouseButton(MouseButton.e1, true); app.frames(1)
        input.feedMouseButton(MouseButton.e1, false); app.frames(2)
        assertEquals(1, scene.clicks, "the Button's onClick ran")
        assertEquals(labelOnce + 1, scene.labelCompositions, "the state change recomposed the label that reads it")
        assertEquals(composedOnce, scene.compositions, "and only that: the column around it does not read it")
    }
}
