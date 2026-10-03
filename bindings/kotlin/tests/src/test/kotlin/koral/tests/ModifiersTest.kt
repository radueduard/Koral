package koral.tests

import koral.Buffer
import koral.MouseButton
import koral.OffscreenSettings
import koral.Scene
import koral.SceneArgs
import koral.Vec2
import koral.compose.Box
import koral.compose.CircleShape
import koral.compose.Color
import koral.compose.Column
import koral.compose.Modifier
import koral.compose.background
import koral.compose.clickable
import koral.compose.clip
import koral.compose.dp
import koral.compose.offset
import koral.compose.setContent
import koral.compose.size
import kotlin.test.Test
import kotlin.test.assertEquals

class Modified(@Suppress("UNUSED_PARAMETER") args: SceneArgs) : Scene() {
    val readback: Buffer = readback(SIZE)
    var clicks = 0
    override fun initialize() {
        graph.add(Clear(0f, 0f, 0f))
        setContent {
            Column {
                Box(Modifier.offset(x = 16.dp).size(16.dp).background(Color.Red).clickable { clicks++ })
                Box(Modifier.size(16.dp))
                Box(Modifier.size(32.dp).clip(CircleShape).background(Color.Blue))
            }
        }
        graph.add(ReadScreen(readback))
    }
}

/** Modifier.offset moves drawing and clicks, not layout; Modifier.clip cuts what follows to a shape. */
class ModifiersTest {
    @Test
    fun offsetAndClip() = headlessApp().use { app ->
        app.register("Modified") { Modified(it) }
        val scene = app.openOffscreen("Modified", OffscreenSettings(width = SIZE, height = SIZE), SceneArgs()) as Modified
        fun at(x: Int, y: Int) = scene.readback.pixel(SIZE, x, y)
        val input = scene.input
        input.feedMousePosition(Vec2(60f, 60f))
        app.frames(3)
        assertEquals(listOf(255, 0, 0, 255), at(24, 8), "drawn 16 to the right")
        assertEquals(listOf(0, 0, 0, 255), at(8, 8), "and not where it was laid out")
        assertEquals(listOf(0, 0, 255, 255), at(16, 48), "the circle's middle")
        assertEquals(listOf(0, 0, 0, 255), at(1, 33), "its corner, clipped")

        input.feedMousePosition(Vec2(24f, 8f)); app.frames(1)
        input.feedMouseButton(MouseButton.e1, true); app.frames(1)
        input.feedMouseButton(MouseButton.e1, false); app.frames(1)
        assertEquals(1, scene.clicks, "clicked where it is drawn")
    }
}
