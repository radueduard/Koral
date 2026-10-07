package koral.tests

import koral.Buffer
import koral.BufferType
import koral.BufferUsage
import koral.OffscreenSettings
import koral.Scene
import koral.compose.Alignment
import koral.compose.Arrangement
import koral.compose.Box
import koral.compose.Color
import koral.compose.DockLayout
import koral.compose.DockSpace
import koral.compose.Modifier
import koral.compose.Row
import koral.compose.background
import koral.compose.clickable
import koral.compose.dp
import koral.compose.fillMaxSize
import koral.compose.fillMaxWidth
import koral.compose.height
import koral.compose.setContent
import koral.compose.size
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

private const val W = 240
private const val H = 160

class TitleBarred : Scene() {
    val readback: Buffer = Buffer.Builder().setSize(W.toLong() * H * 4).setUsage(BufferUsage.eTransferDst)
        .setType(BufferType.eReadback).build()
    val layout = DockLayout()
    var plays = 0

    override fun initialize() {
        graph.add(Clear(0f, 0f, 1f))
        setContent {
            DockSpace(layout, Modifier.fillMaxSize(), multiViewport = false) {
                panel("viewport", "Viewport", titleBar = {
                    Row(Modifier.fillMaxWidth().height(36.dp).background(Color.Red),
                        horizontalArrangement = Arrangement.spacedBy(4.dp, Alignment.CenterHorizontally),
                        verticalAlignment = Alignment.CenterVertically) {
                        Box(Modifier.size(20.dp).background(Color.Green).clickable { plays++ })
                    }
                }) { Box(Modifier.fillMaxSize().background(Color.Yellow)) }
            }
        }
        graph.add(ReadScreen(readback))
    }
}

/** panel(titleBar = { ... }): the panel's own widget in its title bar, between its title and its buttons. */
class DockTitleBarTest {
    @Test
    fun aPanelsTitleBarShowsWhatItIsGivenBetweenItsTitleAndItsButtons(): Unit = headlessApp().use { app ->
        val scene = app.openOffscreen("TitleBarred", TitleBarred(), OffscreenSettings(width = W, height = H))
        fun pixel(x: Int, y: Int) = scene.readback.pixel(W, x, y)
        val red = listOf(255, 0, 0, 255); val green = listOf(0, 255, 0, 255); val yellow = listOf(255, 255, 0, 255)
        app.frames(3)

        // The panel is an island from 3 to 237 across and down; its close button ends 6 from its right, so the
        // title bar's widget ends at 207 — and the bar is the widget's 36 tall, from 3 to 39.
        assertEquals(red, pixel(205, 37), "the widget, up to the buttons and down the bar")
        assertEquals(yellow, pixel(205, 41), "the content under the taller bar")
        assertTrue(pixel(225, 20) != red, "the close button is past it")
        val greens = (0 until W).filter { pixel(it, 21) == green }
        assertTrue(greens.size in 19..20, "its box, 20 wide (19 whole pixels where centring puts it half way): ${greens.size}")
        val middle = (greens.first() + greens.last()) / 2f
        assertTrue(middle > 100f && middle < 200f, "in the middle of the room after the title: $middle")

        // A press on what takes one in it is its own; the bar is not picked up.
        val input = scene.input
        input.feedMousePosition(koral.Vec2(middle, 21f)); app.frames(1)
        input.feedMouseButton(koral.MouseButton.e1, true); app.frames(1)
        input.feedMouseButton(koral.MouseButton.e1, false); app.frames(2)
        assertEquals(1, scene.plays, "the box in the title bar was clicked")
        assertFalse(scene.layout.isFloating("viewport"))
    }
}
