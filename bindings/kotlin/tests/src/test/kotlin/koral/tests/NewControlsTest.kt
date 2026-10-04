package koral.tests

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import koral.MouseButton
import koral.OffscreenSettings
import koral.Scene
import koral.Vec2
import koral.compose.Box
import koral.compose.Color
import koral.compose.Column
import koral.compose.ContextMenuArea
import koral.compose.DragValue
import koral.compose.Dropdown
import koral.compose.MenuItem
import koral.compose.Modifier
import koral.compose.background
import koral.compose.dp
import koral.compose.setContent
import koral.compose.size
import kotlin.test.Test
import kotlin.test.assertEquals

class NewControls : Scene() {
    var amount by mutableStateOf(0f)
    var quality by mutableStateOf(0)
    var picked = ""

    override fun initialize() {
        graph.add(Clear(0f, 0f, 0f))
        setContent {
            // Each a control's height (36), one under the other: the drag value, the dropdown, then a box with a menu.
            Column {
                DragValue(amount, { amount = it }, speed = 0.1f, valueRange = 0f..8f, width = 120.dp)
                Dropdown(listOf("Low", "Medium", "High"), quality, { quality = it }, width = 200.dp)
                ContextMenuArea(listOf(MenuItem("Rename") { picked = "rename" }, MenuItem.Divider,
                                       MenuItem("Locked", enabled = false) { picked = "locked" }, MenuItem("Delete") { picked = "delete" })) {
                    Box(Modifier.size(60.dp, 40.dp).background(Color.Red))
                }
            }
        }
    }
}

/** DragValue, Dropdown and ContextMenuArea: what each does with the pointer. */
class NewControlsTest {
    @Test
    fun dragDropdownAndContextMenu(): Unit = headlessApp().use { app ->
        val scene = app.openOffscreen("NewControls", NewControls(), OffscreenSettings(width = 260, height = 260))
        val input = scene.input
        fun at(x: Float, y: Float) { input.feedMousePosition(Vec2(x, y)); app.frames(1) }
        fun button(down: Boolean, which: MouseButton = MouseButton.e1) { input.feedMouseButton(which, down); app.frames(1) }
        fun click(x: Float, y: Float, which: MouseButton = MouseButton.e1) { at(x, y); button(true, which); button(false, which); app.frames(2) }

        at(250f, 250f); app.frames(3)

        // Dragged 50 to the right at a tenth a unit: five. Dragged far further: its range's end.
        at(40f, 18f); button(true); at(60f, 18f); at(90f, 18f); button(false); app.frames(2)
        assertEquals(5f, scene.amount, 0.01f, "fifty units at 0.1 each")
        at(40f, 18f); button(true); at(140f, 18f); at(240f, 18f); button(false); app.frames(2)
        assertEquals(8f, scene.amount, 0.01f, "stopped at the end of its range")

        // The dropdown opens its list under itself (six of padding, then lines of 36), and takes what is picked.
        click(100f, 54f)
        click(60f, 76f + 6f + 36f + 18f)
        assertEquals(1, scene.quality, "the second line: Medium")

        // Opened and let go of by pressing elsewhere: nothing picked.
        click(100f, 54f)
        click(240f, 240f)
        click(60f, 76f + 6f + 18f)
        assertEquals(1, scene.quality, "the list had closed: the press where its first line was picked nothing")

        // The right button on the box opens its menu there: Rename, a line, Locked (which does nothing), Delete.
        click(20f, 80f, MouseButton.e2)
        click(40f, 80f + 6f + 36f + 9f + 18f)
        assertEquals("", scene.picked, "a line that is not enabled runs nothing")
        click(40f, 80f + 6f + 36f + 9f + 36f + 18f)
        assertEquals("delete", scene.picked, "the last line")
    }
}
