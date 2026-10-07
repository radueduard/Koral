package koral

import java.lang.foreign.Arena
import java.lang.foreign.MemorySegment
import java.lang.foreign.SegmentAllocator
import java.lang.foreign.ValueLayout
import koral.interop.KoralLayouts

// kor::InputAssemblyState and the rest of a graphics pipeline's fixed state: the C++ structs, with the same
// defaults — `DepthStencilState(depthWriteEnable = false)` is `{ .depthWriteEnable = false }`.

/** kor::InputAssemblyState. */
data class InputAssemblyState(val topology: Topology = Topology.eTriangleList, val primitiveRestartEnable: Boolean = false) {
    internal fun native(a: SegmentAllocator) = Fields(a, KoralLayouts.KoralInputAssemblyState)
        .int("topology", topology.value).bool("primitive_restart_enable", primitiveRestartEnable).segment
}

/** kor::RasterizationState. */
data class RasterizationState(
    val depthClampEnable: Boolean = false,
    val rasterizerDiscardEnable: Boolean = false,
    val polygonMode: PolygonMode = PolygonMode.eFill,
    /** Which faces are not drawn: none by default. */
    val cullMode: Set<CullMode> = emptySet(),
    val frontFace: FrontFace = FrontFace.eCounterClockwise,
    val depthBiasEnable: Boolean = false,
    val depthBiasConstantFactor: Float = 0f,
    val depthBiasClamp: Float = 0f,
    val depthBiasSlopeFactor: Float = 0f,
    val lineWidth: Float = 1f,
) {
    internal fun native(a: SegmentAllocator) = Fields(a, KoralLayouts.KoralRasterizationState)
        .bool("depth_clamp_enable", depthClampEnable).bool("rasterizer_discard_enable", rasterizerDiscardEnable)
        .int("polygon_mode", polygonMode.value).int("cull_mode", cullMode.fold(0) { acc, c -> acc or c.value }).int("front_face", frontFace.value)
        .bool("depth_bias_enable", depthBiasEnable).float("depth_bias_constant_factor", depthBiasConstantFactor)
        .float("depth_bias_clamp", depthBiasClamp).float("depth_bias_slope_factor", depthBiasSlopeFactor)
        .float("line_width", lineWidth).segment
}

/** kor::MultisampleState. */
data class MultisampleState(val sampleCount: SampleCount = SampleCount.e1, val sampleShadingEnable: Boolean = false,
                            val minSampleShading: Float = 1f) {
    internal fun native(a: SegmentAllocator) = Fields(a, KoralLayouts.KoralMultisampleState)
        .int("sample_count", sampleCount.value).bool("sample_shading_enable", sampleShadingEnable)
        .float("min_sample_shading", minSampleShading).segment
}

/** kor::StencilOpState. */
data class StencilOpState(
    val failOp: StencilOp = StencilOp.eKeep,
    val passOp: StencilOp = StencilOp.eKeep,
    val depthFailOp: StencilOp = StencilOp.eKeep,
    val compareOp: CompareOp = CompareOp.eAlways,
    val compareMask: Int = 0,
    val writeMask: Int = 0,
    val reference: Int = 0,
) {
    internal fun write(f: Fields, prefix: String) {
        f.int("$prefix.fail_op", failOp.value).int("$prefix.pass_op", passOp.value).int("$prefix.depth_fail_op", depthFailOp.value)
            .int("$prefix.compare_op", compareOp.value).int("$prefix.compare_mask", compareMask)
            .int("$prefix.write_mask", writeMask).int("$prefix.reference", reference)
    }
}

/** kor::DepthStencilState. */
data class DepthStencilState(
    val depthTestEnable: Boolean = true,
    val depthWriteEnable: Boolean = true,
    val depthCompareOp: CompareOp = CompareOp.eLess,
    val depthBoundsEnable: Boolean = false,
    val stencilEnable: Boolean = false,
    val stencilFront: StencilOpState = StencilOpState(),
    val stencilBack: StencilOpState = StencilOpState(),
    val minDepth: Float = 0f,
    val maxDepth: Float = 1f,
) {
    internal fun native(a: SegmentAllocator): MemorySegment {
        val f = Fields(a, KoralLayouts.KoralDepthStencilState)
            .bool("depth_test_enable", depthTestEnable).bool("depth_write_enable", depthWriteEnable)
            .int("depth_compare_op", depthCompareOp.value).bool("depth_bounds_enable", depthBoundsEnable)
            .bool("stencil_enable", stencilEnable).float("min_depth", minDepth).float("max_depth", maxDepth)
        stencilFront.write(f, "stencil_front")
        stencilBack.write(f, "stencil_back")
        return f.segment
    }
}

