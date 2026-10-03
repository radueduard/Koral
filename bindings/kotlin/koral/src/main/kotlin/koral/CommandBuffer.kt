@file:OptIn(ExperimentalUnsignedTypes::class)

package koral

import java.lang.foreign.Arena
import java.lang.foreign.FunctionDescriptor
import java.lang.foreign.MemorySegment
import java.lang.foreign.SegmentAllocator
import java.lang.foreign.ValueLayout
import java.lang.foreign.ValueLayout.ADDRESS
import java.lang.invoke.MethodHandles
import koral.interop.KoralLayouts
import koral.interop.KoralNative
import koral.interop.Native

/**
 * kor::CommandBuffer: what a pass records into — every command the C++ one has, chained the same way:
 *
 * ```
 * override fun record(commands: CommandBuffer) {
 *     commands.beginRendering()
 *         .bindGraphicsPipeline(pipeline)
 *         .bindDescriptorSet(0, set)
 *         .pushConstant("tint", Vec4(1f, 0.5f, 0f, 1f))
 *         .bindMesh(mesh).drawIndexed()
 *         .endRendering()
 * }
 * ```
 *
 * A pass's is valid only while it records. One made with [create] is the scene's (or the application's),
 * and closed with it.
 */
class CommandBuffer internal constructor(native: MemorySegment, private val owned: Boolean = false) : AutoCloseable {
    private var handle = native
    private val n: MemorySegment
        get() {
            check(handle != MemorySegment.NULL) { "a command buffer used after it was closed" }
            return handle
        }

    // ---- one of one's own ----------------------------------------------------------------------------

    fun begin() = apply { KoralNative.koral_cmd_begin(n) }
    fun end() = KoralNative.koral_cmd_end(n)
    val isRecording: Boolean get() = KoralNative.koral_cmd_is_recording(n)

    /** Submit({waitFor, signal}): runs on the GPU after [waitFor] are done, signalling [signal] when it is. */
    fun submit(waitFor: List<Token> = emptyList(), signal: List<Token> = emptyList()) = Arena.ofConfined().use { a ->
        fun list(tokens: List<Token>): MemorySegment {
            if (tokens.isEmpty()) return MemorySegment.NULL
            val s = a.allocate(ADDRESS, tokens.size.toLong())
            tokens.forEachIndexed { i, t -> s.setAtIndex(ADDRESS, i.toLong(), t.native) }
            return s
        }
        checked(KoralNative.koral_cmd_submit(n, list(waitFor), waitFor.size.toLong(), list(signal), signal.size.toLong()), "submitting")
    }

    fun reset() = KoralNative.koral_cmd_reset(n)
    fun waitForFence() = KoralNative.koral_cmd_wait_for_fence(n)

    override fun close() {
        if (handle == MemorySegment.NULL) return
        if (owned) {
            KoralNative.koral_cmd_destroy(handle)
            Ownership.release(this)
        }
        handle = MemorySegment.NULL
    }

    // ---- what went wrong -------------------------------------------------------------------------------

    /** Whether everything recorded so far was valid; [errors] says what was not. */
    val ok: Boolean get() = KoralNative.koral_cmd_ok(n)
    val errors: List<String> get() = List(KoralNative.koral_cmd_error_count(n)) { KoralNative.koral_cmd_error(n, it) }
    fun hasTouched(image: Image): Boolean = KoralNative.koral_cmd_has_touched(n, image.native)

    // ---- timers -----------------------------------------------------------------------------------------

    fun beginTimer(label: String) = apply { KoralNative.koral_cmd_begin_timer(n, label) }
    fun endTimer() = apply { KoralNative.koral_cmd_end_timer(n) }
    fun timer(label: String, body: (CommandBuffer) -> Unit) = apply { beginTimer(label); body(this); endTimer() }
    /** The last frame's time of the timer [label], in milliseconds. */
    fun collectTimer(label: String): Double = Arena.ofConfined().use { a ->
        val ms = a.allocate(ValueLayout.JAVA_DOUBLE)
        checked(KoralNative.koral_cmd_collect_timer(n, label, ms), "collecting the timer $label")
        ms.get(ValueLayout.JAVA_DOUBLE, 0)
    }
    /** Every timer's last result. */
    val timings: List<TimerResult>
        get() = Arena.ofConfined().use { a ->
            val ms = a.allocate(ValueLayout.JAVA_DOUBLE)
            val depth = a.allocate(ValueLayout.JAVA_INT)
            List(KoralNative.koral_cmd_collect_timings(n)) {
                val label = KoralNative.koral_cmd_timing(n, it, ms, depth)
                TimerResult(label, ms.get(ValueLayout.JAVA_DOUBLE, 0), depth.get(ValueLayout.JAVA_INT, 0))
            }
        }
    val supportsTimers: Boolean get() = KoralNative.koral_cmd_supports_timers(n)
    val lastFrameCommandCount: Long get() = KoralNative.koral_cmd_last_frame_command_count(n)

