package koral

import java.lang.foreign.Arena
import java.lang.foreign.MemorySegment
import java.lang.foreign.ValueLayout
import koral.interop.KoralLayouts
import koral.interop.KoralNative

/** kor::Context: what the device can do. */
object Context {
    val hasDevice: Boolean get() = KoralNative.koral_context_has_device()
    val supportsRayTracing: Boolean get() = KoralNative.koral_context_supports_ray_tracing()
    val supportsAsyncCompute: Boolean get() = KoralNative.koral_context_supports_async_compute()
    val asyncComputeIsSeparateFamily: Boolean get() = KoralNative.koral_context_async_compute_is_separate_family()
    /** Whether the device was made with [feature] enabled: asked for, and the GPU has it. */
    fun supports(feature: Feature): Boolean = KoralNative.koral_context_supports_feature(feature.value.toLong())
    /** Whether the GPU has [feature], enabled or not. */
    fun gpuHas(feature: Feature): Boolean = KoralNative.koral_context_gpu_has_feature(feature.value.toLong())
}

/** Where assets are looked for: kor::AssetPath and its search paths. */
object Assets {
    fun path(relative: String): String = KoralNative.koral_asset_path(relative)
    fun addSearchPath(directory: String, front: Boolean = false) = KoralNative.koral_add_asset_search_path(directory, front)
}

/**
 * kor::Framebuffer: the images a render pass draws into.
 *
 * ```
 * val gbuffer = Framebuffer.Builder()
 *     .addColor(Framebuffer.ColorAttachment("albedo", albedo, clear = ClearColor(Vec4(0f, 0f, 0f, 1f))))
 *     .setDepth(Framebuffer.DepthStencilAttachment("depth", depth))
 *     .build()
 * ```
 */
class Framebuffer internal constructor(native: MemorySegment) : Resource(native) {
    /** kor::Framebuffer::Builder::ColorAttachment: [view] an Image (its own view) or an ImageView. */
    data class ColorAttachment(val name: String = "", val view: Resource? = null, val resolve: Resource? = null,
                               val clear: ClearColor = ClearColor.Black)

    /** kor::Framebuffer::Builder::DepthStencilAttachment. */
    data class DepthStencilAttachment(val name: String = "", val view: Resource? = null, val resolve: Resource? = null,
                                      val depth: Float = 1f, val stencil: Int = 0)

    /** kor::Framebuffer::Builder. */
    class Builder : koral.Builder(KoralNative.koral_framebuffer_builder_new()) {
        fun addColor(attachment: ColorAttachment) = apply {
            Arena.ofConfined().use { a ->
                KoralNative.koral_framebuffer_builder_add_color(native, attachment.name, attachment.view.source(), attachment.resolve.source(),
                                                                attachment.clear.native(a))
            }
        }
        fun setDepth(attachment: DepthStencilAttachment) = depthStencil(0, attachment)
        fun setStencil(attachment: DepthStencilAttachment) = depthStencil(1, attachment)
        fun setDepthStencil(attachment: DepthStencilAttachment) = depthStencil(2, attachment)
        private fun depthStencil(which: Int, a: DepthStencilAttachment) = apply {
            KoralNative.koral_framebuffer_builder_set_depth_stencil(native, which, a.name, a.view.source(), a.resolve.source(), a.depth, a.stencil)
        }
        fun setResolveMode(mode: ResolveMode) = apply { KoralNative.koral_framebuffer_builder_set_resolve_mode(native, mode.value) }
        fun build(): Framebuffer = built(KoralNative.koral_framebuffer_builder_build(native), "building a framebuffer")

        private fun Resource?.source(): MemorySegment {
            require(this == null || this is Image || this is ImageView) { "an attachment is an Image or an ImageView" }
            return handleOf(this)
        }
    }