/** kor::ColorBlendState. */
data class ColorBlendState(
    val enableLogicOp: Boolean = false,
    val logicOp: LogicOp = LogicOp.eCopy,
    val attachments: List<AttachmentState> = emptyList(),
    val blendConstants: Vec4 = Vec4.Zero,
) {
    /** kor::ColorBlendState::AttachmentState. */
    data class AttachmentState(
        val blendEnable: Boolean = false,
        val srcColorBlendFactor: BlendFactor = BlendFactor.eOne,
        val dstColorBlendFactor: BlendFactor = BlendFactor.eZero,
        val colorBlendOp: BlendOp = BlendOp.eAdd,
        val srcAlphaBlendFactor: BlendFactor = BlendFactor.eOne,
        val dstAlphaBlendFactor: BlendFactor = BlendFactor.eZero,
        val alphaBlendOp: BlendOp = BlendOp.eAdd,
        val colorWriteMask: Set<ColorComponent> = setOf(ColorComponent.eR, ColorComponent.eG, ColorComponent.eB, ColorComponent.eA),
    ) {
        internal fun write(segment: MemorySegment) {
            Fields(segment, KoralLayouts.KoralColorBlendAttachment)
                .bool("blend_enable", blendEnable)
                .int("src_color_blend_factor", srcColorBlendFactor.value).int("dst_color_blend_factor", dstColorBlendFactor.value)
                .int("color_blend_op", colorBlendOp.value)
                .int("src_alpha_blend_factor", srcAlphaBlendFactor.value).int("dst_alpha_blend_factor", dstAlphaBlendFactor.value)
                .int("alpha_blend_op", alphaBlendOp.value)
                .int("color_write_mask", colorWriteMask.fold(0) { acc, c -> acc or c.value })
        }

        companion object {
            /** Straight alpha blending: what a translucent colour over what is there needs. */
            val AlphaBlend = AttachmentState(blendEnable = true, srcColorBlendFactor = BlendFactor.eSrcAlpha,
                dstColorBlendFactor = BlendFactor.eOneMinusSrcAlpha, srcAlphaBlendFactor = BlendFactor.eOne,
                dstAlphaBlendFactor = BlendFactor.eOneMinusSrcAlpha)
        }
    }

    internal fun native(a: Arena): MemorySegment {
        val list = a.allocate(KoralLayouts.KoralColorBlendAttachment, maxOf(1, attachments.size).toLong())
        attachments.forEachIndexed { i, at -> at.write(list.asSlice(i * KoralLayouts.KoralColorBlendAttachment.byteSize())) }
        return Fields(a, KoralLayouts.KoralColorBlendState)
            .bool("enable_logic_op", enableLogicOp).int("logic_op", logicOp.value)
            .address("attachments", list).long("attachment_count", attachments.size.toLong())
            .floats("blend_constants", blendConstants.x, blendConstants.y, blendConstants.z, blendConstants.w).segment
    }
}

/** kor::TessellationState. */
data class TessellationState(val controlShader: Shader, val evalShader: Shader, val patchControlPoints: Int = 3)

/** kor::VertexInputBindingDescription. */
/** [inputRate] eInstance: the buffer is stepped through an instance at a time — per-instance data. */
data class VertexInputBindingDescription(val binding: Int, val stride: Int, val inputRate: VertexInputRate = VertexInputRate.eVertex)

/**
 * kor::VertexLayout: how a mesh's buffers are laid out, attributes matched to a vertex shader's inputs by
 * semantic (or placed at a location).
 */
