package koral.tests

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import koral.Buffer
import koral.Key
import koral.MouseButton
import koral.OffscreenSettings
import koral.Scene
import koral.Vec2
import koral.compose.Box
import koral.compose.Color
import koral.compose.Column
import koral.compose.Constraints
import koral.compose.DropdownMenu
import koral.compose.DropdownMenuItem
import koral.compose.Icon
import koral.compose.Icons
import koral.compose.Layout
import koral.compose.Modifier
import koral.compose.PointerEventType
import koral.compose.Row
import koral.compose.ScrollState
import koral.compose.Text
import koral.compose.TextField
import koral.compose.TransformOrigin
import koral.compose.aspectRatio
import koral.compose.background
import koral.compose.dp
import koral.compose.fillMaxWidth
import koral.compose.graphicsLayer
import koral.compose.height
import koral.compose.pointerInput
import koral.compose.setContent
import koral.compose.size
import koral.compose.verticalScroll
import koral.compose.width
import kotlinx.coroutines.runBlocking
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotEquals
import kotlin.test.assertTrue

private const val SIDE = 260

class ComposeMore : Scene() {
    val readback: Buffer = readback(SIDE)
    var presses = 0
    var expanded by mutableStateOf(false)
    var picked = ""
    var text by mutableStateOf("")
    val scroll = ScrollState()

    override fun initialize() {
        graph.add(Clear(0f, 0f, 0f))
        setContent {
            Column {
                // 0 to 40: a layout of its own putting a red box at (40, 10), and a green one grown to twice its size.
                Row {
                    Layout(content = { Box(Modifier.size(10.dp).background(Color.Red)) }) { measurables, _ ->
                        val box = measurables[0].measure(Constraints())
                        layout(60, 40) { box.place(40, 10) }
                    }
                    Box(Modifier.graphicsLayer(scaleX = 2f, scaleY = 2f, transformOrigin = TransformOrigin(0f, 0f)).size(10.dp).background(Color.Green))
                }
                // 40 to 70: sixty wide, and half that tall.
                Box(Modifier.width(60.dp).aspectRatio(2f).background(Color.Blue))
                // 70 to 80: half the width.
                Box(Modifier.fillMaxWidth(0.5f).height(10.dp).background(Color.Red))
                // 80 to 104: an icon.
                Icon(Icons.Default.Add, contentDescription = "Add", tint = Color.White)
                // 104 to 124: what hears the pointer an event at a time, and a menu under it.
                Box {
                    Box(Modifier.size(30.dp, 20.dp).background(Color.Blue).pointerInput(Unit) {
                        awaitPointerEventScope { while (true) if (awaitPointerEvent().type == PointerEventType.Press) presses++ }
                    })
                    DropdownMenu(expanded, onDismissRequest = { expanded = false }) {
                        DropdownMenuItem(text = { Text("One") }, onClick = { picked = "one"; expanded = false })
                    }
                }
                // 124 on: a field of up to three lines, and something scrolled.
                TextField(text, { text = it }, maxLines = 3, width = 100.dp)
                Column(Modifier.height(30.dp).verticalScroll(scroll)) { Box(Modifier.size(20.dp, 200.dp).background(Color.Green)) }
            }
        }
        graph.add(ReadScreen(readback))
    }
}

/** What Compose has that koral-ui had to be given: a layout of one's own, transforms, shares, menus, icons, scroll state, several lines. */
class ComposeMoreTest {
    @Test
    fun theRestOfWhatComposeHas(): Unit = headlessApp().use { app ->
        val scene = app.openOffscreen("ComposeMore", ComposeMore(), OffscreenSettings(width = SIDE, height = SIDE))
        val input = scene.input
        fun at(x: Float, y: Float) { input.feedMousePosition(Vec2(x, y)); app.frames(1) }
        fun button(down: Boolean) { input.feedMouseButton(MouseButton.e1, down); app.frames(1) }
        fun click(x: Float, y: Float) { at(x, y); button(true); button(false); app.frames(2) }
        fun pixel(x: Int, y: Int) = scene.readback.pixel(SIDE, x, y)
        val red = listOf(255, 0, 0, 255); val green = listOf(0, 255, 0, 255); val blue = listOf(0, 0, 255, 255); val black = listOf(0, 0, 0, 255)

        at(250f, 250f); app.frames(3)
        assertEquals(red, pixel(45, 15), "Layout: placed where its rule said")
        assertEquals(black, pixel(5, 5))
        assertEquals(green, pixel(75, 15), "graphicsLayer: twice the size, from its top-left")
        assertEquals(blue, pixel(30, 65), "aspectRatio: half as tall as wide")
        assertNotEquals(blue, pixel(30, 73))
        assertEquals(red, pixel(100, 75), "fillMaxWidth(0.5f)")
        assertEquals(black, pixel(150, 75))
        assertNotEquals(black, pixel(12, 92), "an Icon draws")

        click(10f, 114f)
        assertEquals(1, scene.presses, "awaitPointerEvent: a press")

        scene.expanded = true
        app.frames(4)
        click(40f, 150f)
        assertEquals("one", scene.picked, "a DropdownMenuItem, in the menu under what it is beside")
        scene.expanded = true
        app.frames(4)
        click(250f, 250f)
        assertFalse(scene.expanded, "a press outside the menu asks for it to go")

        click(20f, 140f)
        input.feedText("a"); app.frames(2)
        input.feedKey(Key.eEnter, true); app.frames(1); input.feedKey(Key.eEnter, false); app.frames(1)
        input.feedText("b"); app.frames(3)
        assertEquals("a\nb", scene.text, "a field of several lines: Enter starts another")

        assertTrue(scene.scroll.maxValue in 1..400, "a ScrollState knows how far it can go: ${scene.scroll.maxValue}")
        runBlocking { scene.scroll.scrollTo(50) }
        app.frames(3)
        assertEquals(50, scene.scroll.value, "and goes where it is told")
    }
}
