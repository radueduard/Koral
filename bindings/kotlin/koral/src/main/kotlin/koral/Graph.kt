package koral

import java.lang.foreign.Arena
import java.lang.foreign.FunctionDescriptor
import java.lang.foreign.MemorySegment
import java.lang.foreign.ValueLayout
import java.lang.foreign.ValueLayout.ADDRESS
import java.lang.invoke.MethodHandles
import koral.interop.KoralLayouts
import koral.interop.KoralNative
import koral.interop.Native

/**
 * kor::ImageDesc: an image the graph makes for a pass — sized to the screen ([scale] of it), to another
 * resource ([sizeOf]), or to a fixed [extent].
 */
data class ImageDesc(
    val format: ImageFormat = ImageFormat.eRGBA8_UNORM,
    val usage: Set<ImageUsage> = emptySet(),
    val scale: Float = 1f,
    val sizeOf: String = "",
    val extent: UVec2? = null,
    val mipLevels: Int = 1,
)

/** kor::BufferDesc: a buffer the graph makes for a pass. */
data class BufferDesc(val size: Long, val usage: Set<BufferUsage> = setOf(BufferUsage.eStorage), val type: BufferType = BufferType.eDeviceLocal)

/** Something a frame graph runs: a pass written in Kotlin, or one a library implements natively. */
abstract class GraphPass {
    internal var native: MemorySegment = MemorySegment.NULL
    private var enabledBefore = true
    internal fun attach(native: MemorySegment) {
        this.native = native
        if (!enabledBefore) KoralNative.koral_pass_set_enabled(native, false)
    }

    /** Adds itself to [graph], returning the native pass. */
    protected abstract fun addTo(graph: MemorySegment): MemorySegment
    internal fun addToGraph(graph: MemorySegment): MemorySegment = addTo(graph)

    /** A disabled pass is skipped; what reads its outputs reads what was there before it. */
    var enabled: Boolean
        get() = if (native != MemorySegment.NULL) KoralNative.koral_pass_enabled(native) else enabledBefore
        set(value) {
            enabledBefore = value
            if (native != MemorySegment.NULL) KoralNative.koral_pass_set_enabled(native, value)
        }
}

/**
 * kor::RenderPass, in Kotlin: [setup] says what it uses, [initialize] looks it up (again whenever it
 * changes), [prepare] runs on the main thread before recording, and [record] records — on a worker thread,
 * alongside other passes, so it should only read what the pass holds.
 */
abstract class RenderPass(val name: String) : GraphPass() {
    abstract fun setup(builder: PassBuilder)
    open fun initialize(resources: PassResources) {}
    open fun prepare() {}
    abstract fun record(commands: CommandBuffer)

    /** Whether a resource called [name] had a previous frame's version this frame. */
    protected fun hasPrevious(name: String): Boolean = native != MemorySegment.NULL && KoralNative.koral_pass_has_previous(native, name)
    /** Runs [initialize] again before the next frame: what changed something it looked up should call. */
    protected fun requestInitialize() { if (native != MemorySegment.NULL) KoralNative.koral_pass_request_initialize(native) }

    override fun addTo(graph: MemorySegment): MemorySegment = Arena.ofConfined().use { a ->
        val user = Handles.put(this)
        val callbacks = Fields(a, KoralLayouts.KoralPassCallbacks)
            .address("user", user)
            .address("setup", PassBridge.setupStub)
            .address("initialize", PassBridge.initializeStub)
            .address("prepare", PassBridge.prepareStub)
            .address("destroy", PassBridge.destroyStub)
        if (this is CpuPass) callbacks.address("run", PassBridge.runStub) else callbacks.address("record", PassBridge.recordStub)
        val added = KoralNative.koral_graph_add(graph, name, callbacks.segment)
        if (added == MemorySegment.NULL) Handles.free(user)
        checked(added, "adding the pass $name")
    }
}

/** kor::CpuPass: work on the CPU, ordered with the graph's passes by what it reads and writes. */
abstract class CpuPass(name: String) : RenderPass(name) {
    abstract fun run()
    final override fun record(commands: CommandBuffer) {}
}

internal object PassBridge {
    private val lookup = MethodHandles.lookup()
    private fun stub(name: String, descriptor: FunctionDescriptor): MemorySegment =
        Native.linker.upcallStub(lookup.findStatic(PassBridge::class.java, name, descriptor.toMethodType()), descriptor, Arena.global())