    // ---- rendering and dynamic state ------------------------------------------------------------------------

    /** BeginRendering: into the screen (the scene's window, or its offscreen target) by default. */
    fun beginRendering(info: RenderInfo? = null) = apply {
        Arena.ofConfined().use { a -> KoralNative.koral_cmd_begin_rendering(n, info?.native(a) ?: MemorySegment.NULL) }
    }
    fun beginRendering(framebuffer: Framebuffer) = beginRendering(RenderInfo(framebuffer))
    fun endRendering() = apply { KoralNative.koral_cmd_end_rendering(n) }
    fun setViewport(x: Int, y: Int, width: Int, height: Int) = apply { KoralNative.koral_cmd_set_viewport(n, x, y, width, height) }
    fun setScissor(x: Int, y: Int, width: Int, height: Int) = apply { KoralNative.koral_cmd_set_scissor(n, x, y, width, height) }
    fun setLineWidth(width: Float) = apply { KoralNative.koral_cmd_set_line_width(n, width) }
    fun setDepthBias(constantFactor: Float, clamp: Float, slopeFactor: Float) = apply { KoralNative.koral_cmd_set_depth_bias(n, constantFactor, clamp, slopeFactor) }
    fun setBlendConstants(constants: Vec4) = apply {
        Arena.ofConfined().use { a -> KoralNative.koral_cmd_set_blend_constants(n, a.allocateFrom(ValueLayout.JAVA_FLOAT, *constants.toArray())) }
    }
    fun setStencilCompareMask(face: StencilFace, mask: Int) = apply { KoralNative.koral_cmd_set_stencil_compare_mask(n, face.value, mask) }
    fun setStencilWriteMask(face: StencilFace, mask: Int) = apply { KoralNative.koral_cmd_set_stencil_write_mask(n, face.value, mask) }
    fun setStencilReference(face: StencilFace, reference: Int) = apply { KoralNative.koral_cmd_set_stencil_reference(n, face.value, reference) }
    fun setCullMode(vararg cullMode: CullMode) = apply { KoralNative.koral_cmd_set_cull_mode(n, cullMode.fold(0) { acc, c -> acc or c.value }) }
    fun setFrontFace(frontFace: FrontFace) = apply { KoralNative.koral_cmd_set_front_face(n, frontFace.value) }
    fun setDepthTestEnable(enable: Boolean) = apply { KoralNative.koral_cmd_set_depth_test_enable(n, enable) }
    fun setDepthWriteEnable(enable: Boolean) = apply { KoralNative.koral_cmd_set_depth_write_enable(n, enable) }
    fun setDepthCompareOp(op: CompareOp) = apply { KoralNative.koral_cmd_set_depth_compare_op(n, op.value) }
    fun setStencilTestEnable(enable: Boolean) = apply { KoralNative.koral_cmd_set_stencil_test_enable(n, enable) }
    fun setStencilOp(face: StencilFace, failOp: StencilOp, passOp: StencilOp, depthFailOp: StencilOp, compareOp: CompareOp) = apply {
        KoralNative.koral_cmd_set_stencil_op(n, face.value, failOp.value, passOp.value, depthFailOp.value, compareOp.value)
    }
    fun setDepthBiasEnable(enable: Boolean) = apply { KoralNative.koral_cmd_set_depth_bias_enable(n, enable) }
    fun setRasterizerDiscardEnable(enable: Boolean) = apply { KoralNative.koral_cmd_set_rasterizer_discard_enable(n, enable) }
    fun setPrimitiveRestartEnable(enable: Boolean) = apply { KoralNative.koral_cmd_set_primitive_restart_enable(n, enable) }

    // ---- binding --------------------------------------------------------------------------------------------

