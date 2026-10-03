package koral.tests

import java.nio.file.Files
import kotlin.io.path.writeText
import kotlinx.coroutines.delay
import koral.App
import koral.Buffer
import koral.BufferType
import koral.BufferUsage
import koral.ChannelType
import koral.CommandBuffer
import koral.CommandBufferUsage
import koral.ComputePipeline
import koral.Debug
import koral.DebugDrawPass
import koral.DebugStyle
import koral.DescriptorSet
import koral.ErrorCode
import koral.Filter
import koral.FrameGraph
import koral.GpuLayout
import koral.GraphicsPipeline
import koral.Image
import koral.ImageFormat
import koral.ImageUsage
import koral.Input
import koral.InputSource
import koral.Key
import koral.KoralException
import koral.Mat4
import koral.Mesh
import koral.OffscreenSettings
import koral.PassBuilder
import koral.RenderPass
import koral.Sampler
import koral.SamplerAddressMode
import koral.Scene
import koral.SceneArgs
import koral.Shader
import koral.ShaderStage
import koral.Token
import koral.UVec2
import koral.UVec3
import koral.Vec2
import koral.Vec3
import koral.Vec4
import koral.VertexInputBindingDescription
import koral.VertexLayout
import koral.launch
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertSame
import kotlin.test.assertTrue

private fun shaderFile(name: String, source: String): String {
    val dir = Files.createTempDirectory("koral-kotlin-")
    return dir.resolve(name).also { it.writeText(source.trimIndent()) }.toString()
}

/** The resource API: builders chained and poisoning, mappings, a compute dispatch by names, failures. */
class ResourcesTest {
    @Test
    fun buildersChainAndPoison(): Unit = headlessApp().use {
        val image = Image.Builder()
            .setFormat(ImageFormat.eRGBA16_SFLOAT)
            .setExtent(UVec2(32, 8))
            .setMipLevels(2)
            .setUsage(ImageUsage.eSampled, ImageUsage.eTransferDst)
            .build()
        assertTrue(image.isValid && image.isOwned, "an image, from a chain")
        assertEquals(UVec3(32, 8, 1), image.extent)
        assertEquals(ImageFormat.eRGBA16_SFLOAT, image.format)
        assertEquals(2, image.mipLevels)
        assertEquals(setOf(ImageUsage.eSampled, ImageUsage.eTransferDst), image.usage intersect setOf(ImageUsage.eSampled, ImageUsage.eTransferDst))
        val view = image.view()
        assertTrue(!view.isOwned && view.image == image, "its own view, borrowed, of it")

        val bad = Buffer.Builder().setSize(-1)
        assertTrue(bad.hasErrors, "a setter's problem is known before the build")
        val poisoned = bad.build()
        assertTrue(poisoned.isPoisoned && !poisoned.isValid, "and the build is poisoned, not thrown")
        assertEquals(ErrorCode.eBufferSizeInvalid, poisoned.failure?.code)
        assertFailsWith<KoralException>("a poisoned resource's own members throw") { poisoned.size }

        val shader = Shader.Builder().setPath("no/such/shader.comp").setStage(ShaderStage.eCompute).build()
        assertTrue(shader.isPoisoned, "a shader that is not there")
        val pipeline = ComputePipeline.Builder().setComputeShader(shader).build()
        assertTrue(pipeline.isPoisoned, "what is built from it is poisoned too")
        assertTrue("no/such/shader.comp" in pipeline.failure!!.history, "and names the cause: ${pipeline.failure}")

        val sampler = Sampler.Builder().setMinFilter(Filter.eNearest).setAddressModeU(SamplerAddressMode.eClampToEdge).build()
        assertTrue(sampler.isValid)
        sampler.close()
        assertFailsWith<IllegalStateException>("a closed resource refuses to be used") { sampler.name }
        Unit
    }

    @Test
    fun mappingsAndTypedData() = headlessApp().use {
        val buffer = Buffer.Builder().setInstanceCount(8, GpuLayout.Int).setType(BufferType.eDynamic).build()
        buffer.map { it.write(IntArray(8) { i -> i * i }) }
        assertEquals((0 until 8).map { it * it }, buffer.readInts().toList(), "what the mapping wrote")
        buffer.mapConst(8, 8) { m -> assertEquals(listOf(4, 9), List(2) { m.segment.getAtIndex(java.lang.foreign.ValueLayout.JAVA_INT, it.toLong()) }) }
        buffer.write(intArrayOf(100), offset = 4)
        assertEquals(100, buffer.readInts(1, offset = 1)[0])
        assertEquals(32, buffer.size)

        val points = Buffer.Builder().setData(listOf(Vec2(1f, 2f), Vec2(3f, 4f)), GpuLayout.Vec2).setType(BufferType.eDynamic).build()
        assertEquals(listOf(Vec2(1f, 2f), Vec2(3f, 4f)), points.read(GpuLayout.Vec2), "values in and out by their layout")
    }