    val setupStub by lazy { stub("onSetup", FunctionDescriptor.ofVoid(ADDRESS, ADDRESS, ADDRESS)) }
    val initializeStub by lazy { stub("onInitialize", FunctionDescriptor.ofVoid(ADDRESS, ADDRESS, ADDRESS)) }
    val prepareStub by lazy { stub("onPrepare", FunctionDescriptor.ofVoid(ADDRESS, ADDRESS)) }
    val recordStub by lazy { stub("onRecord", FunctionDescriptor.ofVoid(ADDRESS, ADDRESS, ADDRESS)) }
    val runStub by lazy { stub("onRun", FunctionDescriptor.ofVoid(ADDRESS, ADDRESS)) }
    val destroyStub by lazy { stub("onDestroy", FunctionDescriptor.ofVoid(ADDRESS)) }

    private inline fun run(user: MemorySegment, what: String, body: (RenderPass) -> Unit) {
        val pass = Handles.get<RenderPass>(user) ?: return
        try { body(pass) } catch (e: Throwable) { Log.error("[${pass.name}] $what threw $e\n${e.stackTraceToString()}") }
    }

    @JvmStatic fun onSetup(native: MemorySegment, builder: MemorySegment, user: MemorySegment) = run(user, "setup") {
        it.attach(native)
        it.setup(PassBuilder(builder))
    }
    @JvmStatic fun onInitialize(native: MemorySegment, resources: MemorySegment, user: MemorySegment) =
        run(user, "initialize") { it.initialize(PassResources(resources)) }
    @JvmStatic fun onPrepare(native: MemorySegment, user: MemorySegment) = run(user, "prepare") { it.prepare() }
    @JvmStatic fun onRecord(native: MemorySegment, commands: MemorySegment, user: MemorySegment) =
        run(user, "record") { it.record(CommandBuffer(commands)) }
    @JvmStatic fun onRun(native: MemorySegment, user: MemorySegment) = run(user, "run") { (it as CpuPass).run() }
    @JvmStatic fun onDestroy(user: MemorySegment) {
        Handles.get<RenderPass>(user)?.native = MemorySegment.NULL
        Handles.free(user)
    }
}

/**
 * kor::DebugDrawPass: draws [draw]'s lines over [target], seen through [viewProjection] (asked each frame),
 * depth-tested against [depth] when one is named.
 */
class DebugDrawPass(private val draw: DebugDraw, private val viewProjection: () -> Mat4, private val target: String = FrameGraph.Screen,
                    private val depth: String? = null) : GraphPass() {
    override fun addTo(graph: MemorySegment): MemorySegment {
        val user = Handles.put(viewProjection)
        val added = KoralNative.koral_graph_add_debug_draw_pass(graph, draw.native, DebugCamera.stub, user, DebugCamera.free, target, depth)
        if (added == MemorySegment.NULL) Handles.free(user)
        return checked(added, "adding the debug-draw pass")
    }
}

internal object DebugCamera {
    private val lookup = MethodHandles.lookup()
    val stub: MemorySegment by lazy {
        val d = FunctionDescriptor.ofVoid(ADDRESS, ADDRESS)
        Native.linker.upcallStub(lookup.findStatic(DebugCamera::class.java, "camera", d.toMethodType()), d, Arena.global())
    }
    val free: MemorySegment by lazy {
        val d = FunctionDescriptor.ofVoid(ADDRESS)
        Native.linker.upcallStub(lookup.findStatic(DebugCamera::class.java, "release", d.toMethodType()), d, Arena.global())
    }

    @JvmStatic fun camera(matrix: MemorySegment, user: MemorySegment) {
        val out = matrix.reinterpret(64)
        val m = try { Handles.get<() -> Mat4>(user)?.invoke() ?: Mat4.Identity } catch (e: Throwable) {
            Log.error("[DebugDraw] the camera threw $e")
            Mat4.Identity
        }
        m.toArray().forEachIndexed { i, f -> out.setAtIndex(ValueLayout.JAVA_FLOAT, i.toLong(), f) }
    }
    @JvmStatic fun release(user: MemorySegment) = Handles.free(user)
}

