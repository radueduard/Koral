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
import koral.compose.Box
import koral.compose.Color
import koral.compose.LazyColumn
import koral.compose.Modifier
import koral.compose.background
import koral.compose.clickable
import koral.compose.dp
import koral.compose.fillMaxSize
import koral.compose.items
import koral.compose.setContent
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class Listing(@Suppress("UNUSED_PARAMETER") args: SceneArgs) : Scene() {
    val readback: Buffer = readback(SIZE)
    val composed = mutableSetOf<Int>()
    val rows = mutableStateOf((0 until 1000).toList())

    override fun initialize() {
        graph.add(Clear(0f, 0f, 0f))
        setContent {
            LazyColumn(Modifier.fillMaxSize(), itemHeight = 16.dp) {
                items(rows.value, key = { it }) { row ->
                    composed += row
                    var taps by remember { mutableStateOf(0) }
                    val color = if (taps > 0) Color.Green else if (row % 2 == 0) Color.Red else Color.Blue
                    Box(Modifier.fillMaxSize().background(color).clickable { taps++ })
                }
            }
        }
        graph.add(ReadScreen(readback))
    }
}

/** LazyColumn: only what is in view is composed; items keep their state while they stay. */
class LazyColumnTest {
    @Test
    fun composesWhatIsInView() = headlessApp().use { app ->
        app.register("Listing") { Listing(it) }
        val scene = app.openOffscreen("Listing", OffscreenSettings(width = SIZE, height = SIZE), SceneArgs()) as Listing
        fun at(x: Int, y: Int) = scene.readback.pixel(SIZE, x, y)
        val input = scene.input
        input.feedMousePosition(Vec2(-10f, -10f))
        app.frames(3)
        assertEquals(listOf(255, 0, 0, 255), at(8, 8), "row 0")
        assertEquals(listOf(0, 0, 255, 255), at(8, 24), "row 1")
        assertTrue(scene.composed.size <= 64, "what koral-ui builds before it knows its height (64), not a thousand: ${scene.composed.size}")

        input.feedMousePosition(Vec2(8f, 24f)); app.frames(1)
        input.feedMouseButton(MouseButton.e1, true); app.frames(1)
        input.feedMouseButton(MouseButton.e1, false); app.frames(1)
        input.feedMousePosition(Vec2(-10f, -10f)); app.frames(2)
        assertEquals(listOf(0, 255, 0, 255), at(8, 24), "row 1 remembers it was tapped")

        // A row put in front: keyed, the tapped row keeps its state as it moves down.
        scene.rows.value = listOf(-1) + scene.rows.value
        app.frames(3)
        assertEquals(listOf(0, 0, 255, 255), at(8, 8), "the new row (-1 is odd)")
        assertEquals(listOf(255, 0, 0, 255), at(8, 24), "row 0, moved down")
        assertEquals(listOf(0, 255, 0, 255), at(8, 40), "row 1, moved down and still tapped")

        // Far down and back: what left the kept range was disposed, and comes back new.
        input.feedMousePosition(Vec2(8f, 8f))
        repeat(20) { input.feedScroll(Vec2(0f, -10f)); app.frames(1) }
        app.frames(2)
        val far = scene.composed.max()
        assertTrue(far > 100, "scrolled far enough to compose row $far")
        repeat(20) { input.feedScroll(Vec2(0f, 10f)); app.frames(1) }
        input.feedMousePosition(Vec2(-10f, -10f)); app.frames(3)
        assertEquals(listOf(0, 0, 255, 255), at(8, 40), "row 1 again, composed afresh")
    }
}
