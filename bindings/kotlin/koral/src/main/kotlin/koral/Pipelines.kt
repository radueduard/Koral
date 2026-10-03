package koral

import java.lang.foreign.Arena
import java.lang.foreign.MemorySegment
import java.lang.foreign.ValueLayout
import koral.interop.KoralNative

/** kor::Pipeline: what the three kinds share — their descriptor-set layouts and push constants. */
abstract class Pipeline internal constructor(native: MemorySegment) : Resource(native) {
    /** SetLayoutRef(index): the layout of set [index], as the shaders declare it. */
    fun setLayout(index: Int): DescriptorSetLayout = borrowed(KoralNative.koral_pipeline_set_layout(native, index))!!
    val usesDeviceAddresses: Boolean get() = KoralNative.koral_pipeline_uses_device_addresses(native)
    /** Whether the shaders have a push constant called [name]. */
    fun hasPushConstant(name: String): Boolean = KoralNative.koral_pipeline_has_push_constant(native, name)
}

/** A 4- or 8-byte specialization constant's bytes. */
internal fun specialization(a: Arena, value: Any): MemorySegment = when (value) {
    is Int -> a.allocateFrom(ValueLayout.JAVA_INT, value)
    is Float -> a.allocateFrom(ValueLayout.JAVA_FLOAT, value)
    is Boolean -> a.allocateFrom(ValueLayout.JAVA_INT, if (value) 1 else 0)   // a VkBool32
    is Long -> a.allocateFrom(ValueLayout.JAVA_LONG, value)
    is Double -> a.allocateFrom(ValueLayout.JAVA_DOUBLE, value)
    else -> throw IllegalArgumentException("a specialization constant is an Int, Float, Boolean, Long or Double")
}

/**
 * kor::GraphicsPipeline: vertex to fragment, built again when one of its shaders is edited.
 *
 * ```
 * val pipeline = GraphicsPipeline.Builder()
 *     .setVertexShader(Shader.Builder().setPath("mesh.vert").getOrBuild(), layout)
 *     .setFragmentShader(Shader.Builder().setPath("mesh.frag").getOrBuild())
 *     .setDepthStencilState(DepthStencilState(depthCompareOp = CompareOp.eLessOrEqual))
 *     .build()
 * ```
 */
class GraphicsPipeline internal constructor(native: MemorySegment) : Pipeline(native) {
    /** kor::GraphicsPipeline::Builder. */
    class Builder : koral.Builder(KoralNative.koral_graphics_pipeline_builder_new()) {
        fun setVertexShader(shader: Shader) = apply {
            KoralNative.koral_graphics_pipeline_builder_set_vertex_shader(native, shader.native, MemorySegment.NULL)
        }
        fun setVertexShader(shader: Shader, layout: VertexLayout) = apply {
            Arena.ofConfined().use { a -> KoralNative.koral_graphics_pipeline_builder_set_vertex_shader(native, shader.native, layout.native(a)) }
        }
        fun setTessellationState(state: TessellationState) = apply {
            KoralNative.koral_graphics_pipeline_builder_set_tessellation_state(native, state.controlShader.native, state.evalShader.native,
                                                                               state.patchControlPoints)
        }
        fun setGeometryShader(shader: Shader) = apply { KoralNative.koral_graphics_pipeline_builder_set_geometry_shader(native, shader.native) }
        fun setFragmentShader(shader: Shader) = apply { KoralNative.koral_graphics_pipeline_builder_set_fragment_shader(native, shader.native) }
        fun setTaskShader(shader: Shader) = apply { KoralNative.koral_graphics_pipeline_builder_set_task_shader(native, shader.native) }
        fun setMeshShader(shader: Shader) = apply { KoralNative.koral_graphics_pipeline_builder_set_mesh_shader(native, shader.native) }
        fun setInputAssemblyState(state: InputAssemblyState) = apply {
            Arena.ofConfined().use { a -> KoralNative.koral_graphics_pipeline_builder_set_input_assembly_state(native, state.native(a)) }
        }
        fun setRasterizationState(state: RasterizationState) = apply {
            Arena.ofConfined().use { a -> KoralNative.koral_graphics_pipeline_builder_set_rasterization_state(native, state.native(a)) }
        }
        fun setMultisampleState(state: MultisampleState) = apply {
            Arena.ofConfined().use { a -> KoralNative.koral_graphics_pipeline_builder_set_multisample_state(native, state.native(a)) }
        }
        fun setDepthStencilState(state: DepthStencilState) = apply {
            Arena.ofConfined().use { a -> KoralNative.koral_graphics_pipeline_builder_set_depth_stencil_state(native, state.native(a)) }
        }
        fun setColorBlendState(state: ColorBlendState) = apply {
            Arena.ofConfined().use { a -> KoralNative.koral_graphics_pipeline_builder_set_color_blend_state(native, state.native(a)) }
        }
        fun setFramebuffer(framebuffer: Framebuffer) = apply { KoralNative.koral_graphics_pipeline_builder_set_framebuffer(native, framebuffer.native) }
        /** SetSpecializationConstant(id, value): an Int, Float, Boolean, Long or Double. */
        fun setSpecializationConstant(id: Int, value: Any) = apply {
            Arena.ofConfined().use { a ->
                val v = specialization(a, value)
                KoralNative.koral_graphics_pipeline_builder_set_specialization_constant(native, id, v, v.byteSize().toInt())
            }
        }
        fun build(): GraphicsPipeline = built(KoralNative.koral_graphics_pipeline_builder_build(native), "building a graphics pipeline")
    }
}

