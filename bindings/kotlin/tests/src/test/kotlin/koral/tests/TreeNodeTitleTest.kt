package koral.tests

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import koral.Buffer
import koral.BufferType
import koral.BufferUsage
import koral.OffscreenSettings
import koral.Scene
import koral.compose.Box
import koral.compose.Color
import koral.compose.Column
import koral.compose.Modifier
import koral.compose.TreeNode
import koral.compose.background
import koral.compose.clickable
import koral.compose.dp
import koral.compose.dragSource
import koral.compose.fillMaxSize
import koral.compose.height
import koral.compose.setContent
import koral.compose.size
import koral.compose.width
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

private const val W = 240
private const val H = 120

class TitledTree : Scene() {
    val readback: Buffer = Buffer.Builder().setSize(W.toLong() * H * 4).setUsage(BufferUsage.eTransferDst)
        .setType(BufferType.eReadback).build()
    var expanded by mutableStateOf(false)
    var taps = 0
    var presses = 0

    override fun initialize() {
        graph.add(Clear(0f, 0f, 1f))
        setContent {
            Column(Modifier.fillMaxSize()) {
                TreeNode(expanded, { expanded = it }, onClick = { taps++ }, title = {
                    Box(Modifier.size(16.dp).background(Color.Green).clickable { presses++ })
                    Box(Modifier.weight(1f).height(12.dp).background(Color.Red))
                }) {
                    TreeNode("Child", leaf = true)
                }
            }
        }
        graph.add(ReadScreen(readback))
    }
}

class DraggedTitledTree : Scene() {
    val readback: Buffer = Buffer.Builder().setSize(W.toLong() * H * 4).setUsage(BufferUsage.eTransferDst)
        .setType(BufferType.eReadback).build()

    override fun initialize() {
        graph.add(Clear(0f, 0f, 1f))
        setContent {
            Box(Modifier.width(120.dp)) {
                TreeNode(false, {}, Modifier.dragSource("node", "it"), leaf = true, title = {
                    Box(Modifier.weight(1f).height(12.dp).background(Color.Red))
                })
            }
        }
        graph.add(ReadScreen(readback))
    }
}

/** TreeNode(title = { ... }): a row of the caller's own after the arrow, whose controls keep their presses. */
class TreeNodeTitleTest {
    @Test
    fun aTitledNodesRowIsItsTitleAndOnlyItsArrowOpensIt(): Unit = headlessApp().use { app ->
        val scene = app.openOffscreen("TitledTree", TitledTree(), OffscreenSettings(width = W, height = H))
        fun pixel(x: Int, y: Int) = scene.readback.pixel(W, x, y)
        val red = listOf(255, 0, 0, 255); val green = listOf(0, 255, 0, 255)
        app.frames(3)

        val row = (0 until H).firstOrNull { y -> (0 until W).any { pixel(it, y) == green } } ?: error("the title's button is not drawn")
        val y = row + 6
        val greens = (0 until W).filter { pixel(it, y) == green }
        val reds = (0 until W).filter { pixel(it, y) == red }
        assertTrue(greens.isNotEmpty() && reds.isNotEmpty(), "both of the title's boxes are drawn")
        assertTrue(greens.first() > 16, "after the arrow: ${greens.first()}")
        assertTrue(reds.first() > greens.last(), "the boxes in the order given")
        assertTrue(reds.last() > W - 16, "the title fills the rest of the row: it ends at ${reds.last()}")

        val input = scene.input
        fun press(x: Float) {
            input.feedMousePosition(koral.Vec2(x, y.toFloat())); app.frames(1)
            input.feedMouseButton(koral.MouseButton.e1, true); app.frames(1)
            input.feedMouseButton(koral.MouseButton.e1, false); app.frames(2)
        }

        press((greens.first() + greens.last()) / 2f)
        assertEquals(1, scene.presses, "the title's own control hears its press")
        assertEquals(0, scene.taps, "and the node does not")
        assertEquals(false, scene.expanded, "nor is it opened")

        press((reds.first() + reds.last()) / 2f)
        assertEquals(1, scene.taps, "a press elsewhere on the row taps the node")
        assertEquals(false, scene.expanded, "without opening it")

        press(greens.first() / 2f)
        assertEquals(true, scene.expanded, "the arrow opens it")
        assertEquals(1, scene.taps, "and does not tap it")
    }
}

/** A titled node in hand: shown as it is where it was — its title filling its own row, not the overlay's. */
class TreeNodeDragTest {
    @Test
    fun aDraggedTitledNodeIsShownAsWideAsItIs(): Unit = headlessApp().use { app ->
        val scene = app.openOffscreen("DraggedTitledTree", DraggedTitledTree(), OffscreenSettings(width = W, height = H))
        fun reddish(x: Int, y: Int) = scene.readback.pixel(W, x, y).let { it[0] > 150 && it[1] < 60 }
        app.frames(3)
        val y = (0 until H).firstOrNull { y -> (0 until W).any { reddish(it, y) } } ?: error("the title is not drawn")
        val start = (0 until W).first { reddish(it, y) }
        val input = scene.input
        input.feedMousePosition(koral.Vec2(start + 10f, y.toFloat())); app.frames(1)
        input.feedMouseButton(koral.MouseButton.e1, true); app.frames(1)
        for (step in 1..8) { input.feedMousePosition(koral.Vec2(start + 10f + step * 5f, y + step * 5f)); app.frames(1) }
        app.frames(2)

        // Held 40 to the right and down, over nothing: what is in hand is the row, no wider than it was.
        val row = y + 40
        val reds = (0 until W).filter { reddish(it, row) }
        assertTrue(reds.isNotEmpty(), "the row is shown in hand")
        assertTrue(reds.last() < 120 + 40 + 8, "and ends where it would: at ${reds.last()}, not the screen's edge")
        input.feedMouseButton(koral.MouseButton.e1, false); app.frames(2)
    }
}
