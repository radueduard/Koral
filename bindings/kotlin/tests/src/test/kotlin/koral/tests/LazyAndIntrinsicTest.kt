package koral.tests

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import koral.Buffer
import koral.CommandBuffer
import koral.OffscreenSettings
import koral.Scene
import koral.Vec2
import koral.compose.Arrangement
import koral.compose.Box
import koral.compose.Color
import koral.compose.Column
import koral.compose.GridCells
import koral.compose.IntrinsicSize
import koral.compose.LazyColumn
import koral.compose.LazyListState
import koral.compose.LazyRow
import koral.compose.LazyVerticalGrid
import koral.compose.Modifier
import koral.compose.PaddingValues
import koral.compose.Row
import koral.compose.SubcomposeLayout
import koral.compose.background
import koral.compose.dp
import koral.compose.fillMaxHeight
import koral.compose.fillMaxWidth
import koral.compose.height
import koral.compose.setContent
import koral.compose.size
import koral.compose.width
import kotlinx.coroutines.runBlocking
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

private const val SIDE = 240

class Lists : Scene() {
    val readback: Buffer = readback(SIDE)
    val down = LazyListState()
    val across = LazyListState()
    val composed = HashSet<Int>()
    var wide by mutableStateOf(false)

    override fun initialize() {
        graph.add(Clear(0f, 0f, 0f))
        setContent {
            Column {
                // 0 to 80: a list down, each item as tall as it is — the even ones ten and red, the odd ones thirty and
                // green — four apart, six before the first.
                LazyColumn(Modifier.size(100.dp, 80.dp), down, contentPadding = PaddingValues(0.dp, 6.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    items(100_000) { i ->
                        composed += i
                        Box(Modifier.fillMaxWidth().height(if (i % 2 == 0) 10.dp else 30.dp).background(if (i % 2 == 0) Color.Red else Color.Green))
                    }
                }
                // 80 to 100: one across, of which only what is in view is composed.
                LazyRow(Modifier.size(100.dp, 20.dp), across) {
                    items(100_000) { i -> Box(Modifier.size(if (i % 2 == 0) 10.dp else 30.dp, 20.dp).background(if (i % 2 == 0) Color.Red else Color.Blue)) }
                }
                // 100 to 130: a row as tall as its tallest, the line beside it filling that.
                Row(Modifier.height(IntrinsicSize.Min)) {
                    Box(Modifier.size(20.dp, 30.dp).background(Color.Red))
                    Box(Modifier.width(10.dp).fillMaxHeight().background(Color.Green))
                }
                // 130 to 150: a column as wide as its widest, the other filling that.
                Column(Modifier.width(IntrinsicSize.Max)) {
                    Box(Modifier.size(50.dp, 10.dp).background(Color.Red))
                    Box(Modifier.fillMaxWidth().height(10.dp).background(Color.Blue))
                }
                // 150 to 170: what is under the first part is as wide as the first part came out.
                SubcomposeLayout(Modifier.size(100.dp, 20.dp)) { tight ->
                    val constraints = tight.copy(minWidth = 0, minHeight = 0)
                    val first = subcompose("first") { Box(Modifier.size(if (wide) 60.dp else 30.dp, 10.dp).background(Color.Red)) }.map { it.measure(constraints) }
                    val width = first.maxOfOrNull { it.width } ?: 0
                    val second = subcompose("second") { Box(Modifier.size(width.dp, 10.dp).background(Color.Green)) }.map { it.measure(constraints) }
                    layout(100, 20) { first.forEach { it.place(0, 0) }; second.forEach { it.place(0, 10) } }
                }
                // 170 on: a grid of as many columns as fit at forty each in a hundred: two.
                LazyVerticalGrid(GridCells.Adaptive(40.dp), Modifier.size(100.dp, 40.dp)) {
                    items(6) { i -> Box(Modifier.fillMaxWidth().height(20.dp).background(listOf(Color.Red, Color.Green, Color.Blue)[i % 3])) }
                }
            }
        }
        graph.add(ReadScreen(readback))
    }
}

/** Lazy lists whose items are as long as they like, down and across, with a state; intrinsic sizes; a layout that composes. */
class LazyAndIntrinsicTest {
    @Test
    fun listsOfItemsOfTheirOwnLengthAndSizesOfTheirOwn(): Unit = headlessApp().use { app ->
        val scene = app.openOffscreen("Lists", Lists(), OffscreenSettings(width = SIDE, height = SIDE))
        val input = scene.input
        fun frame(): ByteArray { CommandBuffer.singleTimeCommand { }.waitBlocking(); return scene.readback.read() }
        fun at(x: Int, y: Int): List<Int> { val b = frame(); return (0 until 3).map { b[(y * SIDE + x) * 4 + it].toInt() and 0xff } }
        val red = listOf(255, 0, 0); val green = listOf(0, 255, 0); val blue = listOf(0, 0, 255); val black = listOf(0, 0, 0)

        input.feedMousePosition(Vec2(230f, 230f)); app.frames(5)
        assertEquals(black, at(10, 3), "the padding before the first")
        assertEquals(red, at(10, 10), "item 0: ten tall, from six")
        assertEquals(black, at(10, 18), "four between")
        assertEquals(green, at(10, 30), "item 1: thirty tall, from twenty")
        assertEquals(red, at(10, 58), "item 2, from fifty-four")
        assertTrue(scene.composed.size < 200, "only what is near the view: ${scene.composed.size}")
        assertEquals(0, scene.down.firstVisibleItemIndex)

        input.feedMousePosition(Vec2(10f, 10f)); app.frames(1)
        input.feedScroll(Vec2(0f, -1f)); app.frames(3)       // 48: past item 0, and 28 into item 1, which starts at 20
        assertEquals(1, scene.down.firstVisibleItemIndex, "the state says where it is")
        assertEquals(28, scene.down.firstVisibleItemScrollOffset)

        runBlocking { scene.down.scrollToItem(5001) }
        app.frames(5)
        assertEquals(5001, scene.down.firstVisibleItemIndex, "and sends it elsewhere")
        assertEquals(green, at(10, 5), "an odd item first in view")
        assertTrue(scene.composed.max() < 5200 && 5001 in scene.composed, "composing only what is there")

        assertEquals(red, at(5, 90), "a row: item 0, ten wide")
        assertEquals(blue, at(25, 90), "item 1, thirty wide")
        assertEquals(red, at(45, 90))
        runBlocking { scene.across.scrollToItem(1) }
        app.frames(4)
        assertEquals(blue, at(5, 90), "the row, sent on")

        assertEquals(green, at(25, 125), "IntrinsicSize.Min: the line fills the height of the tallest")
        assertEquals(black, at(35, 125))
        assertEquals(blue, at(45, 145), "IntrinsicSize.Max: as wide as the widest")
        assertEquals(black, at(55, 145))

        assertEquals(green, at(25, 165), "SubcomposeLayout: the second as wide as the first came out")
        assertEquals(black, at(35, 165))
        scene.wide = true
        app.frames(6)
        assertEquals(green, at(55, 165), "and again when that changes")

        assertEquals(red, at(25, 180), "an adaptive grid: two across in a hundred")
        assertEquals(green, at(75, 180))
        assertEquals(blue, at(25, 200))
    }
}