data class VertexLayout(
    val bindings: List<VertexInputBindingDescription> = emptyList(),
    val attributes: List<Attribute> = emptyList(),
    /** Which attribute is the position (for acceleration structures and bounds), if any. */
    val positionAttribute: Int? = null,
) {
    /** kor::VertexLayout::Attribute. */
    data class Attribute(
        val semantic: String = "",
        val semanticNamespace: String = "",
        val binding: Int = 0,
        val offset: Int = 0,
        val channelType: ChannelType = ChannelType.eFloat,
        val channelCount: Int = 0,
        val location: Int? = null,
        /** How many consecutive shader locations it fills: 4 for a mat4 (a column each), N for an array of N. */
        val locations: Int = 1,
        /** The bytes from one of its locations to the next; 0: packed. */
        val locationStride: Int = 0,
    ) {
        companion object {
            /** A matrix of [columns] columns of [rows] floats, a location a column: `Attribute.matrix("TRANSFORM", 1, 0)`. */
            fun matrix(semantic: String, binding: Int, offset: Int, columns: Int = 4, rows: Int = 4) =
                Attribute(semantic = semantic, binding = binding, offset = offset, channelType = ChannelType.eFloat, channelCount = rows, locations = columns)
            fun atLocation(location: Int, binding: Int, offset: Int, channelType: ChannelType, channelCount: Int) =
                Attribute(location = location, binding = binding, offset = offset, channelType = channelType, channelCount = channelCount)
        }
    }

    val isEmpty: Boolean get() = bindings.isEmpty() && attributes.isEmpty()

    internal fun native(a: Arena): MemorySegment {
        val b = a.allocate(KoralLayouts.KoralVertexBinding, maxOf(1, bindings.size).toLong())
        bindings.forEachIndexed { i, d ->
            Fields(b.asSlice(i * KoralLayouts.KoralVertexBinding.byteSize()), KoralLayouts.KoralVertexBinding)
                .int("binding", d.binding).int("stride", d.stride).int("input_rate", d.inputRate.value)
        }
        val at = a.allocate(KoralLayouts.KoralVertexAttribute, maxOf(1, attributes.size).toLong())
        attributes.forEachIndexed { i, d ->
            Fields(at.asSlice(i * KoralLayouts.KoralVertexAttribute.byteSize()), KoralLayouts.KoralVertexAttribute)
                .string(a, "semantic", d.semantic).string(a, "semantic_namespace", d.semanticNamespace)
                .int("binding", d.binding).int("offset", d.offset).int("channel_type", d.channelType.value)
                .int("channel_count", d.channelCount).long("location", (d.location ?: -1).toLong())   // 64-bit: -1 written as 32 bits would read 4294967295
                .int("locations", d.locations).int("location_stride", d.locationStride)
        }
        return Fields(a, KoralLayouts.KoralVertexLayout)
            .address("bindings", b).long("binding_count", bindings.size.toLong())
            .address("attributes", at).long("attribute_count", attributes.size.toLong())
            .long("position_attribute", (positionAttribute ?: -1).toLong()).segment
    }
}

/**
 * kor::ClearColor: a float, int or uint vector, as the std::variant is — `ClearColor(Vec4(0f, 0f, 0f, 1f))`,
 * `ClearColor.ints(1, 2)`, `ClearColor.uints(7)`.
 */
class ClearColor private constructor(private val scalar: Int, private val components: Int, private val bits: IntArray) {
    constructor(color: Vec4) : this(0, 4, color.toArray().map { it.toRawBits() }.toIntArray())
    constructor(vararg floats: Float) : this(0, floats.size, floats.map { it.toRawBits() }.toIntArray())

    /** What it holds, as floats (for a float colour). */
    fun asVec4(): Vec4 { val f = FloatArray(4) { java.lang.Float.intBitsToFloat(bits.getOrElse(it) { 0 }) }; return Vec4(f[0], f[1], f[2], f[3]) }

    internal fun write(segment: MemorySegment) {
        Fields(segment, KoralLayouts.KoralClearColor).int("scalar_type", scalar).int("components", components)
            .ints("value", *IntArray(4) { bits.getOrElse(it) { 0 } })
    }

    internal fun native(a: SegmentAllocator): MemorySegment = a.allocate(KoralLayouts.KoralClearColor).also(::write)

    override fun equals(other: Any?) = other is ClearColor && scalar == other.scalar && components == other.components && bits.contentEquals(other.bits)
    override fun hashCode() = bits.contentHashCode() * 31 + scalar * 7 + components

    companion object {
        fun ints(vararg values: Int) = ClearColor(1, values.size, values.copyOf())
        fun uints(vararg values: Int) = ClearColor(2, values.size, values.copyOf())
        internal fun from(segment: MemorySegment): ClearColor {
            val f = Fields(segment, KoralLayouts.KoralClearColor)
            return ClearColor(f.readInt("scalar_type"), f.readInt("components"), IntArray(4) { f.readInt("value", it) })
        }
        val Black = ClearColor(Vec4(0f, 0f, 0f, 1f))
    }
}

