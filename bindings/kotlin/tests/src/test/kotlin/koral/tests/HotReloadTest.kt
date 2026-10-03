package koral.tests

import java.nio.file.Files
import java.nio.file.Path
import koral.Buffer
import koral.HotReload
import koral.KoralException
import koral.MouseButton
import koral.OffscreenSettings
import koral.Scene
import koral.SceneArgs
import koral.Vec2
import koral.compose.setContent
import koral.tests.reload.Reloaded
import kotlin.io.path.extension
import kotlin.io.path.readBytes
import kotlin.io.path.relativeTo
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue
import kotlin.test.fail

class Reloading(@Suppress("UNUSED_PARAMETER") args: SceneArgs) : Scene() {
    val readback: Buffer = readback(SIZE)
    override fun initialize() {
        graph.add(Clear(0f, 0f, 0f))
        setContent { Reloaded() }
        graph.add(ReadScreen(readback))
    }
}

/** The class files a source set compiled: binary name to bytes. */
private fun compiled(property: String): Map<String, ByteArray> = buildMap {
    for (dir in System.getProperty(property).split(java.io.File.pathSeparator).map { Path.of(it) }.filter { Files.isDirectory(it) }) {
        Files.walk(dir).use { files ->
            files.filter { it.extension == "class" }.forEach {
                put(it.relativeTo(dir).toString().removeSuffix(".class").replace(java.io.File.separatorChar, '.'), it.readBytes())
            }
        }
    }
}

/** Edits applied while it runs: the new code draws, and what was remembered is still there. */
class HotReloadTest {
    @Test
    fun editsApplyInPlaceKeepingState() = headlessApp().use { app ->
        assertTrue(HotReload.isAvailable, "the tests run with Koral's jar as their agent")
        if (!System.getenv("KORAL_JBR").isNullOrEmpty()) assertTrue(HotReload.isStructural, "on the JetBrains Runtime, any edit")
        app.register("Reloading") { Reloading(it) }
        val scene = app.openOffscreen("Reloading", OffscreenSettings(width = SIZE, height = SIZE), SceneArgs()) as Reloading
        fun at(x: Int, y: Int) = scene.readback.pixel(SIZE, x, y)
        val input = scene.input
        input.feedMousePosition(Vec2(60f, 60f))
        app.frames(3)
        assertEquals(listOf(255, 0, 0, 255), at(8, 30), "version 1, not tapped")

        input.feedMousePosition(Vec2(8f, 30f)); app.frames(1)
        input.feedMouseButton(MouseButton.e1, true); app.frames(1)
        input.feedMouseButton(MouseButton.e1, false); app.frames(1)
        input.feedMousePosition(Vec2(60f, 60f)); app.frames(2)
        assertEquals(listOf(0, 0, 255, 255), at(8, 30), "version 1, tapped")

        // An edit to what a method does: any JVM.
        val changed = HotReload.apply(compiled("koral.test.reloadBody"))
        assertTrue(changed.any { it.name == "koral.tests.reload.ReloadedKt" }, "the composable's class was redefined: $changed")
        app.frames(2)
        assertEquals(listOf(0, 255, 0, 255), at(8, 30), "the new code draws, and the tap is remembered")

        // An edit that adds a lambda: the JetBrains Runtime only.
        val shape = compiled("koral.test.reloadShape")
        if (HotReload.isStructural) {
            HotReload.apply(shape)
            app.frames(2)
            assertEquals(listOf(255, 255, 0, 255), at(14, 2), "the box added at the top")
            assertEquals(listOf(255, 0, 255, 255), at(8, 20), "the box that was first, moved down")
            assertEquals(listOf(0, 255, 0, 255), at(8, 50), "the tapped box, moved down and still tapped")
        } else {
            try {
                HotReload.apply(shape)
                fail("a structural edit on a standard JVM should be refused")
            } catch (e: KoralException) {
                assertTrue("JetBrains Runtime" in e.message!!, e.message)
            }
            app.frames(1)
            assertEquals(listOf(0, 255, 0, 255), at(8, 30), "a refused edit changes nothing")
        }
    }
}