/** kor::ComputePipeline. */
class ComputePipeline internal constructor(native: MemorySegment) : Pipeline(native) {
    /** kor::ComputePipeline::Builder. */
    class Builder : koral.Builder(KoralNative.koral_compute_pipeline_builder_new()) {
        fun setComputeShader(shader: Shader) = apply { KoralNative.koral_compute_pipeline_builder_set_compute_shader(native, shader.native) }
        fun setSpecializationConstant(id: Int, value: Any) = apply {
            Arena.ofConfined().use { a ->
                val v = specialization(a, value)
                KoralNative.koral_compute_pipeline_builder_set_specialization_constant(native, id, v, v.byteSize().toInt())
            }
        }
        fun build(): ComputePipeline = built(KoralNative.koral_compute_pipeline_builder_build(native), "building a compute pipeline")
    }
}

/** kor::RayTracingPipeline. */
class RayTracingPipeline internal constructor(native: MemorySegment) : Pipeline(native) {
    /** kor::RayTracingPipeline::HitGroup: any of the three may be absent. */
    data class HitGroup(val closestHitShader: Shader? = null, val anyHitShader: Shader? = null, val intersectionShader: Shader? = null)

    /** kor::RayTracingPipeline::Builder. */
    class Builder : koral.Builder(KoralNative.koral_ray_tracing_pipeline_builder_new()) {
        fun setRaygenShader(shader: Shader) = apply { KoralNative.koral_ray_tracing_pipeline_builder_set_raygen_shader(native, shader.native) }
        fun addMissShader(shader: Shader) = apply { KoralNative.koral_ray_tracing_pipeline_builder_add_miss_shader(native, shader.native) }
        fun addHitGroup(group: HitGroup) = apply {
            KoralNative.koral_ray_tracing_pipeline_builder_add_hit_group(native, handleOf(group.closestHitShader), handleOf(group.anyHitShader),
                                                                         handleOf(group.intersectionShader))
        }
        fun addCallableShader(shader: Shader) = apply { KoralNative.koral_ray_tracing_pipeline_builder_add_callable_shader(native, shader.native) }
        fun setMaxRecursionDepth(depth: Int) = apply { KoralNative.koral_ray_tracing_pipeline_builder_set_max_recursion_depth(native, depth) }
        fun build(): RayTracingPipeline = built(KoralNative.koral_ray_tracing_pipeline_builder_build(native), "building a ray-tracing pipeline")
    }

    val maxRecursionDepth: Int get() = KoralNative.koral_ray_tracing_pipeline_max_recursion_depth(native)
}

/** kor::DescriptorSetLayout: what a set holds, and where — made from a pipeline's shaders. */
class DescriptorSetLayout internal constructor(native: MemorySegment) : Resource(native) {
    /** FindBinding(name): the binding the shaders call [name], or null. */
    fun findBinding(name: String): Int? = KoralNative.koral_descriptor_set_layout_find_binding(native, name).takeIf { it >= 0 }?.toInt()
}

/**
 * kor::DescriptorSet: the resources a pipeline's shaders read, bound by name (as the shaders call them) or
 * by binding.
 *
 * ```
 * val set = DescriptorSet.Builder(pipeline, 0)
 *     .write("camera", cameraBuffer)
 *     .write("albedo", albedo, sampler)
 *     .build()
 * ```
 */
