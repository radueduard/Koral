package koral.tests

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import koral.MouseButton
import koral.OffscreenSettings
import koral.Scene
import koral.Vec2
import koral.compose.Alignment
import koral.compose.Box
import koral.compose.Color
import koral.compose.DockLayout
import koral.compose.DockSpace
import koral.compose.Modifier
import koral.compose.RoundedCornerShape
import koral.compose.background
import koral.compose.clickable
import koral.compose.dp
import koral.compose.dragSource
import koral.compose.dropTarget
import koral.compose.fillMaxSize
import koral.compose.padding
import koral.compose.setContent
import koral.compose.size
import koral.compose.DockArea
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotEquals
import kotlin.test.assertTrue

class Docking : Scene() {
    val layout = DockLayout().dock("src", DockArea.Center).dock("dst", DockArea.Right)
    var dropped: String? = null
    var accepted: Boolean? = null
    var entered = 0
    var taps = 0
    var changes = 0

    override fun initialize() {
        graph.add(Clear(0f, 0f, 0f))
        setContent {
            DockSpace(layout, stripeGap = 0.dp, onLayoutChanged = { changes++ }) {
                panel("src", "Source") {
                    Box(Modifier.padding(4.dp).size(40.dp).background(Color.Red, RoundedCornerShape(6.dp))
                        .dragSource("word", "hello", onDragEnd = { accepted = it }))
                }
                panel("dst", "Target", closable = false) {
                    // State of its own: kept wherever the panel goes.
                    var count by remember { mutableStateOf(0) }
                    Box(Modifier.fillMaxSize().clickable { count++; taps = count }
                        .dropTarget("word", onEnter = { entered++ }) { dropped = it.payload as String })
                }
            }
        }
    }
}

/** Docking and drag and drop, in Compose. */
class DockingTest {
    @Test
    fun panelsDockAndADragLandsOnItsTarget(): Unit = headlessApp().use { app ->
        val scene = app.openOffscreen("Docking", Docking(), OffscreenSettings(width = 240, height = 160))
        val input = scene.input
        fun at(x: Float, y: Float) { input.feedMousePosition(Vec2(x, y)); app.frames(1) }
        fun button(down: Boolean) { input.feedMouseButton(MouseButton.e1, down); app.frames(1) }
        app.frames(3)

        // The source in the middle, the target down the right (53 wide, from 149), its button in the stripe at the
        // edge; each under a title bar of 28. The red box is in the source; the target is its whole panel.
        at(20f, 50f); button(true); at(50f, 60f); at(175f, 100f)
        assertEquals(1, scene.entered, "over the target")
        button(false); app.frames(1)
        assertEquals("hello", scene.dropped)
        assertEquals(true, scene.accepted)

        at(175f, 100f); button(true); button(false)
        assertEquals(1, scene.taps)

        // The target's button, dragged into the left margin of the space: down the left side, its state with it.
        assertFalse(scene.layout.isFloating("dst"))
        val before = scene.layout.save()
        at(221f, 19f); button(true); at(100f, 40f); at(10f, 80f); at(10f, 80f); button(false); app.frames(2)
        assertNotEquals(before, scene.layout.save(), "the layout changed")
        assertTrue(scene.changes > 0)
        at(60f, 100f); button(true); button(false)
        assertEquals(2, scene.taps, "the same panel, counting on")

        val other = DockLayout()
        assertTrue(other.load(scene.layout.save()))
        assertEquals(scene.layout.save(), other.save())
        assertFalse(other.load("nonsense"))
        scene.layout.popOut("dst")   // no display here: it floats inside the space
        app.frames(2)
        assertTrue(scene.layout.isFloating("dst"))
    }
}
