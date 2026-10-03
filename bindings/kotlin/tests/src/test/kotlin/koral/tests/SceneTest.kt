package koral.tests

import koral.Buffer
import koral.Key
import koral.OffscreenSettings
import koral.Scene
import koral.SceneArgs
import koral.Vec2
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class Painter(args: SceneArgs) : Scene() {
    val green = args.flag("green")
    val readback: Buffer = readback(16)   // made while the scene is: the scene's, closed with it
    var updates = 0
    var spacePressedAt = -1
    var typed = ""

    override fun initialize() {
        graph.add(Clear(0f, if (green) 1f else 0f, if (green) 0f else 1f))
        graph.add(ReadScreen(readback))
        window.title = "painting"
    }

    override fun update() {
        ++updates
        if (input.isKeyPressed(Key.eSpace)) spacePressedAt = updates
        typed += input.typedText
    }
}

/** A scene in Kotlin: its hooks, its window and input, passes written in Kotlin, and what it owns. */
class SceneTest {
    @Test
    fun aSceneRunsPaintsAndGoesWithWhatItMade() = headlessApp().use { app ->
        app.register("Painter") { Painter(it) }
        val scene = app.openOffscreen("Painter", OffscreenSettings(width = 16, height = 16), SceneArgs().with("green", true)) as Painter
        assertTrue(scene.green, "the arguments reached the constructor")
        assertEquals("Painter", scene.name)
        assertEquals("painting", scene.window.title, "its window, set from inside")

        app.frames(3)
        scene.input.feedKey(Key.eSpace, true)
        scene.input.feedText("hé")
        app.frames(3)

        assertEquals(6, scene.updates, "updated every frame")
        assertEquals(4, scene.spacePressedAt, "fed input arrives the next frame")
        assertEquals("hé", scene.typed, "typed text, through the scene's input")
        assertEquals(listOf(0, 255, 0, 255), scene.readback.pixel(16, 8, 8), "the Kotlin pass painted the screen green")
        assertEquals(Vec2(0f, 0f), scene.input.mouseScrollDelta)

        val readback = scene.readback
        app.close(scene)
        assertFalse(app.frame(), "no scene left")
        assertFalse(scene.isOpen)
        assertFalse(readback.isValid, "what the scene made went with it")
    }
}