    fun bindComputePipeline(pipeline: ComputePipeline) = apply { KoralNative.koral_cmd_bind_compute_pipeline(n, pipeline.native) }
    fun bindGraphicsPipeline(pipeline: GraphicsPipeline) = apply { KoralNative.koral_cmd_bind_graphics_pipeline(n, pipeline.native) }
    fun bindRayTracingPipeline(pipeline: RayTracingPipeline) = apply { KoralNative.koral_cmd_bind_ray_tracing_pipeline(n, pipeline.native) }
    fun bindDescriptorSet(index: Int, set: DescriptorSet) = apply { KoralNative.koral_cmd_bind_descriptor_set(n, index, set.native) }
    fun bindMesh(mesh: Mesh) = apply { KoralNative.koral_cmd_bind_mesh(n, mesh.native) }

    /** PushConstantBlock: [data] (a primitive array or a MemorySegment) at byte [offset] of the push-constant block. */
    fun pushConstantBlock(data: Any, offset: Int = 0) = apply {
        Arena.ofConfined().use { a ->
            val s = segmentOf(a, data)
            KoralNative.koral_cmd_push_constant_block(n, s, s.byteSize().toInt(), offset)
        }
    }

    /**
     * PushConstant(name, value): the push constant the shaders call [name] — a Float, Int, UInt (`7u`), Vec2/3/4,
     * IVec, UVec or Mat4, or an array of floats, ints or uints — checked against the shader's type, as in C++.
     */
    fun pushConstant(name: String, value: Any) = apply {
        Arena.ofConfined().use { a ->
            val (bytes, shape) = ValueShape.encode(a, value)
            KoralNative.koral_cmd_push_constant(n, name, bytes, bytes.byteSize().toInt(), shape.native(a))
        }
    }

    // ---- synchronisation and labels -------------------------------------------------------------------------

    fun barrier(buffers: List<BufferBarrier> = emptyList(), images: List<ImageBarrier> = emptyList()) = apply {
        Arena.ofConfined().use { a ->
            val b = a.allocate(KoralLayouts.KoralBufferBarrier, maxOf(1, buffers.size).toLong())
            buffers.forEachIndexed { i, x -> x.write(b.asSlice(i * KoralLayouts.KoralBufferBarrier.byteSize())) }
            val im = a.allocate(KoralLayouts.KoralImageBarrier, maxOf(1, images.size).toLong())
            images.forEachIndexed { i, x -> x.write(im.asSlice(i * KoralLayouts.KoralImageBarrier.byteSize())) }
            KoralNative.koral_cmd_barrier(n, b, buffers.size.toLong(), im, images.size.toLong())
        }
    }
    fun bufferBarrier(barrier: BufferBarrier) = barrier(buffers = listOf(barrier))
    fun imageBarrier(barrier: ImageBarrier) = barrier(images = listOf(barrier))

    fun beginDebugLabel(label: String, color: Vec4 = Vec4.One) = apply {
        Arena.ofConfined().use { a -> KoralNative.koral_cmd_begin_debug_label(n, label, a.allocateFrom(ValueLayout.JAVA_FLOAT, *color.toArray())) }
    }
    fun endDebugLabel() = apply { KoralNative.koral_cmd_end_debug_label(n) }
    fun insertDebugLabel(label: String, color: Vec4 = Vec4.One) = apply {
        Arena.ofConfined().use { a -> KoralNative.koral_cmd_insert_debug_label(n, label, a.allocateFrom(ValueLayout.JAVA_FLOAT, *color.toArray())) }
    }
    fun debugLabel(label: String, color: Vec4 = Vec4.One, body: (CommandBuffer) -> Unit) = apply {
        beginDebugLabel(label, color); body(this); endDebugLabel()
    }

    // ---- work -----------------------------------------------------------------------------------------------

