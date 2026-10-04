package koral.tests

import koral.Buffer
import koral.Image
import koral.ImageFormat
import koral.ImageUsage
import koral.OffscreenSettings
import koral.Scene
import koral.UVec2
import koral.Vec2
import koral.compose.ContentScale
import koral.compose.Modifier
import koral.compose.Picture
import koral.compose.fillMaxSize
import koral.compose.setContent
import kotlin.test.Test
import kotlin.test.assertEquals

class Showing : Scene() {
    val readback: Buffer = readback(SIZE)
    override fun initialize() {
        graph.add(Clear(0f, 0f, 0f))
        // A 2x2 texture: red, green / blue, white.
        val texture = Image.Builder().setFormat(ImageFormat.eRGBA8_UNORM).setExtent(UVec2(2, 2))
            .setData(byteArrayOf(-1, 0, 0, -1, 0, -1, 0, -1, 0, 0, -1, -1, -1, -1, -1, -1))
            .setUsage(ImageUsage.eSampled, ImageUsage.eTransferDst).build()
        setContent { Picture(texture, "test pattern", Modifier.fillMaxSize(), ContentScale.FillBounds) }
        graph.add(ReadScreen(readback))
    }
}

/** A Koral image in a Compose interface. */
class ImageTest {
    @Test
    fun anImageShows(): Unit = headlessApp().use { app ->
        val scene = app.openOffscreen("Showing", Showing(), OffscreenSettings(width = SIZE, height = SIZE))
        scene.input.feedMousePosition(Vec2(-10f, -10f))
        app.frames(3)
        fun at(x: Int, y: Int) = scene.readback.pixel(SIZE, x, y)
        assertEquals(listOf(255, 0, 0, 255), at(8, 8), "its top-left texel, stretched over a quarter")
        assertEquals(listOf(0, 255, 0, 255), at(56, 8))
        assertEquals(listOf(0, 0, 255, 255), at(8, 56))
        assertEquals(listOf(255, 255, 255, 255), at(56, 56))
    }
}
