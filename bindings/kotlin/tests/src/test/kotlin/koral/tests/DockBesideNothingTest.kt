package koral.tests

import koral.Buffer
import koral.BufferType
import koral.BufferUsage
import koral.MouseButton
import koral.OffscreenSettings
import koral.Scene
import koral.Vec2
import koral.compose.dp
import koral.compose.Box
import koral.compose.Color
import koral.compose.ComposeUi
import koral.compose.DockLayout
import koral.compose.DockSpace
import koral.compose.Modifier
import koral.compose.Rect
import koral.compose.background
import koral.compose.fillMaxSize
import koral.compose.setContent
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

private const val SPACE_W = 240
private const val SPACE_H = 160

class OneFloat : Scene() {
    val readback: Buffer = Buffer.Builder().setSize(SPACE_W.toLong() * SPACE_H * 4).setUsage(BufferUsage.eTransferDst)
        .setType(BufferType.eReadback).build()
    val layout = DockLayout().float("tools", Rect(80f, 40f, 200f, 120f))
    lateinit var ui: ComposeUi

    override fun initialize() {
        graph.add(Clear(0f, 0f, 1f))
        ui = setContent {
            DockSpace(layout, Modifier.fillMaxSize(), multiViewport = false, stripeGap = 0.dp) {
                panel("tools", "Tools", closable = false) { Box(Modifier.fillMaxSize().background(Color.Red)) }
            }
        }
        graph.add(ReadScreen(readback))
    }
}

/** A space with nothing docked in it: a panel docks down a side of it, behind a button in a stripe, and the middle stays empty. */
class DockBesideNothingTest {
    @Test
    fun aMarginOfTheSpaceDocksDownThatSideAndTheMiddleStaysEmpty(): Unit = headlessApp().use { app ->
        val scene = app.openOffscreen("OneFloat", OneFloat(), OffscreenSettings(width = SPACE_W, height = SPACE_H))
        val input = scene.input
        val blue = listOf(0, 0, 255, 255)
        val red = listOf(255, 0, 0, 255)
        fun pixel(x: Int, y: Int) = scene.readback.pixel(SPACE_W, x, y)
        fun at(x: Float, y: Float) { input.feedMousePosition(Vec2(x, y)); app.frames(1) }
        fun button(down: Boolean) { input.feedMouseButton(MouseButton.e1, down); app.frames(1) }
        fun drag(fromX: Float, fromY: Float, toX: Float, toY: Float) {
            at(fromX, fromY); button(true)
            at((fromX + toX) / 2, (fromY + toY) / 2); at(toX, toY); at(toX, toY)
            button(false); app.frames(3)
        }

        at(230f, 150f); app.frames(3)
        assertTrue(scene.layout.isFloating("tools"))

        // By its title, into the space's left margin: docked down the left side, its button in a stripe at the edge.
        drag(100f, 55f, 10f, 80f)
        at(230f, 150f); app.frames(2)
        assertFalse(scene.layout.isFloating("tools"), "docked")
        assertTrue(scene.layout.isShown("tools"))
        assertEquals(red, pixel(60, 100), "down the left, beside the stripe and under its title bar")
        assertEquals(blue, pixel(150, 80), "the rest of the space is empty: the scene shows")
        at(150f, 80f)
        assertFalse(scene.ui.wantsPointer, "and the pointer there is the scene's")

        // Where it is docked, and that it is open, is kept with the layout.
        val other = DockLayout()
        assertTrue(other.load(scene.layout.save()))
        assertEquals(scene.layout.save(), other.save())

        // Its button folds it away, and opens it again.
        at(19f, 19f); button(true); button(false); app.frames(3)
        assertFalse(scene.layout.isShown("tools"), "folded away")
        assertFalse(scene.layout.isFloating("tools"))
        at(230f, 150f); app.frames(2)
        assertEquals(blue, pixel(60, 100), "the scene where it was")
        at(19f, 19f); button(true); button(false); app.frames(3)
        at(230f, 150f); app.frames(2)
        assertTrue(scene.layout.isShown("tools"))
        assertEquals(red, pixel(60, 100), "open again")

        // Its button, dragged into the right margin: down the right side instead.
        drag(19f, 19f, 230f, 80f)
        at(120f, 150f); app.frames(2)
        assertFalse(scene.layout.isFloating("tools"))
        assertEquals(red, pixel(180, 100), "down the right now")
        assertEquals(blue, pixel(60, 100), "and nothing on the left")

        // And into the middle of what is empty — about its centre: docked there, with all that the edges leave.
        drag(221f, 19f, 72f, 80f)
        at(230f, 5f); app.frames(2)
        assertFalse(scene.layout.isFloating("tools"))
        assertTrue(scene.layout.isShown("tools"))
        assertEquals(red, pixel(60, 100), "in the middle")
        assertEquals(red, pixel(200, 140), "which is the whole space, with nothing round its edge")
    }
}