    @Test
    fun computeDispatchesByName() = headlessApp().use {
        val path = shaderFile("fill.comp", """
            #version 450
            layout(local_size_x = 4) in;
            layout(std430, set = 0, binding = 0) buffer Data { uint values[]; } data;
            layout(push_constant) uniform Push { uint add; } push;
            void main() { data.values[gl_GlobalInvocationID.x] = gl_GlobalInvocationID.x * 10u + push.add; }
        """)
        val pipeline = ComputePipeline.Builder().setComputeShader(Shader.Builder().setPath(path).setStage(ShaderStage.eCompute).build()).build()
        assertTrue(pipeline.isValid, "a compute pipeline: ${pipeline.failure}")
        assertTrue(pipeline.hasPushConstant("add"))
        assertEquals(0, pipeline.setLayout(0).findBinding("data"))

        val values = Buffer.Builder().setInstanceCount(4, GpuLayout.Int)
            .setUsage(BufferUsage.eStorage, BufferUsage.eTransferSrc, BufferUsage.eTransferDst).setType(BufferType.eDynamic).build()
        val set = DescriptorSet.Builder(pipeline, 0).write("data", values).build()
        assertTrue(set.isValid, "a descriptor set: ${set.failure}")

        CommandBuffer.singleTimeCommand(CommandBufferUsage.eCompute) {
            it.bindComputePipeline(pipeline).bindDescriptorSet(0, set).pushConstant("add", 7u).dispatch()
        }.waitBlocking()
        assertEquals(listOf(7, 17, 27, 37), values.readInts().toList(), "the shader wrote them")

        val errors = mutableListOf<String>()
        CommandBuffer.singleTimeCommand(CommandBufferUsage.eCompute) {
            it.bindComputePipeline(pipeline).pushConstant("add", 1f).dispatch()
            errors += it.errors
        }.waitBlocking()
        assertTrue(errors.size == 1 && "add" in errors[0], "a push constant of the wrong type is kept as an error: $errors")
    }

    @Test
    fun failuresAreExceptions() = headlessApp().use { app ->
        val unknown = assertFailsWith<KoralException> { app.openOffscreen("Nope", OffscreenSettings(width = 8, height = 8)) }
        assertTrue("Nope" in unknown.message!!, "says which: ${unknown.message}")
        assertFailsWith<IllegalStateException>("a second application") { App() }
        assertFailsWith<KoralException>("the current scene's input, with none current") { Input.current }
        assertNull(InputSource.parse("Key.Nothing"))
        assertEquals("-GamepadAxis.LeftY", InputSource.parse("-GamepadAxis.LeftY")?.name)
        assertEquals("Key.Space", InputSource(Key.eSpace).name)
    }

    @Test
    fun sceneArgsRoundTrip() {
        val args = SceneArgs().with("map", "caves").with("level", 3).with("hard", true).with("scale", 1.5)
        val back = SceneArgs.fromJson(args.toJson())
        assertEquals("caves", back.string("map"))
        assertEquals(3L, back.integer("level"))
        assertTrue(back.flag("hard"))
        assertEquals(1.5, back.number("scale"))
        assertEquals(7L, back.integer("missing", 7))
        assertEquals(2L, SceneArgs.fromJson("""{"level": 2, "name": "x"}""").integer("level"), "a number as JSON writes it")
    }
}

/** A coroutine of a scene: awaiting GPU work, a delay and a readback, each resuming with its scene current. */
class Waiter : Scene() {
    var resumedIn: Scene? = null
    var delayedIn: Scene? = null
    var readIn: Scene? = null
    var filled: List<Int> = emptyList()
    var readLater: List<Int> = emptyList()

    override fun initialize() {
        launch {
            val buffer = Buffer.Builder().setInstanceCount(4, GpuLayout.Int)
                .setUsage(BufferUsage.eTransferDst, BufferUsage.eTransferSrc, BufferUsage.eStorage).setType(BufferType.eDynamic).build()
            CommandBuffer.singleTimeCommand { it.fillBuffer(buffer, intArrayOf(7, 7, 7, 7)) }.await()
            resumedIn = Scene.current
            filled = buffer.readInts().toList()
            delay(10)
            delayedIn = Scene.current
            val numbers = Buffer.Builder().setData(intArrayOf(3, 1, 4, 1, 5)).setType(BufferType.eDeviceLocal).build()
            readLater = numbers.readAsync(GpuLayout.Int)
            readIn = Scene.current
        }
    }
}