class DescriptorSet internal constructor(native: MemorySegment) : Resource(native) {
    /** kor::DescriptorSet::Builder: set [setIndex] of [pipeline]'s layout, or one of [layout]. */
    class Builder private constructor(target: MemorySegment, setIndex: Int) :
        koral.Builder(KoralNative.koral_descriptor_set_builder_new(target, setIndex)) {
        constructor(pipeline: Pipeline, setIndex: Int) : this(pipeline.native, setIndex)
        constructor(layout: DescriptorSetLayout) : this(layout.native, 0)

        fun write(binding: Int, buffer: Buffer, index: Int = 0) = apply { KoralNative.koral_descriptor_set_builder_write_buffer(native, null, binding, index, buffer.native) }
        fun write(name: String, buffer: Buffer) = apply { KoralNative.koral_descriptor_set_builder_write_buffer(native, name, 0, 0, buffer.native) }
        fun write(binding: Int, slice: Buffer.Slice, index: Int = 0) = apply {
            KoralNative.koral_descriptor_set_builder_write_buffer_slice(native, null, binding, index, slice.buffer.native, slice.offset, slice.size)
        }
        fun write(name: String, slice: Buffer.Slice) = apply {
            KoralNative.koral_descriptor_set_builder_write_buffer_slice(native, name, 0, 0, slice.buffer.native, slice.offset, slice.size)
        }
        fun write(binding: Int, view: BufferView, index: Int = 0) = apply { KoralNative.koral_descriptor_set_builder_write_buffer_view(native, null, binding, index, view.native) }
        fun write(name: String, view: BufferView) = apply { KoralNative.koral_descriptor_set_builder_write_buffer_view(native, name, 0, 0, view.native) }
        fun write(binding: Int, image: Image, sampler: Sampler? = null, index: Int = 0) = apply {
            KoralNative.koral_descriptor_set_builder_write_image(native, null, binding, index, image.native, handleOf(sampler))
        }
        fun write(name: String, image: Image, sampler: Sampler? = null) = apply {
            KoralNative.koral_descriptor_set_builder_write_image(native, name, 0, 0, image.native, handleOf(sampler))
        }
        fun write(binding: Int, view: ImageView, sampler: Sampler? = null, index: Int = 0) = apply {
            KoralNative.koral_descriptor_set_builder_write_image_view(native, null, binding, index, view.native, handleOf(sampler))
        }
        fun write(name: String, view: ImageView, sampler: Sampler? = null) = apply {
            KoralNative.koral_descriptor_set_builder_write_image_view(native, name, 0, 0, view.native, handleOf(sampler))
        }
        fun write(binding: Int, sampler: Sampler, index: Int = 0) = apply { KoralNative.koral_descriptor_set_builder_write_sampler(native, null, binding, index, sampler.native) }
        fun write(name: String, sampler: Sampler) = apply { KoralNative.koral_descriptor_set_builder_write_sampler(native, name, 0, 0, sampler.native) }
        fun write(binding: Int, structure: AccelerationStructure, index: Int = 0) = apply {
            KoralNative.koral_descriptor_set_builder_write_acceleration_structure(native, null, binding, index, structure.native)
        }
        fun write(name: String, structure: AccelerationStructure) = apply {
            KoralNative.koral_descriptor_set_builder_write_acceleration_structure(native, name, 0, 0, structure.native)
        }
        fun build(): DescriptorSet = built(KoralNative.koral_descriptor_set_builder_build(native), "building a descriptor set")
    }

    fun rebind(binding: Int, buffer: Buffer, index: Int = 0) = checked(KoralNative.koral_descriptor_set_rebind_buffer(native, null, binding, index, buffer.native), "rebinding")
    fun rebind(name: String, buffer: Buffer) = checked(KoralNative.koral_descriptor_set_rebind_buffer(native, name, 0, 0, buffer.native), "rebinding $name")
    fun rebind(binding: Int, slice: Buffer.Slice, index: Int = 0) =
        checked(KoralNative.koral_descriptor_set_rebind_buffer_slice(native, null, binding, index, slice.buffer.native, slice.offset, slice.size), "rebinding")
    fun rebind(name: String, slice: Buffer.Slice) =
        checked(KoralNative.koral_descriptor_set_rebind_buffer_slice(native, name, 0, 0, slice.buffer.native, slice.offset, slice.size), "rebinding $name")
    fun rebind(binding: Int, view: BufferView, index: Int = 0) = checked(KoralNative.koral_descriptor_set_rebind_buffer_view(native, null, binding, index, view.native), "rebinding")
    fun rebind(name: String, view: BufferView) = checked(KoralNative.koral_descriptor_set_rebind_buffer_view(native, name, 0, 0, view.native), "rebinding $name")
    fun rebind(binding: Int, view: ImageView, sampler: Sampler? = null, index: Int = 0) =
        checked(KoralNative.koral_descriptor_set_rebind_image_view(native, null, binding, index, view.native, handleOf(sampler)), "rebinding")
    fun rebind(name: String, view: ImageView, sampler: Sampler? = null) =
        checked(KoralNative.koral_descriptor_set_rebind_image_view(native, name, 0, 0, view.native, handleOf(sampler)), "rebinding $name")
    fun rebind(binding: Int, sampler: Sampler, index: Int = 0) = checked(KoralNative.koral_descriptor_set_rebind_sampler(native, null, binding, index, sampler.native), "rebinding")
    fun rebind(name: String, sampler: Sampler) = checked(KoralNative.koral_descriptor_set_rebind_sampler(native, name, 0, 0, sampler.native), "rebinding $name")
    fun rebind(binding: Int, structure: AccelerationStructure, index: Int = 0) =
        checked(KoralNative.koral_descriptor_set_rebind_acceleration_structure(native, null, binding, index, structure.native), "rebinding")
    fun rebind(name: String, structure: AccelerationStructure) =
        checked(KoralNative.koral_descriptor_set_rebind_acceleration_structure(native, name, 0, 0, structure.native), "rebinding $name")

    val layout: DescriptorSetLayout get() = borrowed(KoralNative.koral_descriptor_set_layout(native))!!
}
