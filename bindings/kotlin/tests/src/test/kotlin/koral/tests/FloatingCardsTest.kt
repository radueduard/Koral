package koral.tests

import koral.Buffer
import koral.BufferType
import koral.BufferUsage
import koral.MouseButton
import koral.OffscreenSettings
import koral.Scene
import koral.Vec2
import koral.compose.Box
import koral.compose.Color
import koral.compose.ComposeUi
import koral.compose.DockLayout
import koral.compose.DockSpace
import koral.compose.Modifier
import koral.compose.Rect
import koral.compose.background
import koral.compose.clickable
import koral.compose.dp
import koral.compose.fillMaxSize
import koral.compose.padding
import koral.compose.setContent
import koral.compose.size
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotEquals
import kotlin.test.assertTrue

private const val WIDTH = 240
private const val HEIGHT = 160

class Cards : Scene() {
    val readback: Buffer = Buffer.Builder().setSize(WIDTH.toLong() * HEIGHT * 4).setUsage(BufferUsage.eTransferDst)
        .setType(BufferType.eReadback).build()
    val layout = DockLayout().float("card", Rect(40f, 30f, 160f, 110f))
    lateinit var ui: ComposeUi

    override fun initialize() {
        graph.add(Clear(0f, 0f, 1f))
        ui = setContent {
            DockSpace(layout, Modifier.fillMaxSize(), multiViewport = false) {
                panel("card", "Card", closable = false) { Box(Modifier.fillMaxSize().background(Color.Red)) }
            }
        }
        graph.add(ReadScreen(readback))
    }
}

class BareCards : Scene() {
    val readback: Buffer = Buffer.Builder().setSize(WIDTH.toLong() * HEIGHT * 4).setUsage(BufferUsage.eTransferDst)
        .setType(BufferType.eReadback).build()
    val layout = DockLayout().float("card", Rect(20f, 20f, 140f, 100f)).float("other", Rect(150f, 20f, 230f, 100f))
    var taps = 0

    override fun initialize() {
        graph.add(Clear(0f, 0f, 1f))
        setContent {
            DockSpace(layout, Modifier.fillMaxSize(), multiViewport = false) {
                // Only its content: red, with twenty of padding around a green square that takes taps.
                panel("card", "Card", dockable = false, showTitleBar = false) {
                    Box(Modifier.fillMaxSize().background(Color.Red).padding(20.dp)) {
                        Box(Modifier.size(40.dp).background(Color.Green).clickable { taps++ })
                    }
                }
                panel("other", "Other", closable = false) { Box(Modifier.fillMaxSize()) }
            }
        }
        graph.add(ReadScreen(readback))
    }
}

/** A dock space with nothing docked: see-through, with what floats in it as cards over the scene. */
class FloatingCardsTest {
    @Test
    fun aSpaceOfOnlyFloatsShowsTheSceneAndItsCardsMove(): Unit = headlessApp().use { app ->
        val scene = app.openOffscreen("Cards", Cards(), OffscreenSettings(width = WIDTH, height = HEIGHT))
        val input = scene.input
        val blue = listOf(0, 0, 255, 255)
        val red = listOf(255, 0, 0, 255)
        fun pixel(x: Int, y: Int) = scene.readback.pixel(WIDTH, x, y)
        fun at(x: Float, y: Float) { input.feedMousePosition(Vec2(x, y)); app.frames(1) }
        fun button(down: Boolean) { input.feedMouseButton(MouseButton.e1, down); app.frames(1) }

        at(220f, 150f); app.frames(3)
        assertEquals(blue, pixel(220, 150), "the scene shows where nothing is docked")
        assertFalse(scene.ui.wantsPointer, "and the pointer there is the scene's")
        assertEquals(red, pixel(100, 90), "the card's content, under its title bar")
        assertNotEquals(red, pixel(40, 30), "its corner is rounded off")

        at(100f, 90f)
        assertTrue(scene.ui.wantsPointer, "over the card the pointer is the interface's")

        // Dragged by the empty part of its title bar, between its title and its right end, the card moves and stays floating.
        at(150f, 44f); button(true); at(170f, 54f); at(190f, 64f); button(false); app.frames(2)
        at(220f, 150f); app.frames(2)
        assertTrue(scene.layout.isFloating("card"), "still floating")
        assertEquals(red, pixel(140, 110), "the card, 40 right and 20 down")
        assertEquals(blue, pixel(45, 40), "and the scene where it was, clear of its shadow")
    }
}

/** A panel with no title bar that never docks: a card that is only its content. In a class of its own: an application is one per process. */
class BareCardsTest {
    @Test
    fun aCardWithNoTitleBarIsItsContentAndNeverDocks(): Unit = headlessApp().use { app ->
        val scene = app.openOffscreen("BareCards", BareCards(), OffscreenSettings(width = WIDTH, height = HEIGHT))
        val input = scene.input
        val blue = listOf(0, 0, 255, 255)
        val red = listOf(255, 0, 0, 255)
        val green = listOf(0, 255, 0, 255)
        fun pixel(x: Int, y: Int) = scene.readback.pixel(WIDTH, x, y)
        fun at(x: Float, y: Float) { input.feedMousePosition(Vec2(x, y)); app.frames(1) }
        fun button(down: Boolean) { input.feedMouseButton(MouseButton.e1, down); app.frames(1) }

        at(5f, 150f); app.frames(3)
        assertEquals(red, pixel(21, 21), "no bar and no frame: its content from its very corner")
        assertEquals(green, pixel(60, 60), "and what the content holds")

        // Dragged by its padding, it moves.
        at(25f, 25f); button(true); at(45f, 35f); at(75f, 55f); button(false); app.frames(2)
        at(5f, 150f); app.frames(2)
        assertEquals(blue, pixel(25, 25), "the scene where it was")
        assertEquals(red, pixel(75, 55), "the card, 50 right and 30 down")
        assertEquals(green, pixel(110, 90), "with what it holds")

        // What it holds is still itself: a tap is a tap, and a drag begun on it does not move the card.
        at(110f, 90f); button(true); button(false); app.frames(2)
        assertEquals(1, scene.taps, "the square was tapped")
        at(110f, 90f); button(true); at(120f, 95f); at(150f, 110f); button(false); app.frames(2)
        at(5f, 150f); app.frames(2)
        assertEquals(red, pixel(75, 55), "the card stayed where it was")

        // It never docks: not when told to, and not as a target for another panel's tab.
        scene.layout.dock("card")
        app.frames(3)
        assertTrue(scene.layout.isFloating("card"), "told to dock, it floats still")
        at(160f, 34f); button(true); at(170f, 60f); at(180f, 120f); button(false); app.frames(3)   // onto the card's edge
        at(5f, 150f); app.frames(2)
        assertTrue(scene.layout.isFloating("card") && scene.layout.isFloating("other"), "both float, each on its own")
        assertEquals(red, pixel(80, 60), "the card is still only its content, where it was")
    }
}