    fun dispatch(groupCountX: Int = 1, groupCountY: Int = 1, groupCountZ: Int = 1) = apply { KoralNative.koral_cmd_dispatch(n, groupCountX, groupCountY, groupCountZ) }
    fun dispatchIndirect(buffer: Buffer, offset: Long = 0) = apply { KoralNative.koral_cmd_dispatch_indirect(n, buffer.native, offset) }
    fun traceRays(width: Int = 1, height: Int = 1, depth: Int = 1) = apply { KoralNative.koral_cmd_trace_rays(n, width, height, depth) }
    /** Draw: every vertex of the bound mesh, by default. */
    fun draw(vertexCount: Long = WholeSize, instanceCount: Int = 1, firstVertex: Int = 0, firstInstance: Int = 0) = apply {
        KoralNative.koral_cmd_draw(n, vertexCount, instanceCount, firstVertex, firstInstance)
    }
    fun drawIndexed(indexCount: Long = WholeSize, instanceCount: Int = 1, firstIndex: Int = 0, vertexOffset: Int = 0, firstInstance: Int = 0) = apply {
        KoralNative.koral_cmd_draw_indexed(n, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance)
    }
    fun drawMesh(mesh: Mesh, instanceCount: Int = 1, baseInstance: Int = 0) = apply { KoralNative.koral_cmd_draw_mesh(n, mesh.native, instanceCount, baseInstance) }
    fun drawSubMesh(mesh: Mesh, baseIndex: Int, indexCount: Int) = apply { KoralNative.koral_cmd_draw_sub_mesh(n, mesh.native, baseIndex, indexCount) }
    fun drawMeshTasks(x: Int = 1, y: Int = 1, z: Int = 1) = apply { KoralNative.koral_cmd_draw_mesh_tasks(n, x, y, z) }
    fun drawIndirect(buffer: Buffer, offset: Long = 0, drawCount: Int = 1, stride: Int = 0) = apply { KoralNative.koral_cmd_draw_indirect(n, buffer.native, offset, drawCount, stride) }
    fun drawIndexedIndirect(buffer: Buffer, offset: Long = 0, drawCount: Int = 1, stride: Int = 0) = apply {
        KoralNative.koral_cmd_draw_indexed_indirect(n, buffer.native, offset, drawCount, stride)
    }
    fun drawMeshTasksIndirect(buffer: Buffer, offset: Long = 0, drawCount: Int = 1, stride: Int = 0) = apply {
        KoralNative.koral_cmd_draw_mesh_tasks_indirect(n, buffer.native, offset, drawCount, stride)
    }

    // ---- transfers ------------------------------------------------------------------------------------------

    fun clearBuffer(buffer: Buffer, offset: Long = 0, size: Long = WholeSize) = apply { KoralNative.koral_cmd_clear_buffer(n, buffer.native, offset, size) }
    fun clearColorImage(image: Image, color: Vec4 = Vec4(0f, 0f, 0f, 1f)) = apply {
        Arena.ofConfined().use { a -> KoralNative.koral_cmd_clear_color_image(n, image.native, a.allocateFrom(ValueLayout.JAVA_FLOAT, *color.toArray())) }
    }
    fun clearColorImage(image: Image, r: Float, g: Float, b: Float, a: Float = 1f) = clearColorImage(image, Vec4(r, g, b, a))
    /** FillBuffer: [data] (a primitive array or a MemorySegment) written at byte [offset], in order with the other commands. */
    fun fillBuffer(buffer: Buffer, data: Any, offset: Long = 0) = apply {
        Arena.ofConfined().use { a ->
            val s = segmentOf(a, data)
            KoralNative.koral_cmd_fill_buffer(n, buffer.native, s, offset, s.byteSize())
        }
    }
    fun copyBuffer(source: Buffer, destination: Buffer, size: Long = WholeSize, sourceOffset: Long = 0, destinationOffset: Long = 0) = apply {
        KoralNative.koral_cmd_copy_buffer(n, source.native, destination.native, size, sourceOffset, destinationOffset)
    }
    fun blitToScreen(source: Image, blit: Blit = Blit()) = apply {
        Arena.ofConfined().use { a -> KoralNative.koral_cmd_blit_to_screen(n, source.native, blit.native(a)) }
    }
    fun blit(source: Image, destination: Image, blit: Blit = Blit()) = apply {
        Arena.ofConfined().use { a -> KoralNative.koral_cmd_blit(n, source.native, destination.native, blit.native(a)) }
    }
    fun copyImage(source: Image, destination: Image) = apply { KoralNative.koral_cmd_copy_image(n, source.native, destination.native) }
    fun resolveToScreen(source: Image, resolve: Resolve = Resolve()) = apply {
        Arena.ofConfined().use { a -> KoralNative.koral_cmd_resolve_to_screen(n, source.native, resolve.native(a)) }
    }
    fun resolve(source: Image, destination: Image, resolve: Resolve = Resolve()) = apply {
        Arena.ofConfined().use { a -> KoralNative.koral_cmd_resolve(n, source.native, destination.native, resolve.native(a)) }
    }
    fun generateMipmaps(image: Image) = apply { KoralNative.koral_cmd_generate_mipmaps(n, image.native) }
    fun copyBufferToImage(buffer: Buffer, image: Image, copy: Copy? = null) = apply {
        Arena.ofConfined().use { a -> KoralNative.koral_cmd_copy_buffer_to_image(n, buffer.native, image.native, copy?.native(a) ?: MemorySegment.NULL) }
    }
    fun copyImageToBuffer(image: Image, buffer: Buffer, copy: Copy? = null) = apply {
        Arena.ofConfined().use { a -> KoralNative.koral_cmd_copy_image_to_buffer(n, image.native, buffer.native, copy?.native(a) ?: MemorySegment.NULL) }
    }

