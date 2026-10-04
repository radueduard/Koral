package koral.tests

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import koral.MouseButton
import koral.OffscreenSettings
import koral.Scene
import koral.Vec2
import koral.compose.Box
import koral.compose.Button
import koral.compose.Color
import koral.compose.Column
import koral.compose.Dialog
import koral.compose.IntSize
import koral.compose.Modifier
import koral.compose.Slider
import koral.compose.Tab
import koral.compose.TabRow
import koral.compose.Text
import koral.compose.TextAlign
import koral.compose.animateFloatAsState
import koral.compose.background
import koral.compose.detectDragGestures
import koral.compose.detectTapGestures
import koral.compose.dp
import koral.compose.onSizeChanged
import koral.compose.pointerInput
import koral.compose.setContent
import koral.compose.size
import koral.compose.tween
import koral.compose.width
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class ComposeApi : Scene() {
    var taps = 0
    var doubles = 0
    var dragged = 0f
    var stepped by mutableStateOf(0f)
    var tab by mutableStateOf(0)
    var asking by mutableStateOf(false)
    var answered = false
    var go by mutableStateOf(false)
    var animated = 0f
    var size = IntSize.Zero

    override fun initialize() {
        graph.add(Clear(0f, 0f, 0f))
        setContent {
            Column {
                // 0 to 40: a box that hears taps and drags, and says how big it is.
                Box(Modifier.size(40.dp).background(Color.Red).onSizeChanged { size = it }
                        .pointerInput(Unit) { detectTapGestures(onDoubleTap = { doubles++ }, onTap = { taps++ }) }
                        .pointerInput(Unit) { detectDragGestures { _, amount -> dragged += amount.x } })
                // 40 to 68: a slider that stops at the whole numbers from 0 to 10.
                Slider(stepped, { stepped = it }, valueRange = 0f..10f, steps = 9)
                // From 68: two tabs.
                TabRow(tab) {
                    Tab(tab == 0, { tab = 0 }, text = { Text("A") })
                    Tab(tab == 1, { tab = 1 }, text = { Text("B") })
                }
                Text("in the middle", Modifier.width(200.dp), textAlign = TextAlign.Center)
            }
            if (asking) Dialog(onDismissRequest = { asking = false }) {
                Button(onClick = { answered = true; asking = false }) { Text("Yes") }
            }
            val value by animateFloatAsState(if (go) 1f else 0f, tween(80))
            animated = value
        }
    }
}

/** Compose's own names doing what Compose's do: pointerInput, a stepped Slider, TabRow and Tab, Dialog, animate*AsState. */
class ComposeApiTest {
    @Test
    fun theNamesComposeHasDoWhatComposesDo(): Unit = headlessApp().use { app ->
        val scene = app.openOffscreen("ComposeApi", ComposeApi(), OffscreenSettings(width = 260, height = 260))
        val input = scene.input
        fun at(x: Float, y: Float) { input.feedMousePosition(Vec2(x, y)); app.frames(1) }
        fun button(down: Boolean) { input.feedMouseButton(MouseButton.e1, down); app.frames(1) }
        fun click(x: Float, y: Float) { at(x, y); button(true); button(false); app.frames(2) }

        at(250f, 250f); app.frames(3)
        assertEquals(IntSize(40, 40), scene.size, "onSizeChanged, in whole pixels")

        click(20f, 20f)
        assertEquals(1, scene.taps, "detectTapGestures: a tap")
        click(20f, 20f)
        assertEquals(1, scene.doubles, "and a second right after it: a double tap")

        at(10f, 20f); button(true); at(20f, 20f); at(30f, 20f); button(false); app.frames(2)
        assertEquals(20f, scene.dragged, 0.5f, "detectDragGestures: how far it was dragged")

        // A little past three tenths of the way along the track (8 to 192): the place nearest.
        click(8f + 184f * 0.32f, 54f)
        assertEquals(3f, scene.stepped, 0.001f, "a slider with steps stops only at them")

        click(52f, 86f)
        assertEquals(1, scene.tab, "the second tab")

        scene.asking = true
        app.frames(3)
        click(20f, 20f)
        assertEquals(1, scene.taps, "what is behind a dialog is deaf")
        assertFalse(scene.asking, "and a press outside the dialog asks for it to go")

        scene.go = true
        val until = System.nanoTime() + 3_000_000_000L
        while (scene.animated < 1f && System.nanoTime() < until) { app.frames(1); Thread.sleep(5) }
        assertEquals(1f, scene.animated, 0.0001f, "animateFloatAsState gets where it is going")
        assertTrue(scene.go)
    }
}
