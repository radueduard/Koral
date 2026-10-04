package koral.tests

import koral.App
import koral.AppSettings
import koral.Buffer
import koral.BufferType
import koral.BufferUsage
import koral.CommandBuffer
import koral.FrameGraph
import koral.Image
import koral.ImageUsage
import koral.KoralException
import koral.PassBuilder
import koral.PassResources
import koral.RenderPass
import koral.WindowPlatform
import org.junit.jupiter.api.Assumptions.assumeTrue

/** An application with no windowing system — or the test skipped, on a machine with no Vulkan device. */
fun headlessApp(): App = try {
    App(AppSettings(platform = WindowPlatform.eNone))
} catch (e: KoralException) {
    assumeTrue(false, "no Vulkan device: ${e.message}")
    throw e
}

fun App.frames(count: Int) = repeat(count) { frame() }

/** Clears the screen, so what follows draws over a known colour. */
class Clear(private val r: Float, private val g: Float, private val b: Float) : RenderPass("Clear") {
    private var screen: Image? = null
    override fun setup(builder: PassBuilder) { builder.write(FrameGraph.Screen, ImageUsage.eTransferDst) }
    override fun initialize(resources: PassResources) { screen = resources.imageNamed(FrameGraph.Screen) }
    override fun record(commands: CommandBuffer) { commands.clearColorImage(screen!!, r, g, b) }
}

/** Copies the screen where the test can read it. */
class ReadScreen(val readback: Buffer) : RenderPass("Read") {
    private var screen: Image? = null
    override fun setup(builder: PassBuilder) { builder.read(FrameGraph.Screen, ImageUsage.eTransferSrc).sideEffect() }
    override fun initialize(resources: PassResources) { screen = resources.imageNamed(FrameGraph.Screen) }
    override fun record(commands: CommandBuffer) { commands.copyImageToBuffer(screen!!, readback) }
}

fun readback(size: Int): Buffer = Buffer.Builder().setSize(size.toLong() * size * 4).setUsage(BufferUsage.eTransferDst)
    .setType(BufferType.eReadback).build()

/**
 * The RGBA bytes of pixel (x, y) of a [size]-wide screen, as the last frame drew it.
 *
 * A frame is submitted, not waited for: with two in flight, the copy into this buffer may not have
 * happened yet when frame() returns, and a read would see the frame before — on some GPUs, some of the
 * time. Work submitted after it and waited for is done only once it is, the queue running in order.
 */
fun Buffer.pixel(size: Int, x: Int, y: Int): List<Int> {
    CommandBuffer.singleTimeCommand { }.waitBlocking()
    val bytes = read()
    val i = (y * size + x) * 4
    return (0 until 4).map { bytes[i + it].toInt() and 0xff }
}