    val isDefault: Boolean get() = KoralNative.koral_framebuffer_is_default(native)
    val colorAttachmentCount: Int get() = KoralNative.koral_framebuffer_color_attachment_count(native)
    val samples: SampleCount get() = SampleCount.of(KoralNative.koral_framebuffer_samples(native))
    val extent: UVec2 get() = twoInts({ x, y -> KoralNative.koral_framebuffer_extent(native, x, y) }, ::UVec2).also { checkLastError() }
    fun colorAttachment(index: Int): ImageView? = borrowed(KoralNative.koral_framebuffer_color_attachment(native, index))
    val hasDepthAttachment: Boolean get() = KoralNative.koral_framebuffer_has_depth_attachment(native)
    val depthAttachment: ImageView? get() = borrowed(KoralNative.koral_framebuffer_depth_attachment(native))
    val hasStencilAttachment: Boolean get() = KoralNative.koral_framebuffer_has_stencil_attachment(native)
    val stencilAttachment: ImageView? get() = borrowed(KoralNative.koral_framebuffer_stencil_attachment(native))
    val hasResolveAttachments: Boolean get() = KoralNative.koral_framebuffer_has_resolve_attachments(native)
    fun resolveAttachment(index: Int): ImageView? = borrowed(KoralNative.koral_framebuffer_resolve_attachment(native, index))
    fun attachmentNamed(name: String): ImageView? = borrowed(KoralNative.koral_framebuffer_attachment_named(native, name))
    fun imageNamed(name: String): Image? = borrowed(KoralNative.koral_framebuffer_image_named(native, name))
    fun colorImage(index: Int = 0): Image? = borrowed(KoralNative.koral_framebuffer_color_image(native, index))
    val depthImage: Image? get() = borrowed(KoralNative.koral_framebuffer_depth_image(native))
    val attachmentNames: List<String>
        get() = List(KoralNative.koral_framebuffer_attachment_name_count(native)) { KoralNative.koral_framebuffer_attachment_name(native, it) }
    fun clearColorAt(index: Int): ClearColor = Arena.ofConfined().use { a ->
        val c = a.allocate(KoralLayouts.KoralClearColor)
        KoralNative.koral_framebuffer_clear_color_at(native, index, c)
        checkLastError()
        ClearColor.from(c)
    }
    val clearDepth: Float get() = KoralNative.koral_framebuffer_clear_depth(native)
    val clearStencil: Int get() = KoralNative.koral_framebuffer_clear_stencil(native)
    val resolveMethod: ResolveMode get() = ResolveMode.of(KoralNative.koral_framebuffer_resolve_method(native))

    fun resize(extent: UVec2) {
        KoralNative.koral_framebuffer_resize(native, extent.x.toInt(), extent.y.toInt())
        checkLastError()
    }
}

/** kor::Mesh: vertex and index buffers, and how the vertices are laid out. */
class Mesh internal constructor(native: MemorySegment) : Resource(native) {
    /** kor::Mesh::Builder. */
    class Builder : koral.Builder(KoralNative.koral_mesh_builder_new()) {
        fun setVertexBuffer(binding: Int, vertexBuffer: Buffer) = apply { KoralNative.koral_mesh_builder_set_vertex_buffer(native, binding, vertexBuffer.native) }
        fun setIndexBuffer(indexBuffer: Buffer, indexType: ChannelType = ChannelType.eUInt) = apply {
            KoralNative.koral_mesh_builder_set_index_buffer(native, indexBuffer.native, indexType.value)
        }
        fun setVertexLayout(layout: VertexLayout) = apply {
            Arena.ofConfined().use { a -> KoralNative.koral_mesh_builder_set_vertex_layout(native, layout.native(a)) }
        }
        fun build(): Mesh = built(KoralNative.koral_mesh_builder_build(native), "building a mesh")
    }

