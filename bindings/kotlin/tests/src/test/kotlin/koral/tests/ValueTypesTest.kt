package koral.tests

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import koral.MouseButton
import koral.OffscreenSettings
import koral.Scene
import koral.Vec2
import koral.Vec3
import koral.compose.Column
import koral.compose.Dp
import koral.compose.DragValue
import koral.compose.Slider
import koral.compose.TextUnit
import koral.compose.dp
import koral.compose.setContent
import koral.compose.sp
import kotlin.test.Test
import kotlin.test.assertEquals

class ValueTypes : Scene() {
    var radius by mutableStateOf(0.dp)
    var count by mutableStateOf(0)
    var text by mutableStateOf(8.sp)
    var gap by mutableStateOf(0.dp)
    var position by mutableStateOf(Vec3(0f, 0f, 0f))

    override fun initialize() {
        graph.add(Clear(0f, 0f, 0f))
        setContent {
            // A slider is 200 by 28, its ends 8 in; a drag value is a control's height (36).
            Column {
                Slider(radius, { radius = it }, valueRange = 0.dp..24.dp)
                Slider(count, { count = it }, valueRange = 0..10)
                Slider(text, { text = it }, valueRange = 8.sp..32.sp)
                DragValue(gap, { gap = it }, speed = 0.1f, valueRange = 0.dp..8.dp, width = 120.dp)
                // A vector: a drag value a component, 80 wide and 6 apart.
                DragValue(position, { position = it }, speed = 0.1f, width = 80.dp)
            }
        }
    }
}

/** A Slider and a DragValue over the kinds of number an interface is written in: each handed back as what it was given. */
class ValueTypesTest {
    @Test
    fun slidersAndDragValuesTakeLengthsCountsAndTextSizes(): Unit = headlessApp().use { app ->
        val scene = app.openOffscreen("ValueTypes", ValueTypes(), OffscreenSettings(width = 260, height = 260))
        val input = scene.input
        fun at(x: Float, y: Float) { input.feedMousePosition(Vec2(x, y)); app.frames(1) }
        fun button(down: Boolean) { input.feedMouseButton(MouseButton.e1, down); app.frames(1) }
        fun click(x: Float, y: Float) { at(x, y); button(true); button(false); app.frames(2) }

        at(250f, 250f); app.frames(3)

        // Halfway along its track (8 to 192): half its range.
        click(100f, 14f)
        assertEquals(Dp(12f), scene.radius, "a length, from a slider over lengths")

        // A little past three tenths of the way: the whole number nearest.
        click(8f + 184f * 0.32f, 42f)
        assertEquals(3, scene.count, "a count: whole numbers only")

        click(192f, 70f)
        assertEquals(TextUnit(32f), scene.text, "a text size, at the end of its range")

        // Dragged 50 to the right at a tenth a unit: five.
        at(40f, 102f); button(true); at(60f, 102f); at(90f, 102f); button(false); app.frames(2)
        assertEquals(5f, scene.gap.value, 0.01f, "a length, from a drag value over lengths")

        // The second of the vector's three, dragged 30 to the right: its Y, and only its Y.
        at(110f, 138f); button(true); at(125f, 138f); at(140f, 138f); button(false); app.frames(2)
        assertEquals(3f, scene.position.y, 0.01f, "the component under the pointer")
        assertEquals(0f, scene.position.x, 0.01f)
        assertEquals(0f, scene.position.z, 0.01f)
    }
}