    // ---- control --------------------------------------------------------------------------------------------

    /** Run: [command], recorded here — in a graph, when the pass's record runs. */
    fun run(command: (CommandBuffer) -> Unit) = apply {
        val user = Handles.put(command)
        try { KoralNative.koral_cmd_run(n, CommandCallbacks.run, user) } finally { Handles.free(user) }
    }
    fun runIf(condition: Boolean, onTrue: (CommandBuffer) -> Unit, onFalse: ((CommandBuffer) -> Unit)? = null) = apply {
        if (condition) onTrue(this) else onFalse?.invoke(this)
    }
    fun <T> forEach(items: Iterable<T>, body: (CommandBuffer, T) -> Unit) = apply { for (item in items) body(this, item) }

    companion object {
        const val WholeSize: Long = -1L

        /** One of one's own, recorded and submitted by hand: the scene's (or the application's), closed with it. */
        fun create(usage: CommandBufferUsage = CommandBufferUsage.eGraphics): CommandBuffer =
            Ownership.adopt(CommandBuffer(checked(KoralNative.koral_cmd_create(usage.value), "making a command buffer"), owned = true))

        /** SingleTimeCommand: records [command] now and submits it; the token is done when the GPU is. */
        fun singleTimeCommand(usage: CommandBufferUsage = CommandBufferUsage.eGraphics, command: (CommandBuffer) -> Unit): Token {
            val user = Handles.put(command)
            try {
                return Token(checked(KoralNative.koral_cmd_single_time_command(CommandCallbacks.run, user, usage.value), "a single-time command"))
            } finally {
                Handles.free(user)
            }
        }

        /** The image the screen is this frame, if any. */
        fun screenImage(): Image? = Resource.borrowed(KoralNative.koral_cmd_screen_image())
    }
}

internal object CommandCallbacks {
    val run: MemorySegment by lazy {
        val d = FunctionDescriptor.ofVoid(ADDRESS, ADDRESS)
        Native.linker.upcallStub(MethodHandles.lookup().findStatic(CommandCallbacks::class.java, "onRun", d.toMethodType()), d, Arena.global())
    }

    @JvmStatic fun onRun(commands: MemorySegment, user: MemorySegment) {
        val command = Handles.get<(CommandBuffer) -> Unit>(user) ?: return
        try { command(CommandBuffer(commands)) } catch (e: Throwable) { Log.error("[koral] a command buffer's callback threw $e\n${e.stackTraceToString()}") }
    }
}

data class TimerResult(val label: String, val milliseconds: Double, val depth: Int)

/** kor::ValueShape: what a push constant's value is — checked against the shader's declaration. */
data class ValueShape(val scalar: ValueScalar, val rows: Int = 1, val columns: Int = 1, val count: Int = 1, val known: Boolean = true) {
    internal fun native(a: SegmentAllocator) = Fields(a, KoralLayouts.KoralValueShape)
        .int("scalar", scalar.value).int("rows", rows).int("columns", columns).int("count", count).bool("known", known).segment

