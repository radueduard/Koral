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
import koral.compose.DockLayout
import koral.compose.DockSpace
import koral.compose.Modifier
import koral.compose.background
import koral.compose.dp
import koral.compose.fillMaxSize
import koral.compose.setContent
import koral.compose.size
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNotEquals

private const val W = 240
private const val H = 160

class Contained : Scene() {
    val readback: Buffer = Buffer.Builder().setSize(W.toLong() * H * 4).setUsage(BufferUsage.eTransferDst)
        .setType(BufferType.eReadback).build()
    // Where they float is said; how big is not: each is as big as what it shows.
    val layout = DockLayout().float("card", 20f, 20f).float("framed", 120f, 20f)
    var width by mutableStateOf(50)

    override fun initialize() {
        graph.add(Clear(0f, 0f, 1f))
        setContent {
            DockSpace(layout, Modifier.fillMaxSize(), multiViewport = false) {
                panel("card", "Card", dockable = false, showTitleBar = false) { Box(Modifier.size(width.dp, 30.dp).background(Color.Red)) }
                panel("framed", "F", closable = false) { Box(Modifier.size(60.dp, 40.dp).background(Color.Green)) }
            }
        }
        graph.add(ReadScreen(readback))
    }
}

/** DockLayout.float(panel, x, y): a float as big as what it shows. */
class ContainedFloatTest {
    @Test
    fun aFloatDeclaredWithNoSizeIsAsBigAsItsContent(): Unit = headlessApp().use { app ->
        val scene = app.openOffscreen("Contained", Contained(), OffscreenSettings(width = W, height = H))
        val blue = listOf(0, 0, 255, 255)
        val red = listOf(255, 0, 0, 255)
        val green = listOf(0, 255, 0, 255)
        fun pixel(x: Int, y: Int) = scene.readback.pixel(W, x, y)

        app.frames(3)
        // With no title bar: exactly its content, 50 by 30 from (20, 20).
        assertEquals(red, pixel(20, 20), "its first pixel")
        assertEquals(red, pixel(69, 49), "its last")
        assertEquals(blue, pixel(71, 35), "nothing past its content's width")
        assertEquals(blue, pixel(40, 51), "nor past its height")

        // With a title bar: a frame of one around it and a bar of 28 along its top, over the content, 60 by 40.
        assertEquals(green, pixel(125, 52), "its content starts under the title bar")
        assertEquals(green, pixel(180, 70), "and reaches its right edge")
        assertEquals(green, pixel(150, 88), "and its bottom one (the corner itself is rounded off)")
        assertNotEquals(green, pixel(150, 40), "the title bar is above it")
        assertNotEquals(green, pixel(150, 90), "and only its frame below")

        // It follows its content.
        scene.width = 60
        app.frames(3)
        assertEquals(red, pixel(79, 35), "grown with what it shows")
        assertNotEquals(red, pixel(81, 35), "and no further")
    }
}