class AwaitTest {
    @Test
    fun awaitResumesInItsScene() = headlessApp().use { app ->
        val waiter = app.openOffscreen("Waiter", Waiter(), OffscreenSettings(width = 8, height = 8))
        val deadline = System.nanoTime() + 10_000_000_000L
        while (waiter.readIn == null && System.nanoTime() < deadline) app.frame()
        assertSame(waiter, waiter.resumedIn, "after GPU work, in its scene")
        assertEquals(listOf(7, 7, 7, 7), waiter.filled)
        assertSame(waiter, waiter.delayedIn, "after an ordinary delay, too")
        assertSame(waiter, waiter.readIn, "and after readAsync")
        assertEquals(listOf(3, 1, 4, 1, 5), waiter.readLater)
        assertTrue(Token.create().let { t -> t.signal(); t.isReady }, "a token of one's own")
    }
}

class Lines : Scene() {
    var cameraCalls = 0
    override fun initialize() {
        graph.add(Clear(0f, 0f, 0f))
        graph.add(DebugDrawPass(debug, { cameraCalls++; Mat4.Identity }))
    }
    override fun update() {
        Debug.current.line(Vec3(-0.5f, 0f, 0.5f), Vec3(0.5f, 0f, 0.5f), DebugStyle(Vec4(0f, 1f, 0f, 1f)))
        debug.box(Vec3(-0.5f, -0.5f, -0.5f), Vec3(0.5f, 0.5f, 0.5f), DebugStyle(onTop = true))
        debug.sphere(Vec3.Zero, 1f, DebugStyle(duration = 1f))   // still there after the frame that drew it
    }
}

class DebugLinesTest {
    @Test
    fun debugLinesDraw() = headlessApp().use { app ->
        app.register<Lines>()
        val scene = app.openOffscreen("Lines", OffscreenSettings(width = 32, height = 32)) as Lines
        app.frames(4)
        assertTrue(scene.cameraCalls >= 3, "the camera was asked each frame (${scene.cameraCalls})")
        assertTrue(scene.debug.lineCount > 0, "the lines are there")
    }
}

/** A triangle through a graphics pipeline: shaders from files, a vertex layout, a mesh, a push constant. */
class Triangle : Scene() {
    val readback: Buffer = readback(SIZE)
    lateinit var pipeline: GraphicsPipeline
    lateinit var mesh: Mesh

    override fun initialize() {
        val vert = shaderFile("tri.vert", """
            #version 450
            layout(location = 0) in vec2 position;
            void main() { gl_Position = vec4(position, 0.0, 1.0); }
        """)
        val frag = shaderFile("tri.frag", """
            #version 450
            layout(push_constant) uniform Push { vec4 tint; } push;
            layout(location = 0) out vec4 color;
            void main() { color = push.tint; }
        """)
        val layout = VertexLayout(listOf(VertexInputBindingDescription(0, 8)),
                                  listOf(VertexLayout.Attribute.atLocation(0, 0, 0, ChannelType.eFloat, 2)))
        pipeline = GraphicsPipeline.Builder()
            .setVertexShader(Shader.Builder().setPath(vert).setStage(ShaderStage.eVertex).build(), layout)
            .setFragmentShader(Shader.Builder().setPath(frag).setStage(ShaderStage.eFragment).build())
            .setDepthStencilState(koral.DepthStencilState(depthTestEnable = false, depthWriteEnable = false))
            .build()
        mesh = Mesh.Builder()
            .setVertexBuffer(0, Mesh.makeBuffer(floatArrayOf(-1f, -1f, 3f, -1f, -1f, 3f), BufferUsage.eVertex))
            .setVertexLayout(layout)
            .build()
        graph.add(object : RenderPass("Triangle") {
            override fun setup(builder: PassBuilder) { builder.write(FrameGraph.Screen, ImageUsage.eColorAttachment) }
            override fun record(commands: CommandBuffer) {
                commands.beginRendering(koral.RenderInfo().setClearColor(Vec4(0f, 0f, 0f, 1f)))
                    .bindGraphicsPipeline(pipeline)
                    .pushConstant("tint", Vec4(1f, 0f, 1f, 1f))
                    .bindMesh(mesh).draw()
                    .endRendering()
                check(commands.ok) { commands.errors.joinToString() }
            }
        })
        graph.add(ReadScreen(readback))
    }
}

class TriangleTest {
    @Test
    fun aTriangleDraws() = headlessApp().use { app ->
        val scene = app.openOffscreen("Triangle", Triangle(), OffscreenSettings(width = SIZE, height = SIZE))
        assertTrue(scene.pipeline.isValid, "a graphics pipeline: ${scene.pipeline.failure}")
        app.frames(3)
        assertEquals(listOf(255, 0, 255, 255), scene.readback.pixel(SIZE, SIZE / 2, SIZE / 2), "the triangle, tinted by its push constant")
        assertNotNull(scene.mesh.vertexBuffers.firstOrNull())
        assertFalse(scene.mesh.hasIndexBuffer)
    }
}
