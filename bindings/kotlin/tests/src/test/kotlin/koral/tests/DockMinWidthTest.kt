package koral.tests

import koral.OffscreenSettings
import koral.Scene
import koral.Vec3
import koral.compose.Arrangement
import koral.compose.Box
import koral.compose.Column
import koral.compose.DockArea
import koral.compose.DockLayout
import koral.compose.DockSpace
import koral.compose.DragValue
import koral.compose.Icons
import koral.compose.Modifier
import koral.compose.dp
import koral.compose.fillMaxSize
import koral.compose.fillMaxWidth
import koral.compose.onSizeChanged
import koral.compose.padding
import koral.compose.setContent
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

private const val W = 800
private const val H = 300

/** The Engine's transform panel: a Vec3 to drag, as wide as the panel, down the right of the space. */
class TransformDock : Scene() {
    val layout = DockLayout().dock("viewport", DockArea.Center).dock("transform", DockArea.Right)
    var row = 0
    var view = 0

    override fun initialize() {
        graph.add(Clear(0f, 0f, 0f))
        setContent {
            DockSpace(layout, Modifier.fillMaxSize(), multiViewport = false, gap = 4.dp, stripeGap = 0.dp) {
                panel("viewport", "Viewport") { Box(Modifier.fillMaxSize().onSizeChanged { view = it.width }) }
                panel("transform", "Transform", icon = Icons.Filled.Tune) {
                    Column(Modifier.fillMaxSize().padding(12.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                        DragValue(Vec3(0f, 0f, 0f), {}, Modifier.fillMaxWidth().onSizeChanged { row = it.width })
                    }
                }
            }
        }
    }
}

/** A docked panel is no narrower than what it shows needs; a vector's drags share the panel's width. */
class DockMinWidthTest {
    @Test
    fun aTransformPanelIsAsWideAsItsDragsNeedAndTheyFillIt(): Unit = headlessApp().use { app ->
        val scene = app.openOffscreen("TransformDock", TransformDock(), OffscreenSettings(width = W, height = H))
        app.frames(3)
        // The space: a stripe of 38 for the transform's button, half the gap at the left, the gap between the two.
        val panel = W - 38 - 2 - 4 - scene.view
        assertEquals(panel - 24, scene.row, "the drags fill the panel, inside its padding")
        // Three drags, each its label and a number of a sign, four digits and two decimals, side by side: far more
        // than the quarter of the space the right side would take of itself.
        assertTrue(scene.row > 3 * 90, "wide enough for three drags: ${scene.row}")
        assertTrue(panel > W / 4, "wider than the side's own share: $panel")
    }
}