    internal companion object {
        fun encode(a: Arena, value: Any): Pair<MemorySegment, ValueShape> {
            fun floats(vararg f: Float) = a.allocateFrom(ValueLayout.JAVA_FLOAT, *f)
            fun ints(vararg i: Int) = a.allocateFrom(ValueLayout.JAVA_INT, *i)
            return when (value) {
                is Float -> floats(value) to ValueShape(ValueScalar.eFloat)
                is Int -> ints(value) to ValueShape(ValueScalar.eInt)
                is UInt -> ints(value.toInt()) to ValueShape(ValueScalar.eUInt)
                is Double -> a.allocateFrom(ValueLayout.JAVA_DOUBLE, value) to ValueShape(ValueScalar.eDouble)
                is Vec2 -> floats(value.x, value.y) to ValueShape(ValueScalar.eFloat, 2)
                is Vec3 -> floats(value.x, value.y, value.z) to ValueShape(ValueScalar.eFloat, 3)
                is Vec4 -> floats(*value.toArray()) to ValueShape(ValueScalar.eFloat, 4)
                is Mat4 -> floats(*value.toArray()) to ValueShape(ValueScalar.eFloat, 4, 4)
                is IVec2 -> ints(value.x, value.y) to ValueShape(ValueScalar.eInt, 2)
                is IVec3 -> ints(value.x, value.y, value.z) to ValueShape(ValueScalar.eInt, 3)
                is IVec4 -> ints(value.x, value.y, value.z, value.w) to ValueShape(ValueScalar.eInt, 4)
                is UVec2 -> ints(value.x, value.y) to ValueShape(ValueScalar.eUInt, 2)
                is UVec3 -> ints(value.x, value.y, value.z) to ValueShape(ValueScalar.eUInt, 3)
                is UVec4 -> ints(value.x, value.y, value.z, value.w) to ValueShape(ValueScalar.eUInt, 4)
                is FloatArray -> floats(*value) to ValueShape(ValueScalar.eFloat, count = value.size)
                is IntArray -> ints(*value) to ValueShape(ValueScalar.eInt, count = value.size)
                is UIntArray -> ints(*value.toIntArray()) to ValueShape(ValueScalar.eUInt, count = value.size)
                is MemorySegment -> value to ValueShape(ValueScalar.eOther, known = false)
                else -> throw IllegalArgumentException("${value.javaClass.simpleName} cannot be a push constant: give a number, a vector, a Mat4 or an array")
            }
        }
    }
}

/** kor::BufferBarrier. */
data class BufferBarrier(val buffer: Buffer, val dstAccess: ResourceAccess, val offset: Long = 0, val size: Long = CommandBuffer.WholeSize) {
    internal fun write(s: MemorySegment) {
        Fields(s, KoralLayouts.KoralBufferBarrier).address("buffer", buffer.native).int("dst_access", dstAccess.value)
            .long("offset", offset).long("size", size)
    }
}

/** kor::ImageBarrier: each range absent means the whole image. */
data class ImageBarrier(val image: Image, val dstAccess: ResourceAccess, val baseMipLevel: Int? = null, val levelCount: Int? = null,
                        val baseArrayLayer: Int? = null, val layerCount: Int? = null) {
    internal fun write(s: MemorySegment) {
        Fields(s, KoralLayouts.KoralImageBarrier).address("image", image.native).int("dst_access", dstAccess.value)
            .int("base_mip_level", baseMipLevel ?: -1).int("level_count", levelCount ?: -1)
            .int("base_array_layer", baseArrayLayer ?: -1).int("layer_count", layerCount ?: -1)
    }
}

/** kor::CommandBuffer::Blit: an extent of -1 is the whole image. */
data class Blit(
    val srcOffset: IVec3 = IVec3(0, 0, 0), val srcExtent: IVec3 = IVec3(-1, -1, -1),
    val dstOffset: IVec3 = IVec3(0, 0, 0), val dstExtent: IVec3 = IVec3(-1, -1, -1),
    val srcBaseArrayLayer: Int = 0, val dstBaseArrayLayer: Int = 0, val layerCount: Int = 1,
    val srcMipLevel: Int = 0, val dstMipLevel: Int = 0, val filtering: Filter = Filter.eNearest,
) {
    internal fun native(a: SegmentAllocator) = Fields(a, KoralLayouts.KoralBlit)
        .ivec3("src_offset", srcOffset).ivec3("src_extent", srcExtent).ivec3("dst_offset", dstOffset).ivec3("dst_extent", dstExtent)
        .int("src_base_array_layer", srcBaseArrayLayer).int("dst_base_array_layer", dstBaseArrayLayer).int("layer_count", layerCount)
        .int("src_mip_level", srcMipLevel).int("dst_mip_level", dstMipLevel).int("filtering", filtering.value).segment
}