    val vertexCount: Long get() = KoralNative.koral_mesh_vertex_count(native)
    val hasIndexBuffer: Boolean get() = KoralNative.koral_mesh_has_index_buffer(native)
    val indexCount: Int?
        get() = Arena.ofConfined().use { a ->
            val out = a.allocate(ValueLayout.JAVA_INT)
            if (KoralNative.koral_mesh_index_count(native, out)) out.get(ValueLayout.JAVA_INT, 0) else null
        }
    val indexType: ChannelType?
        get() = Arena.ofConfined().use { a ->
            val out = a.allocate(ValueLayout.JAVA_INT)
            if (KoralNative.koral_mesh_index_type(native, out)) ChannelType.of(out.get(ValueLayout.JAVA_INT, 0)) else null
        }
    val vertexBuffers: List<Buffer>
        get() = List(KoralNative.koral_mesh_vertex_buffer_count(native)) { borrowed<Buffer>(KoralNative.koral_mesh_vertex_buffer(native, it))!! }
    val indexBuffer: Buffer? get() = borrowed(KoralNative.koral_mesh_index_buffer(native))

    companion object {
        /** MakeBuffer(data, usage): a device-local buffer holding [data] (anything Buffer.Builder.setData takes), for a mesh. */
        fun makeBuffer(data: Any, vararg usage: BufferUsage): Buffer = Buffer.Builder().setData(data)
            .setUsage(*(usage.toList() + meshUsage()).toTypedArray()).setType(BufferType.eDeviceLocal).build()

        /** MakeBuffer(values, layout, usage). */
        fun <T> makeBuffer(values: List<T>, layout: GpuLayout<T>, vararg usage: BufferUsage): Buffer = Buffer.Builder().setData(values, layout)
            .setUsage(*(usage.toList() + meshUsage()).toTypedArray()).setType(BufferType.eDeviceLocal).build()

        private fun meshUsage(): List<BufferUsage> =
            listOf(BufferUsage.eTransferDst, BufferUsage.eTransferSrc, BufferUsage.eStorage) +
                (if (Context.supportsRayTracing) listOf(BufferUsage.eAccelerationStructureInput) else emptyList())
    }
}

/** kor::AccelerationStructure: meshes (bottom level) or instances of them (top level), for ray tracing. */
class AccelerationStructure internal constructor(native: MemorySegment) : Resource(native) {
    /** kor::AccelerationStructure::Geometry. */
    data class Geometry(val mesh: Mesh, val firstVertex: Long = 0, val vertexCount: Long = 0, val firstIndex: Long = 0, val indexCount: Long = 0)

    /** kor::AccelerationStructure::Instance. */
    data class Instance(val blas: AccelerationStructure, val transform: Mat4 = Mat4.Identity, val instanceCustomIndex: Int = 0,
                        val hitGroupIndex: Int = 0)

    /** kor::AccelerationStructure::Builder. */
    class Builder : koral.Builder(KoralNative.koral_acceleration_structure_builder_new()) {
        fun addMesh(mesh: Mesh) = apply { KoralNative.koral_acceleration_structure_builder_add_mesh(native, mesh.native) }
        fun addGeometry(geometry: Geometry) = apply {
            KoralNative.koral_acceleration_structure_builder_add_geometry(native, geometry.mesh.native, geometry.firstVertex, geometry.vertexCount,
                                                                          geometry.firstIndex, geometry.indexCount)
        }
        fun addInstance(instance: Instance) = apply {
            Arena.ofConfined().use { a ->
                KoralNative.koral_acceleration_structure_builder_add_instance(native, instance.blas.native,
                    a.allocateFrom(ValueLayout.JAVA_FLOAT, *instance.transform.toArray()), instance.instanceCustomIndex, instance.hitGroupIndex)
            }
        }
        fun build(): AccelerationStructure = built(KoralNative.koral_acceleration_structure_builder_build(native), "building an acceleration structure")
    }

    val type: AccelerationStructureType get() = AccelerationStructureType.of(KoralNative.koral_acceleration_structure_structure_type(native))
}