/** kor::PassBuilder: what a pass says, in its setup, about what it uses. */
class PassBuilder internal constructor(private val native: MemorySegment) {
    fun read(name: String) = apply { KoralNative.koral_pass_builder_read(native, name, 0, 0) }
    fun read(name: String, vararg usage: ImageUsage) = apply { KoralNative.koral_pass_builder_read(native, name, 1, bits(usage)) }
    fun read(name: String, vararg usage: BufferUsage) = apply { KoralNative.koral_pass_builder_read(native, name, 2, bits(usage)) }
    fun write(name: String) = apply { KoralNative.koral_pass_builder_write(native, name, 0, 0) }
    fun write(name: String, vararg usage: ImageUsage) = apply { KoralNative.koral_pass_builder_write(native, name, 1, bits(usage)) }
    fun write(name: String, vararg usage: BufferUsage) = apply { KoralNative.koral_pass_builder_write(native, name, 2, bits(usage)) }

    /** Create: an image the graph makes (and may share memory of with others whose lives do not overlap). */
    fun create(name: String, desc: ImageDesc) = apply {
        Arena.ofConfined().use { a ->
            val d = Fields(a, KoralLayouts.KoralImageDesc).int("format", desc.format.value)
                .int("usage", desc.usage.fold(0) { acc, u -> acc or u.value }).float("scale", desc.scale)
                .string(a, "size_of", desc.sizeOf).bool("has_extent", desc.extent != null).int("mip_levels", desc.mipLevels)
            desc.extent?.let { d.ints("extent", it.x, it.y) }
            KoralNative.koral_pass_builder_create_image(native, name, d.segment)
        }
    }

    fun create(name: String, desc: BufferDesc) = apply {
        Arena.ofConfined().use { a ->
            KoralNative.koral_pass_builder_create_buffer(native, name, Fields(a, KoralLayouts.KoralBufferDesc).long("size", desc.size)
                .int("usage", desc.usage.fold(0) { acc, u -> acc or u.value }).int("type", desc.type.value).segment)
        }
    }

    /** Consume: takes [name] over, as [as] — it is no longer [name] after this pass. */
    fun consume(name: String, `as`: String) = apply { KoralNative.koral_pass_builder_consume(native, name, `as`, 0, 0) }
    fun consume(name: String, `as`: String, vararg usage: ImageUsage) = apply { KoralNative.koral_pass_builder_consume(native, name, `as`, 1, bits(usage)) }
    fun consume(name: String, `as`: String, vararg usage: BufferUsage) = apply { KoralNative.koral_pass_builder_consume(native, name, `as`, 2, bits(usage)) }
    /** Runs even though nothing reads what it writes: a readback, a present. */
    fun sideEffect() = apply { KoralNative.koral_pass_builder_side_effect(native) }
    /** Reads [name] as the last frame left it. */
    fun readPrevious(name: String) = apply { KoralNative.koral_pass_builder_read_previous(native, name, 0, 0) }
    fun readPrevious(name: String, vararg usage: ImageUsage) = apply { KoralNative.koral_pass_builder_read_previous(native, name, 1, bits(usage)) }
    fun readPrevious(name: String, vararg usage: BufferUsage) = apply { KoralNative.koral_pass_builder_read_previous(native, name, 2, bits(usage)) }
    /** Runs on the async-compute queue, alongside the graphics work, where there is one. */
    fun asyncCompute() = apply { KoralNative.koral_pass_builder_async_compute(native) }
}

/** kor::PassResources: the graph's resources, by name, in a pass's initialize. Borrowed. */
class PassResources internal constructor(private val native: MemorySegment) {
    fun imageNamed(name: String): Image = Resource.wrap(checked(KoralNative.koral_pass_resources_image_named(native, name), "the image $name"))!!
    fun bufferNamed(name: String): Buffer = Resource.wrap(checked(KoralNative.koral_pass_resources_buffer_named(native, name), "the buffer $name"))!!
    fun writableBufferNamed(name: String): Buffer =
        Resource.wrap(checked(KoralNative.koral_pass_resources_writable_buffer_named(native, name), "the buffer $name"))!!
    fun extent(name: String): UVec2 = twoInts({ x, y -> KoralNative.koral_pass_resources_extent(native, name, x, y) }, ::UVec2)
    fun previousImageNamed(name: String): Image? = Resource.wrap(KoralNative.koral_pass_resources_previous_image_named(native, name))
    fun previousBufferNamed(name: String): Buffer? = Resource.wrap(KoralNative.koral_pass_resources_previous_buffer_named(native, name))
}

