package koral.tests

import koral.Buffer
import koral.OffscreenSettings
import koral.Scene
import koral.compose.Box
import koral.compose.Color
import koral.compose.Modifier
import koral.compose.UiDebug
import koral.compose.background
import koral.compose.dp
import koral.compose.padding
import koral.compose.setContent
import koral.compose.size
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNotEquals

class Outlined : Scene() {
    val readback: Buffer = readback(SIZE)

    override fun initialize() {
        graph.add(Clear(0f, 0f, 0f))
        // A red square of 24, ten in from the corner.
        setContent { Box(Modifier.padding(10.dp).size(24.dp).background(Color.Red)) }
        graph.add(ReadScreen(readback))
    }
}

/** UiDebug.paintBounds: containers outlined where they were laid out, turned on and off while running. */
class DebugBoundsTest {
    @Test
    fun boundsAreOutlinedWhileTheFlagIsOn(): Unit = headlessApp().use { app ->
        val scene = app.openOffscreen("Outlined", Outlined(), OffscreenSettings(width = SIZE, height = SIZE))
        val red = listOf(255, 0, 0, 255)
        fun pixel(x: Int, y: Int) = scene.readback.pixel(SIZE, x, y)

        app.frames(3)
        assertEquals(red, pixel(10, 20), "off: the square's edge is the square")
        assertEquals(red, pixel(20, 20), "and its middle")

        UiDebug.paintBounds = true
        app.frames(3)
        assertNotEquals(red, pixel(10, 20), "on: a line along the square's edge")
        assertEquals(red, pixel(20, 20), "and its middle untouched")

        UiDebug.paintBounds = false
        app.frames(3)
        assertEquals(red, pixel(10, 20), "off again: as it was")
    }
}
