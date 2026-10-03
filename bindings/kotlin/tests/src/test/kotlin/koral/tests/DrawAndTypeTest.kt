package koral.tests

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import koral.Buffer
import koral.Key
import koral.MouseButton
import koral.OffscreenSettings
import koral.Scene
import koral.SceneArgs
import koral.Vec2
import koral.compose.Canvas
import koral.compose.Color
import koral.compose.Column
import koral.compose.Modifier
import koral.compose.TextField
import koral.compose.dp
import koral.compose.setContent
import koral.compose.size
import kotlin.test.Test
import kotlin.test.assertEquals

class DrawAndType(@Suppress("UNUSED_PARAMETER") args: SceneArgs) : Scene() {
    val readback: Buffer = readback(SIZE)
    val color = mutableStateOf(Color.Red)
    var text by mutableStateOf("")
    var compositions = 0

    override fun initialize() {
        graph.add(Clear(0f, 0f, 0f))
        setContent {
            compositions++
            Column {
                Canvas(Modifier.size(16.dp)) { drawRect(color.value) }   // read while drawing, not composing
                TextField(text, { text = it.uppercase().take(3) }, width = 60.dp)
            }
        }
        graph.add(ReadScreen(readback))
    }
}

/** Compose's draw phase and its controlled text fields. */
class DrawAndTypeTest {
    @Test
    fun canvasRedrawsAndTextFieldIsControlled() = headlessApp().use { app ->
        app.register("DrawAndType") { DrawAndType(it) }
        val scene = app.openOffscreen("DrawAndType", OffscreenSettings(width = SIZE, height = SIZE), SceneArgs()) as DrawAndType
        val input = scene.input
        input.feedMousePosition(Vec2(60f, 2f))
        app.frames(3)
        assertEquals(listOf(255, 0, 0, 255), scene.readback.pixel(SIZE, 8, 8))
        val composed = scene.compositions

        scene.color.value = Color.Blue
        app.frames(2)
        assertEquals(listOf(0, 0, 255, 255), scene.readback.pixel(SIZE, 8, 8), "the Canvas drew again")
        assertEquals(composed, scene.compositions, "without composing again")

        // Focus the field and type: what shows is what onValueChange made of it.
        input.feedMousePosition(Vec2(20f, 26f)); app.frames(1)
        input.feedMouseButton(MouseButton.e1, true); app.frames(1)
        input.feedMouseButton(MouseButton.e1, false); app.frames(1)
        input.feedText("abcd"); app.frames(3)
        assertEquals("ABC", scene.text, "onValueChange made capitals and kept three")

        scene.text = "xy"; app.frames(2)
        input.feedText("z"); app.frames(3)
        assertEquals("XYZ", scene.text, "the field showed what text was set to, and typed after it")

        input.feedKey(Key.eBackspace, true); app.frames(1); input.feedKey(Key.eBackspace, false); app.frames(2)
        assertEquals("XY", scene.text)
    }
}