/** kor::CommandBuffer::Resolve. */
data class Resolve(
    val srcOffset: IVec3 = IVec3(0, 0, 0), val srcExtent: IVec3 = IVec3(-1, -1, -1),
    val dstOffset: IVec3 = IVec3(0, 0, 0), val dstExtent: IVec3 = IVec3(-1, -1, -1),
    val srcBaseArrayLayer: Int = 0, val dstBaseArrayLayer: Int = 0, val layerCount: Int = 1,
    val srcMipLevel: Int = 0, val dstMipLevel: Int = 0,
) {
    internal fun native(a: SegmentAllocator) = Fields(a, KoralLayouts.KoralResolveInfo)
        .ivec3("src_offset", srcOffset).ivec3("src_extent", srcExtent).ivec3("dst_offset", dstOffset).ivec3("dst_extent", dstExtent)
        .int("src_base_array_layer", srcBaseArrayLayer).int("dst_base_array_layer", dstBaseArrayLayer).int("layer_count", layerCount)
        .int("src_mip_level", srcMipLevel).int("dst_mip_level", dstMipLevel).segment
}

/** kor::CommandBuffer::Copy: between a buffer and an image. */
data class Copy(
    val bufferOffset: Long = 0, val bufferRowLength: Long = 0, val bufferImageHeight: Long = 0,
    val imageOffset: IVec3 = IVec3(0, 0, 0), val imageExtent: IVec3 = IVec3(-1, -1, -1),
    val imageBaseArrayLayer: Int = 0, val imageLayerCount: Int = 1, val imageMipLevel: Int = 0,
) {
    internal fun native(a: SegmentAllocator) = Fields(a, KoralLayouts.KoralCopy)
        .long("buffer_offset", bufferOffset).long("buffer_row_length", bufferRowLength).long("buffer_image_height", bufferImageHeight)
        .ivec3("image_offset", imageOffset).ivec3("image_extent", imageExtent)
        .int("image_base_array_layer", imageBaseArrayLayer).int("image_layer_count", imageLayerCount).int("image_mip_level", imageMipLevel).segment
}

private fun Fields.ivec3(field: String, v: IVec3) = ints(field, v.x, v.y, v.z)

/**
 * kor::RenderInfo: where a render pass draws and how its attachments start and end — the screen, or a
 * framebuffer; cleared and stored by default; clear values overriding the framebuffer's.
 */
class RenderInfo(val target: Framebuffer? = null) {
    var colorLoad = LoadOperation.eClear; private set
    var depthLoad = LoadOperation.eClear; private set
    var stencilLoad = LoadOperation.eClear; private set
    var colorStore = StoreOperation.eStore; private set
    var depthStore = StoreOperation.eStore; private set
    var stencilStore = StoreOperation.eStore; private set
    private val clearColors = ArrayList<ClearColor?>()
    private var clearDepth: Float? = null
    private var clearStencil: Int? = null

    fun setColorLoadOperation(op: LoadOperation) = apply { colorLoad = op }
    fun setDepthLoadOperation(op: LoadOperation) = apply { depthLoad = op }
    fun setStencilLoadOperation(op: LoadOperation) = apply { stencilLoad = op }
    fun setColorStoreOperation(op: StoreOperation) = apply { colorStore = op }
    fun setDepthStoreOperation(op: StoreOperation) = apply { depthStore = op }
    fun setStencilStoreOperation(op: StoreOperation) = apply { stencilStore = op }
    fun setClearColor(index: Int, color: ClearColor) = apply {
        while (clearColors.size <= index) clearColors += null
        clearColors[index] = color
    }
    fun setClearColor(color: Vec4) = setClearColor(0, ClearColor(color))
    fun setClearDepth(depth: Float) = apply { clearDepth = depth }
    fun setClearStencil(stencil: Int) = apply { clearStencil = stencil }

    internal fun native(a: Arena): MemorySegment {
        val colors = a.allocate(KoralLayouts.KoralClearColor, maxOf(1, clearColors.size).toLong())
        clearColors.forEachIndexed { i, c -> c?.write(colors.asSlice(i * KoralLayouts.KoralClearColor.byteSize())) }
        return Fields(a, KoralLayouts.KoralRenderInfo).address("framebuffer", handleOf(target))
            .int("color_load", colorLoad.value).int("depth_load", depthLoad.value).int("stencil_load", stencilLoad.value)
            .int("color_store", colorStore.value).int("depth_store", depthStore.value).int("stencil_store", stencilStore.value)
            .address("clear_colors", colors).long("clear_color_count", clearColors.size.toLong())
            .bool("has_clear_depth", clearDepth != null).float("clear_depth", clearDepth ?: 1f)
            .bool("has_clear_stencil", clearStencil != null).int("clear_stencil", clearStencil ?: 0).segment
    }
}