/** kor::FrameGraph: a scene's frame, as passes, ordered and synchronised by what they read and write. */
class FrameGraph internal constructor(internal val native: MemorySegment) {
    /** Adds [pass], and returns it. */
    fun <P : GraphPass> add(pass: P): P {
        check(pass.native == MemorySegment.NULL) { "the pass is already in a graph" }
        pass.attach(pass.addToGraph(native))
        return pass
    }

    /** Import: a resource of one's own, under [name], for passes to read and write. */
    fun import(name: String, image: Image) { KoralNative.koral_graph_import_image(native, name, image.native); checkLastError() }
    fun import(name: String, buffer: Buffer) { KoralNative.koral_graph_import_buffer(native, name, buffer.native); checkLastError() }
    /** Compiles the graph again before the next frame. */
    fun invalidate() = KoralNative.koral_graph_invalidate(native)
    val isEmpty: Boolean get() = KoralNative.koral_graph_empty(native)

    data class Scheduled(val name: String, val level: Int, val async: Boolean)
    data class Skipped(val name: String, val resource: String, val source: String)
    data class Timing(val prepareMs: Double, val recordWallMs: Double, val recordWorkMs: Double, val gpuMs: Double)
    data class MemoryUse(val bytes: Long, val unsharedBytes: Long, val resources: Int, val allocations: Int)

    /** The passes in the order they run, with their dependency level. */
    val schedule: List<Scheduled>
        get() = Arena.ofConfined().use { a ->
            val level = a.allocate(ValueLayout.JAVA_INT)
            val async = a.allocate(ValueLayout.JAVA_BOOLEAN)
            List(KoralNative.koral_graph_schedule_count(native)) {
                val name = KoralNative.koral_graph_schedule(native, it, level, async)
                Scheduled(name, level.get(ValueLayout.JAVA_INT, 0), async.get(ValueLayout.JAVA_BOOLEAN, 0))
            }
        }

    /** Passes nothing needed, so not run. */
    val culledPasses: List<String> get() = List(KoralNative.koral_graph_culled_pass_count(native)) { KoralNative.koral_graph_culled_pass(native, it) }

    /** Passes that could not run, and the resource that stopped them. */
    val skippedPasses: List<Skipped>
        get() = Arena.ofConfined().use { a ->
            val resource = a.allocate(ADDRESS)
            val source = a.allocate(ADDRESS)
            List(KoralNative.koral_graph_skipped_pass_count(native)) {
                val name = KoralNative.koral_graph_skipped_pass(native, it, resource, source)
                Skipped(name, Native.kString(resource.get(ADDRESS, 0)), Native.kString(source.get(ADDRESS, 0)))
            }
        }

    val timing: Timing
        get() = Arena.ofConfined().use { a ->
            val v = a.allocate(ValueLayout.JAVA_DOUBLE, 4)
            KoralNative.koral_graph_timing(native, v, v.asSlice(8), v.asSlice(16), v.asSlice(24))
            Timing(v.getAtIndex(ValueLayout.JAVA_DOUBLE, 0), v.getAtIndex(ValueLayout.JAVA_DOUBLE, 1),
                   v.getAtIndex(ValueLayout.JAVA_DOUBLE, 2), v.getAtIndex(ValueLayout.JAVA_DOUBLE, 3))
        }

    fun imageNamed(name: String, vararg usage: ImageUsage): Image? = Resource.borrowed(KoralNative.koral_graph_image_named(native, name, bits(usage)))

    /** Whether resources whose lives do not overlap share memory. */
    var aliasing: Boolean
        get() = KoralNative.koral_graph_aliasing(native)
        set(value) = KoralNative.koral_graph_set_aliasing(native, value)

    val memory: MemoryUse
        get() = Arena.ofConfined().use { a ->
            val bytes = a.allocate(ValueLayout.JAVA_LONG, 2)
            val counts = a.allocate(ValueLayout.JAVA_INT, 2)
            KoralNative.koral_graph_memory(native, bytes, bytes.asSlice(8), counts, counts.asSlice(4))
            MemoryUse(bytes.getAtIndex(ValueLayout.JAVA_LONG, 0), bytes.getAtIndex(ValueLayout.JAVA_LONG, 1),
                      counts.getAtIndex(ValueLayout.JAVA_INT, 0), counts.getAtIndex(ValueLayout.JAVA_INT, 1))
        }

    fun hasPrevious(name: String): Boolean = KoralNative.koral_graph_has_previous(native, name)

    companion object {
        /** The scene's screen: its window's image, or its offscreen target. */
        const val Screen = "screen"
    }
}
